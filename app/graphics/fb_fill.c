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
        __printf("fb_fill: 无法打开 /dev/fb0（%d）\n", fd);
        return 1;
    }

    /* ── 获取屏幕信息 ── */
    struct fb_var_screeninfo var;
    if (__ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        __printf("fb_fill: FBIOGET_VSCREENINFO 失败\n");
        __close(fd);
        return 1;
    }

    struct fb_fix_screeninfo fix;
    if (__ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        __printf("fb_fill: FBIOGET_FSCREENINFO 失败\n");
        __close(fd);
        return 1;
    }

    size_t screensize = var.yres_virtual * fix.line_length;

    __printf("fb_fill: %u×%u  %ubpp  %u bytes  颜色 0x%06X  延时 %ds\n",
             var.xres, var.yres, var.bits_per_pixel,
             (unsigned int)screensize, color, delay_sec);

    /* ── mmap 帧缓冲 ── */
    unsigned char *fbp = __mmap(0, screensize, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, 0);
    if (fbp == MAP_FAILED) {
        __printf("fb_fill: mmap 失败\n");
        __close(fd);
        return 1;
    }

    /* ── 保存原始 TTY 内容 ── */
    void *saved = fb_save(fbp, screensize);

    /* ── 全屏填充 ── */
    /* 32bpp XRGB8888：每次写入 4 字节 */
    uint32_t *pixels = (uint32_t *)fbp;
    unsigned int total = screensize / 4;
    for (unsigned int i = 0; i < total; i++)
        pixels[i] = color;

    __printf("fb_fill: 已填充 %u 像素，等待 %d 秒...\n", total, delay_sec);

    /* ── 保持显示 ── */
    tlibc_msleep(delay_sec * 1000);

    /* ── 恢复 TTY 原始内容 ── */
    fb_restore(fbp, saved, screensize);

    /* ── 清理 ── */
    __munmap(fbp, screensize);
    __close(fd);

    return 0;
}
