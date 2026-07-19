/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * fb_vsync — 脏矩形 + 绝对时间步长弹跳球动画
 *
 * 机制：
 *   1. 脏矩形：仅擦除旧球位置 + 绘制新球位置，单帧开销与球面积成正比，
 *      而非整屏。~60×60 像素 vs 1920×1080，约 2000 倍差异。
 *   2. 绝对时间同步：clock_nanosleep(TIMER_ABSTIME) 锁定帧率，
 *      不依赖 fbdev ioctl 行为，任何驱动都稳定。
 *   3. 固定点时间步长：速度单位像素/秒，dt 由实际时钟计算，
 *      各帧率速度一致。
 *
 * 系统调用：openat, ioctl(FBIOGET_VSCREENINFO), mmap, munmap, close,
 *          poll, clock_gettime, clock_nanosleep
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
 *   main              打开设备 → mmap → 清屏 →
 *                     动画循环（clock_nanosleep→记旧位→移球→擦旧→画新）
 *                     → 恢复退出
 */

#include "core.h"
#include "fcntl.h"
#include "mman.h"
#include "linux_fb.h"
#include "fb_draw.h"
#include "tty.h"
#include "string.h"

#define COL_WHITE   0xFFFFFF
#define COL_BLACK   0x000000
#define COL_YELLOW  0xFFFF00

/* 速度（像素/秒）——运行时可正可负实现反弹 */
#define SPEED_BASE_X  300
#define SPEED_BASE_Y  200

/* KDSETMODE：切换 TTY 文本/图形模式 */
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

/* ── timespec 工具 ── */

/* ts += ns（归一化） */
static void ts_add_ns(struct timespec *ts, long ns)
{
    ts->tv_nsec += ns;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += ts->tv_nsec / 1000000000L;
        ts->tv_nsec %= 1000000000L;
    }
}

/* 返回 a − b 的微秒差 */
static long ts_diff_us(const struct timespec *a, const struct timespec *b)
{
    return (a->tv_sec - b->tv_sec) * 1000000L
         + (a->tv_nsec - b->tv_nsec) / 1000L;
}

int main(int argc, char *argv[])
{
    int fps = 60;
    if (argc > 1) {
        int n = 0;
        const char *p = argv[1];
        while (*p >= '0' && *p <= '9')
            n = n * 10 + (*p++ - '0');
        if (n >= 1 && n <= 60) fps = n;
    }

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

    int w = var.xres;
    int h = var.yres;
    int ll = fix.line_length;

    size_t screensize = (size_t)var.yres_virtual * ll;

    unsigned char *fbp = __mmap(0, screensize, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, 0);
    if (fbp == MAP_FAILED) {
        __printf("fb_vsync: mmap 失败\n");
        __close(fd);
        return 1;
    }

    /* ── 保存原始显存内容 ── */
    void *saved = fb_save(fbp, (size_t)h * ll);

    /* ── 球参数 ── */
    int bx = w / 2, by = h / 2;
    int r = 30;
    int speed_x = SPEED_BASE_X;
    int speed_y = SPEED_BASE_Y;
    long acc_x = 0, acc_y = 0;

    /* ── 切换 TTY 到图形模式 ── */
    int graphics_mode = 0;
    tlibc_set_term_raw_and_noecho(0);
    graphics_mode = 1;
    { int km = KD_GRAPHICS; __ioctl(0, KDSETMODE, &km); }

    __printf("fb_vsync: %u×%u  %dfps  脏矩形  %dpx/s×%dpx/s  按 q 退出\n",
             w, h, fps, SPEED_BASE_X, SPEED_BASE_Y);

    /* ── 初始清屏（仅一次，后续脏矩形只擦旧球） ── */
    fb_fill_rect(fbp, 0, 0, w, h, COL_BLACK, ll);

    /* ── 帧计时初始化 ── */
    long frame_interval_ns = 1000000000L / fps;
    struct timespec t_start, t_last, next_frame;
    __clock_gettime(CLOCK_MONOTONIC, &t_start);
    t_last = next_frame = t_start;
    ts_add_ns(&next_frame, frame_interval_ns);

    int running = 1;

    while (running) {
        /* ── 等 待 到 目 标 帧 绝 对 时 间 ──
         * clock_nanosleep(ABSTIME) 即使被信号中断也不会提前返回，
         * 目标已过时则立即返回 0。这是帧率稳定的关键。 */
        __clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame, NULL);

        struct timespec now;
        __clock_gettime(CLOCK_MONOTONIC, &now);

        /* 推进帧目标 */
        ts_add_ns(&next_frame, frame_interval_ns);

        /* 如果落后超过 1 帧，重置到当前时间（跳过追赶帧，避免爆帧） */
        long behind_us = ts_diff_us(&next_frame, &now);
        if (behind_us < 0) {
            behind_us = -behind_us;
            if ((unsigned long)behind_us > (unsigned long)(frame_interval_ns / 1000))
                next_frame = now;
        }

        /* ── 物理时间步长 ── */
        long dt_us = ts_diff_us(&now, &t_last);
        if (dt_us > 100000) dt_us = 100000;   /* 上限 100ms */
        if (dt_us < 0) dt_us = 0;
        t_last = now;

        /* ── 键盘检测 ── */
        if (poll_stdin()) {
            int c = read_key();
            if (c == 'q' || c == 'Q')
                running = 0;
        }

        /* ── 记下旧位置（擦除用） ── */
        int old_bx = bx, old_by = by;

        /* ── 更新球位置（固定点累加） ── */
        acc_x += speed_x * dt_us;
        acc_y += speed_y * dt_us;

        bx += (int)(acc_x / 1000000);
        by += (int)(acc_y / 1000000);
        acc_x %= 1000000;
        acc_y %= 1000000;

        /* 边界反弹 */
        if (bx - r < 0)     { bx = r;     speed_x = -speed_x; acc_x = 0; }
        if (bx + r >= w)    { bx = w - r; speed_x = -speed_x; acc_x = 0; }
        if (by - r < 0)     { by = r;     speed_y = -speed_y; acc_y = 0; }
        if (by + r >= h)    { by = h - r; speed_y = -speed_y; acc_y = 0; }

        /* ── 脏矩形绘制 ──
         *    擦除旧球（半径+1 确保覆盖白色轮廓）
         *    绘制新球 + 白色边缘                               */
        fb_fill_circle(fbp, old_bx, old_by, r + 1, COL_BLACK, ll);
        fb_fill_circle(fbp, bx, by, r, COL_YELLOW, ll);
        fb_draw_circle(fbp, bx, by, r, COL_WHITE, ll);

        /* ── 安全超时 ── */
        if (now.tv_sec - t_start.tv_sec >= MAX_SEC)
            running = 0;
    }

    /* ── 清理 ── */
    if (saved)
        fb_restore(fbp, saved, (size_t)h * ll);

    if (graphics_mode) {
        { int km = KD_TEXT; __ioctl(0, KDSETMODE, &km); }
        tlibc_restore_term(0);
    }

    __munmap(fbp, screensize);
    __close(fd);
    return 0;
}
