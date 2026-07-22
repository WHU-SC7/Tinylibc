/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * fb_fill — 全屏填充纯色
 *
 * 机制：打开 /dev/fb0 → ioctl 获取分辨率 → mmap 帧缓冲 → 填充颜色。
 * 系统调用：openat, ioctl, mmap, munmap, close, nanosleep
 *
 * 注意：必须在原始 TTY（Ctrl+Alt+F3）下运行，Wayland/X11 桌面环境中
 *       帧缓冲被显示服务器接管，向 /dev/fb0 写入不可见。
 *
 * 用法：
 *   fb_fill             # 全屏填红色，2 秒后退出
 *   fb_fill 00ff00 5    # 全屏填绿色，5 秒后退出（颜色为 RRGGBB 十六进制）
 */

/*
 * 索引：
 *   main               解析参数 → 打开设备 → mmap → fill → munmap → 退出
 *   parse_color        将 "ff0000" 的 RGB 字符串转为 uint32_t 像素值
 */

#include "core.h"
#include "linux_fb.h"
#include "fcntl.h"
#include "mman.h"
#include "fb_draw.h"
#include "fb_font.h"
#include "tty.h"

/* KDSETMODE：切换 TTY 文本/图形模式 */
#define KDSETMODE     0x4B3A
#define KD_TEXT       0x00
#define KD_GRAPHICS   0x01

/* 将 RRGGBB 十六进制字符串解析为 uint32_t 像素值
 * 像素格式：XRGB8888（B 位低 8 位，G 中间 8 位，R 高 8 位） */
static uint32_t parse_color(const char *hex)
{
    uint32_t r = 0, g = 0, b = 0;
    int i;
    for (i = 0; hex[i] && i < 6; i++) {
        uint32_t v = 0;
        char c = hex[i];
        if (c >= '0' && c <= '9')
            v = c - '0';
        else if (c >= 'a' && c <= 'f')
            v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            v = c - 'A' + 10;
        else
            break;
        if (i < 2)
            r = (r << 4) | v;
        else if (i < 4)
            g = (g << 4) | v;
        else
            b = (b << 4) | v;
    }
    return (r << 16) | (g << 8) | b;
}

int main(int argc, char *argv[])
{
    /* ── 色彩参数 ── */
    uint32_t color = 0x0000FF;        /* 默认 = 红色（B 通道在 0-7 位） */
    int delay_sec = 2;

    if (argc > 1)
        color = parse_color(argv[1]);
    if (argc > 2)
        delay_sec = 0;
    for (int i = 2; i < argc; i++) {
        int n = 0;
        const char *p = argv[i];
        while (*p >= '0' && *p <= '9')
            n = n * 10 + (*p++ - '0');
        if (n > 0) delay_sec = n;
    }

    /* ── 打开设备 ── */
    int fd = __openat(AT_FDCWD, "/dev/fb0", O_RDWR, 0);
    if (fd < 0) {
        __printf("fb_fill: cannot open /dev/fb0 (%d)\n", fd);
        return 1;
    }

    /* ── 获取屏幕信息 ── */
    struct fb_var_screeninfo var;
    if (__ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        __printf("fb_fill: FBIOGET_VSCREENINFO failed\n");
        __close(fd);
        return 1;
    }

    struct fb_fix_screeninfo fix;
    if (__ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        __printf("fb_fill: FBIOGET_FSCREENINFO failed\n");
        __close(fd);
        return 1;
    }

    size_t screensize = var.yres_virtual * fix.line_length;

    /* ── mmap 帧缓冲 ── */
    unsigned char *fbp = __mmap(0, screensize, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, 0);
    if (fbp == MAP_FAILED) {
        __printf("fb_fill: mmap failed\n");
        __close(fd);
        return 1;
    }

    /* ── 切换到图形模式 ── */
    int graphics_mode = 0;
    tlibc_set_term_raw_and_noecho(0);
    graphics_mode = 1;
    { int km = KD_GRAPHICS; __ioctl(0, KDSETMODE, &km); }

    __printf("fb_fill: %u×%u  %ubpp  %u bytes  color 0x%06X  delay %ds\n",
             var.xres, var.yres, var.bits_per_pixel,
             (unsigned int)screensize, color, delay_sec);

    /* ── 全屏填充 ── */
    /* 32bpp XRGB8888：每次写入 4 字节 */
    uint32_t *pixels = (uint32_t *)fbp;
    unsigned int total = screensize / 4;
    for (unsigned int i = 0; i < total; i++)
        pixels[i] = color;

    /* ── 覆层提示：标题 + 颜色 + 退出方法 ── */
    /* 用半透明感暗色条作为文字背景 */
    int bar_h = FB_FONT_H + 8;
    int ll = fix.line_length;
    fb_fill_rect(fbp, 0, 0, var.xres, bar_h, 0x000000, ll);
    fb_fill_rect(fbp, 0, var.yres - bar_h, var.xres, bar_h, 0x000000, ll);

    char info_line[64];
    snprintf(info_line, sizeof(info_line), "fb_fill  |  Color 0x%06X  |  %u x %u", color, var.xres, var.yres);
    fb_draw_string(fbp, 8, 4, info_line, 0xAAAAAA, ll);

    fb_draw_string(fbp, 8, var.yres - bar_h + 4,
                   "Press Q to quit early  |  Auto-exit",
                   0x888888, ll);

    /* ── 保持显示（可提前按 Q 退出）── */
    {
        int remaining = delay_sec;
        char countdown[8];
        while (remaining > 0) {
            /* 更新倒计时 */
            snprintf(countdown, sizeof(countdown), "%ds", remaining);
            fb_fill_rect(fbp, var.xres - 80, var.yres - bar_h + 4, 80, FB_FONT_H, 0x000000, ll);
            fb_draw_string(fbp, var.xres - 80, var.yres - bar_h + 4, countdown, 0xCCCCCC, ll);

            /* 每秒等 4 次 250ms，仍能快速响应 Q */
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

    /* ── 清理 ── */
    __munmap(fbp, screensize);
    __close(fd);

    return 0;
}
