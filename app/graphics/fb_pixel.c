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
#include "fb_font.h"
#include "tty.h"

/* KDSETMODE：切换 TTY 文本/图形模式 */
#define KDSETMODE     0x4B3A
#define KD_TEXT       0x00
#define KD_GRAPHICS   0x01

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
        __printf("fb_pixel: cannot open /dev/fb0 (%d)\n", fd);
        return 1;
    }

    struct fb_var_screeninfo var;
    if (__ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        __printf("fb_pixel: FBIOGET_VSCREENINFO failed\n");
        __close(fd);
        return 1;
    }

    struct fb_fix_screeninfo fix;
    if (__ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        __printf("fb_pixel: FBIOGET_FSCREENINFO failed\n");
        __close(fd);
        return 1;
    }

    size_t screensize = var.yres_virtual * fix.line_length;

    unsigned char *fbp = __mmap(0, screensize, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, 0);
    if (fbp == MAP_FAILED) {
        __printf("fb_pixel: mmap failed\n");
        __close(fd);
        return 1;
    }

    int w = var.xres;
    int h = var.yres;
    int ll = fix.line_length;

    /* ── 切换到图形模式 ── */
    int graphics_mode = 0;
    tlibc_set_term_raw_and_noecho(0);
    graphics_mode = 1;
    { int km = KD_GRAPHICS; __ioctl(0, KDSETMODE, &km); }

    __printf("fb_pixel: %u×%u  %ubpp  display %d s\n",
             var.xres, var.yres, var.bits_per_pixel, delay);

    /* ── 清屏为黑色（覆盖可能残留的 TTY 文本） ── */
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

    /* ── 覆层提示：标题 + 退出方法 ── */
    int bar_h = FB_FONT_H + 8;
    fb_fill_rect(fbp, 0, 0, w, bar_h, 0x000000, ll);
    fb_fill_rect(fbp, 0, h - 20, w, 20, 0x000000, ll);

    fb_draw_string(fbp, (w - fb_string_width("2D Primitive Demo  (fb_pixel)")) / 2, 4,
                   "2D Primitive Demo  (fb_pixel)", COL_WHITE, ll);

    fb_draw_string(fbp, 8, h - 18,
                   "Press Q to quit early  |  Auto-exit", 0x888888, ll);

    /* ── 保持显示（可提前按 Q 退出）── */
    {
        int remaining = delay;
        char countdown[8];
        while (remaining > 0) {
            snprintf(countdown, sizeof(countdown), "%ds", remaining);
            fb_fill_rect(fbp, w - 80, h - 18, 80, FB_FONT_H, 0x000000, ll);
            fb_draw_string(fbp, w - 80, h - 18, countdown, 0xCCCCCC, ll);

            for (int q = 0; q < 4; q++) {
                tlibc_msleep(250);
                struct pollfd pfd;
                pfd.fd = 0;
                pfd.events = POLLIN;
                if (__poll(&pfd, 1, 0) > 0) {
                    char c;
                    if (__read(0, &c, 1) == 1 && (c == 'q' || c == 'Q'))
                        goto done;
                }
            }
            remaining--;
        }
    }
done:

    /* ── 恢复 TTY 文本模式 ── */
    if (graphics_mode) {
        { int km = KD_TEXT; __ioctl(0, KDSETMODE, &km); }
        tlibc_restore_term(0);
    }

    __munmap(fbp, screensize);
    __close(fd);
    return 0;
}
