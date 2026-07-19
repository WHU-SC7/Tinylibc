/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * fb_vsync — 垂直同步 + 弹跳球动画
 *
 * 演示 FBIO_WAITFORVSYNC 同步帧刷新，实现 60fps 平滑动画。
 * 系统调用：openat, ioctl, mmap, munmap, close, ppoll, clock_gettime
 *
 * 注意：需要在原始 TTY（Ctrl+Alt+F3）下运行。
 *
 * 用法：
 *   fb_vsync            # 默认 15 秒自动退出
 *   fb_vsync 30         # 运行 30 秒
 *   fb_vsync 0          # 运行到按 ESC 退出
 */

/*
 * 索引：
 *   main                 打开设备 → mmap → 动画循环（等待vsync→更新→绘制）→ 恢复退出
 *   poll_stdin           非阻塞检查键盘输入
 */

#include "core.h"
#include "fcntl.h"
#include "mman.h"
#include "linux_fb.h"
#include "fb_draw.h"

/* 颜色 */
#define COL_WHITE   0xFFFFFF
#define COL_RED     0x0000FF
#define COL_BLACK   0x000000
#define COL_CYAN    0xFFFF00

/* 轮询间隔（微秒）：检查键盘的频率 */
#define POLL_INTERVAL_US 50000  /* 50ms */

/* ── 非阻塞检查 stdin 是否有数据可读 ── */
static int poll_stdin(void)
{
    struct pollfd pfd;
    pfd.fd = 0;          /* stdin */
    pfd.events = POLLIN;
    if (__poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
        return 1;
    return 0;
}

/* ── 读一个字符（非阻塞，0 = 无输入） ── */
static int read_key(void)
{
    unsigned char c;
    if (__read(0, &c, 1) == 1)
        return c;
    return 0;
}

int main(int argc, char *argv[])
{
    int max_sec = 15;
    int run_forever = 0;
    if (argc > 1) {
        int n = 0;
        const char *p = argv[1];
        while (*p >= '0' && *p <= '9')
            n = n * 10 + (*p++ - '0');
        if (n > 0) max_sec = n;
        else if (n == 0) run_forever = 1;
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

    /* ── 弹跳球参数 ── */
    int bx = w / 2, by = h / 2;          /* 圆心位置 */
    int r = 30;                           /* 半径 */
    int dx = 4, dy = 3;                   /* 速度 */
    int trail = 0;                        /* 是否绘制拖尾轨迹 */

    /* ── 清屏 ── */
    fb_fill_rect(fbp, 0, 0, w, h, COL_BLACK, ll);

    /* ── 计时 ── */
    struct timespec t0;
    __clock_gettime(CLOCK_MONOTONIC, &t0);

    /* ── 状态信息（在终端上打印，不在屏幕上） ── */
    __printf("fb_vsync: %u×%u  弹跳球 %d 秒"
             "  按 ESC 或 q 提前退出\n",
             var.xres, var.yres, max_sec);

    int running = 1;
    while (running) {
        /* ── 等待垂直同步 ── */
        int vsync_arg = 0;
        __ioctl(fd, FBIO_WAITFORVSYNC, &vsync_arg);

        /* ── 擦除旧球（用黑色填充覆盖） ── */
        int old_bx = bx, old_by = by;
        fb_fill_circle(fbp, old_bx, old_by, r, COL_BLACK, ll);

        /* ── 更新位置 ── */
        bx += dx;
        by += dy;

        /* 边界反弹（考虑半径） */
        if (bx - r < 0)     { bx = r;     dx = -dx; }
        if (bx + r >= w)    { bx = w - r; dx = -dx; }
        if (by - r < 0)     { by = r;     dy = -dy; }
        if (by + r >= h)    { by = h - r; dy = -dy; }

        /* ── 绘制新球 ── */
        fb_fill_circle(fbp, bx, by, r, COL_CYAN, ll);
        fb_draw_circle(fbp, bx, by, r, COL_WHITE, ll);

        /* ── 检查键盘 ── */
        if (poll_stdin()) {
            int c = read_key();
            if (c == 0x1B || c == 'q' || c == 'Q')   /* ESC / q */
                running = 0;
        }

        /* ── 超时退出 ── */
        if (!run_forever) {
            struct timespec t1;
            __clock_gettime(CLOCK_MONOTONIC, &t1);
            long elapsed = (t1.tv_sec - t0.tv_sec);
            if (elapsed >= max_sec)
                running = 0;
        }

        /* 防止 busy loop——vsync 已经是 ~16ms 了，不需要额外延时 */
    }

    /* ── 恢复并退出 ── */
    fb_restore(fbp, saved, screensize);
    __munmap(fbp, screensize);
    __close(fd);

    return 0;
}
