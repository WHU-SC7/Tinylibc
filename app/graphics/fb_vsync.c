/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * fb_vsync — 垂直同步 + 基于时间的弹跳球动画
 *
 * 机制：FBIO_WAITFORVSYNC 同步帧刷新 + 固定点时间步长，各帧率速度一致。
 * 速度单位：像素/秒，不依赖帧率。
 * 系统调用：openat, ioctl, mmap, munmap, close, poll, clock_gettime
 *
 * 注意：需要在原始 TTY（Ctrl+Alt+F3）下运行。
 *
 * 用法：
 *   fb_vsync           # 60fps 弹跳球，q 退出
 *   fb_vsync 30        # 30fps
 *   fb_vsync 20        # 20fps
 */

/*
 * 索引：
 *   main              打开设备 → mmap → 动画循环（vsync→计Δt→移球→绘制）→ 恢复退出
 */

#include "core.h"
#include "fcntl.h"
#include "mman.h"
#include "linux_fb.h"
#include "fb_draw.h"
#include "tty.h"

#define COL_WHITE   0xFFFFFF
#define COL_RED     0x0000FF
#define COL_BLACK   0x000000
#define COL_YELLOW  0xFFFF00

/* 速度（像素/秒）—— 运行时可正可负实现反弹 */
#define SPEED_BASE_X  300
#define SPEED_BASE_Y  200

/* KDSETMODE: 切换 TTY 文本/图形模式，防止控制台干扰显存 */
#define KDSETMODE     0x4B3A
#define KD_TEXT       0x00
#define KD_GRAPHICS   0x01

/* 安全超时（秒）：防止忘记退出时一直跑 */
#define MAX_SEC    300

static int poll_stdin(void)
{
    struct pollfd pfd;
    pfd.fd = 0;
    pfd.events = POLLIN;
    if (__poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
        return 1;
    return 0;
}

static int read_key(void)
{
    unsigned char c;
    if (__read(0, &c, 1) == 1) return c;
    return 0;
}

int main(int argc, char *argv[])
{
    int fps = 60;
    if (argc > 1) {
        int n = 0;
        const char *p = argv[1];
        while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
        if (n >= 1 && n <= 60) fps = n;
    }

    /* 帧延时：vsync ~16ms，剩余用 sleep 补足 */
    int frame_delay_ms = 1000 / fps - 16;
    if (frame_delay_ms < 0) frame_delay_ms = 0;

    /* ── 打开设备 ── */
    int fd = __openat(AT_FDCWD, "/dev/fb0", O_RDWR, 0);
    if (fd < 0) {
        __printf("fb_vsync: 无法打开 /dev/fb0（%d）\n", fd);
        return 1;
    }

    struct fb_var_screeninfo var;
    if (__ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        __printf("fb_vsync: FBIOGET_VSCREENINFO 失败\n");
        __close(fd);
        return 1;
    }

    struct fb_fix_screeninfo fix;
    if (__ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        __printf("fb_vsync: FBIOGET_FSCREENINFO 失败\n");
        __close(fd);
        return 1;
    }

    size_t screensize = var.yres_virtual * fix.line_length;

    unsigned char *fbp = __mmap(0, screensize, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, 0);
    if (fbp == MAP_FAILED) {
        __printf("fb_vsync: mmap 失败\n");
        __close(fd);
        return 1;
    }

    void *saved = fb_save(fbp, screensize);

    int w = var.xres;
    int h = var.yres;
    int ll = fix.line_length;

    /* ── 球参数 ── */
    int bx = w / 2, by = h / 2;
    int r = 30;
    int speed_x = SPEED_BASE_X;   /* 可正可负，反弹时取反 */
    int speed_y = SPEED_BASE_Y;

    /* 固定点累加器：1 单位 = 1/1000000 像素 */
    long acc_x = 0, acc_y = 0;

    /* ── 切换 TTY 到图形模式，阻止控制台文本干扰显存 ── */
    tlibc_set_term_raw_and_noecho(0);
    {
        int kd_mode = KD_GRAPHICS;
        __ioctl(0, KDSETMODE, &kd_mode);
    }

    __printf("fb_vsync: %u×%u  %dfps  %dpx/s×%dpx/s  按 q 退出\n",
             var.xres, var.yres, fps, SPEED_BASE_X, SPEED_BASE_Y);

    /* ── 清屏 ── */
    fb_fill_rect(fbp, 0, 0, w, h, COL_BLACK, ll);

    /* ── 初始化计时 ── */
    struct timespec t_start, t_last;
    __clock_gettime(CLOCK_MONOTONIC, &t_start);
    t_last = t_start;

    int running = 1;
    while (running) {
        /* ── 等待垂直同步 ── */
        int vsync_arg = 0;
        __ioctl(fd, FBIO_WAITFORVSYNC, &vsync_arg);

        /* ── 帧率补足 ── */
        if (frame_delay_ms > 0)
            tlibc_msleep(frame_delay_ms);

        /* ── 计算时间步长（微秒） ── */
        struct timespec now;
        __clock_gettime(CLOCK_MONOTONIC, &now);
        long dt_us = (now.tv_sec  - t_last.tv_sec) * 1000000
                   + (now.tv_nsec - t_last.tv_nsec) / 1000;
        if (dt_us > 100000) dt_us = 100000;
        t_last = now;

        /* 擦除旧球（半径+1 确保覆盖白色轮廓） */
        fb_fill_circle(fbp, bx, by, r + 1, COL_BLACK, ll);

        /* ── 更新位置（基于时间，速度方向随反弹变化） ── */
        acc_x += speed_x * dt_us;
        acc_y += speed_y * dt_us;

        int dx = (int)(acc_x / 1000000);
        int dy = (int)(acc_y / 1000000);
        acc_x %= 1000000;
        acc_y %= 1000000;

        bx += dx;
        by += dy;

        /* 边界反弹：反转速度方向，累加器归零 */
        if (bx - r < 0)     { bx = r;     speed_x = -speed_x; acc_x = 0; }
        if (bx + r >= w)    { bx = w - r; speed_x = -speed_x; acc_x = 0; }
        if (by - r < 0)     { by = r;     speed_y = -speed_y; acc_y = 0; }
        if (by + r >= h)    { by = h - r; speed_y = -speed_y; acc_y = 0; }

        /* ── 绘制新球 ── */
        fb_fill_circle(fbp, bx, by, r, COL_YELLOW, ll);
        fb_draw_circle(fbp, bx, by, r, COL_WHITE, ll);

        /* ── 键盘检测 ── */
        if (poll_stdin()) {
            int c = read_key();
            if (c == 'q' || c == 'Q')
                running = 0;
        }

        /* ── 安全超时 ── */
        long elapsed = now.tv_sec - t_start.tv_sec;
        if (elapsed >= MAX_SEC) running = 0;
    }

    /* ── 切换回 TTY 文本模式（控制台重绘其文本缓冲区到显存） ── */
    {
        int kd_mode = KD_TEXT;
        __ioctl(0, KDSETMODE, &kd_mode);
    }
    tlibc_restore_term(0);

    /* fb_restore 不再需要——KD_TEXT 已让控制台重绘了完整文本。
     * 保留 saved 的 tlibc_free 由 fb_restore 内部处理。 */
    if (saved) tlibc_free(saved);
    __munmap(fbp, screensize);
    __close(fd);
    return 0;
}
