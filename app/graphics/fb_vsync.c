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
#define COL_CYAN    0xFFFF00

/* 速度（像素/秒）—— 各帧率下视觉一致 */
#define SPEED_X    120
#define SPEED_Y     80

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

    /* 设置终端 raw 模式：按键无需回车即可读取 */
    tlibc_set_term_raw_and_noecho(0);

    int w = var.xres;
    int h = var.yres;
    int ll = fix.line_length;

    /* ── 球参数 ── */
    int bx = w / 2, by = h / 2;          /* 当前圆心（整数像素） */
    int r = 30;

    /* 固定点累加器：1 单位 = 1/1000000 像素 */
    long acc_x = 0, acc_y = 0;

    __printf("fb_vsync: %u×%u  %dfps  %dpx/s×%dpx/s  按 q 退出\n",
             var.xres, var.yres, fps, SPEED_X, SPEED_Y);

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
        if (dt_us > 100000) dt_us = 100000;   /* 最大 100ms，防止卡顿时跳太远 */
        t_last = now;

        /* 擦除旧球（半径+1 确保覆盖白色轮廓） */
        fb_fill_circle(fbp, bx, by, r + 1, COL_BLACK, ll);

        /* ── 更新位置（基于时间，固定点累加） ── */
        acc_x += SPEED_X * dt_us;
        acc_y += SPEED_Y * dt_us;

        int dx = (int)(acc_x / 1000000);
        int dy = (int)(acc_y / 1000000);
        acc_x %= 1000000;
        acc_y %= 1000000;

        bx += dx;
        by += dy;

        /* 边界反弹 */
        if (bx - r < 0)     { bx = r;     acc_x = -acc_x; }
        if (bx + r >= w)    { bx = w - r; acc_x = -acc_x; }
        if (by - r < 0)     { by = r;     acc_y = -acc_y; }
        if (by + r >= h)    { by = h - r; acc_y = -acc_y; }

        /* ── 绘制新球 ── */
        fb_fill_circle(fbp, bx, by, r, COL_CYAN, ll);
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

    /* 恢复终端设置 */
    tlibc_restore_term(0);

    fb_restore(fbp, saved, screensize);
    __munmap(fbp, screensize);
    __close(fd);
    return 0;
}
