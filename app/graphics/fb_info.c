/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * fb_info — 打印帧缓冲设备信息
 *
 * 机制：open /dev/fb0 → ioctl(FBIOGET_FSCREENINFO/VSCREENINFO) → 打印 → close。
 * 系统调用：openat, ioctl, close, write
 *
 * 用法：
 *   fb_info              # 打印 /dev/fb0 的信息
 *   fb_info /dev/fb1     # 打印指定设备的信息
 */

/*
 * 索引：
 *   main               入口：打开 → ioctl → 打印 → 关闭
 */

#include "core.h"
#include "linux_fb.h"

/* 确保 printf 格式兼容性：
 *   %u  取 unsigned long（vararg 提升规则，uint32_t 可用）
 *   %lx 取 unsigned long 十六进制
 *   %s  取 char*
 */

static const char *visual_name(int visual)
{
    switch (visual) {
    case FB_VISUAL_MONO01:              return "MONO01";
    case FB_VISUAL_MONO10:              return "MONO10";
    case FB_VISUAL_TRUECOLOR:           return "Truecolor";
    case FB_VISUAL_PSEUDOCOLOR:         return "Pseudocolor";
    case FB_VISUAL_DIRECTCOLOR:         return "Directcolor";
    case FB_VISUAL_STATIC_PSEUDOCOLOR:  return "Static Pseudocolor";
    default:                            return "Unknown";
    }
}

static const char *type_name(int type)
{
    switch (type) {
    case FB_TYPE_PACKED_PIXELS: return "Packed Pixels";
    case FB_TYPE_PLANES:        return "Planes";
    case FB_TYPE_INTERLEAVED:   return "Interleaved";
    case FB_TYPE_TEXT:          return "Text";
    case FB_TYPE_VGA_PLANES:    return "VGA Planes";
    default:                    return "Unknown";
    }
}

int main(int argc, char *argv[])
{
    const char *dev_path = (argc > 1) ? argv[1] : "/dev/fb0";
    int fd, ret;

    fd = __openat(AT_FDCWD, dev_path, O_RDWR, 0);
    if (fd < 0) {
        __printf("fb_info: 无法打开 %s（%d）\n", dev_path, fd);
        return 1;
    }

    struct fb_var_screeninfo var;
    ret = __ioctl(fd, FBIOGET_VSCREENINFO, &var);
    if (ret < 0) {
        __printf("fb_info: FBIOGET_VSCREENINFO 失败（%d）\n", ret);
        __close(fd);
        return 1;
    }

    struct fb_fix_screeninfo fix;
    ret = __ioctl(fd, FBIOGET_FSCREENINFO, &fix);
    if (ret < 0) {
        __printf("fb_info: FBIOGET_FSCREENINFO 失败（%d）\n", ret);
        __close(fd);
        return 1;
    }

    __close(fd);

    /* 修正 id 字符串：kernel 可能不补 null 终止 */
    fix.id[15] = '\0';

    __printf("fb_info: %s\n", dev_path);
    __printf("  识别符:    %s\n", fix.id);
    __printf("  分辨率:    %u x %u", var.xres, var.yres);
    if (var.xres_virtual != var.xres || var.yres_virtual != var.yres)
        __printf("  (虚拟: %u x %u)", var.xres_virtual, var.yres_virtual);
    __printf("\n");
    __printf("  色深:      %u bpp\n", var.bits_per_pixel);
    __printf("  行长:      %u 字节/行\n", fix.line_length);
    __printf("  显存:      %u 字节", fix.smem_len);
    {
        unsigned int mib = fix.smem_len / 1048576;
        unsigned int kib = (fix.smem_len % 1048576) / 1024;
        if (mib)
            __printf("（%u MiB + %u KiB）", mib, kib);
        else if (kib)
            __printf("（%u KiB）", kib);
    }
    __printf("\n");
    __printf("  类型:      %s\n", type_name(fix.type));
    __printf("  视觉模式:  %s\n", visual_name(fix.visual));
    __printf("  物理地址:  0x%lx\n", fix.smem_start);

    if (var.width && var.height)
        __printf("  物理尺寸:  %u x %u mm\n", var.width, var.height);

    __printf("  RGB 位域:  R:%u/%u  G:%u/%u  B:%u/%u  A:%u/%u\n",
             var.red.offset, var.red.length,
             var.green.offset, var.green.length,
             var.blue.offset, var.blue.length,
             var.transp.offset, var.transp.length);

    if (var.pixclock)
        __printf("  像素时钟:  %u ps\n", var.pixclock);

    return 0;
}
