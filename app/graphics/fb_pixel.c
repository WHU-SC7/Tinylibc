/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * fb_pixel — 2D 图元演示
 *
 * 演示 fb_draw 库的所有功能：直线、矩形、圆、实心填充。
 * 系统调用：openat, ioctl, mmap, munmap, close
 *
 * 注意：需要在原始 TTY（Ctrl+Alt+F3）下运行，Wayland/X11 中不可见。
 *
 * 用法：
 *   fb_pixel             # 运行演示，5 秒后退出
 *   fb_pixel 10          # 保持 10 秒后退出
 */

/*
 * 索引：
 *   main                 打开设备 → mmap → 依次绘制各图元 → 等待 → 退出
 */

#include "core.h"
#include "fcntl.h"
#include "mman.h"
#include "linux_fb.h"
#include "fb_draw.h"

/* ── 颜色常量（XRGB8888：B 在最低 8 位） ── */

#define COL_RED     0x0000FF
#define COL_GREEN   0x00FF00
#define COL_BLUE    0xFF0000
#define COL_CYAN    0xFFFF00
#define COL_MAGENTA 0xFF00FF
#define COL_YELLOW  0x00FFFF
#define COL_WHITE   0xFFFFFF
#define COL_BLACK   0x000000

int main(int argc, char *argv[])
{
    int delay = 5;
    if (argc > 1) {
        int n = 0;
        const char *p = argv[1];
        while (*p >= '0' && *p <= '9')
            n = n * 10 + (*p++ - '0');
        if (n > 0) delay = n;
    }

    /* ── 打开设备 ── */
    int fd = __openat(AT_FDCWD, "/dev/fb0", O_RDWR, 0);
    if (fd < 0) {
        __printf("fb_pixel: 无法打开 /dev/fb0（%d）\n", fd);
        return 1;
    }

    struct fb_var_screeninfo var;
    if (__ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        __printf("fb_pixel: FBIOGET_VSCREENINFO 失败\n");
        __close(fd);
        return 1;
    }

    struct fb_fix_screeninfo fix;
    if (__ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        __printf("fb_pixel: FBIOGET_FSCREENINFO 失败\n");
        __close(fd);
        return 1;
    }

    size_t screensize = var.yres_virtual * fix.line_length;

    unsigned char *fbp = __mmap(0, screensize, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, 0);
    if (fbp == MAP_FAILED) {
        __printf("fb_pixel: mmap 失败\n");
        __close(fd);
        return 1;
    }

    int w = var.xres;
    int h = var.yres;
    int ll = fix.line_length;

    /* ── 保存原始 TTY 内容 ── */
    void *saved = fb_save(fbp, screensize);

    /* ── 清屏为黑色 ── */
    fb_fill_rect(fbp, 0, 0, w, h, COL_BLACK, ll);

    /* ── 1. 线条：十字 + 对角线 + 星形 ── */
    int cx = w / 2, cy = h / 2;
    int radius = (w < h ? w : h) / 2 - 20;

    /* 水平线 */
    fb_draw_line(fbp, 20, cy, w - 20, cy, COL_RED, ll);
    /* 垂直线 */
    fb_draw_line(fbp, cx, 20, cx, h - 20, COL_GREEN, ll);
    /* 对角线 */
    fb_draw_line(fbp, 20, 20, w - 20, h - 20, COL_BLUE, ll);
    fb_draw_line(fbp, w - 20, 20, 20, h - 20, COL_YELLOW, ll);
    /* 更多线条形成星形 */
    fb_draw_line(fbp, 20, cy / 2, w - 20, cy / 2, COL_CYAN, ll);
    fb_draw_line(fbp, 20, h - cy / 2, w - 20, h - cy / 2, COL_MAGENTA, ll);
    fb_draw_line(fbp, cx / 2, 20, cx / 2, h - 20, COL_WHITE, ll);
    fb_draw_line(fbp, w - cx / 2, 20, w - cx / 2, h - 20, COL_RED, ll);

    /* ── 2. 矩形（左上区域） ── */
    fb_draw_rect(fbp, 40, 40, 200, 150, COL_WHITE, ll);
    fb_fill_rect(fbp, 50, 50, 180, 130, COL_BLUE, ll);

    /* ── 3. 圆（右上区域） ── */
    fb_draw_circle(fbp, w - 200, 160, 120, COL_GREEN, ll);
    fb_fill_circle(fbp, w - 200, 160, 80, COL_RED, ll);

    /* ── 4. 颜色条（底部） ── */
    int bar_w = w / 8;
    uint32_t colors[] = {
        COL_RED, COL_GREEN, COL_BLUE,
        COL_CYAN, COL_MAGENTA, COL_YELLOW,
        COL_WHITE, 0xAAAAAA
    };
    for (int i = 0; i < 8; i++)
        fb_fill_rect(fbp, i * bar_w, h - 60, bar_w, 60, colors[i], ll);

    /* ── 5. 同心圆（中心） ── */
    for (int r = radius; r > 0; r -= 20)
        fb_draw_circle(fbp, cx, cy, r,
                       r % 60 == 0 ? COL_WHITE : colors[(r / 20) % 8], ll);

    __printf("fb_pixel: %u×%u  %ubpp  显示 %d 秒\n",
             var.xres, var.yres, var.bits_per_pixel, delay);

    tlibc_msleep(delay * 1000);

    /* ── 恢复 TTY 原始内容 ── */
    fb_restore(fbp, saved, screensize);

    __munmap(fbp, screensize);
    __close(fd);
    return 0;
}
