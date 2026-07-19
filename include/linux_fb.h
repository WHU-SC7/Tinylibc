/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

#ifndef __LINUX_FB_H
#define __LINUX_FB_H

/*
 * Linux fbdev 接口 — ioctl 命令 + 数据结构
 *
 * 来源：Linux 内核 UAPI <linux/fb.h>，精简到本库需要的子集。
 * 结构体布局按 x86_64 自然对齐（8 字节指针/unsigned long）。
 */

#include "tlibc_types.h"

/* ── ioctl 命令 ── */

#define FBIOGET_VSCREENINFO    0x4600   /* 读 fb_var_screeninfo */
#define FBIOPUT_VSCREENINFO    0x4601   /* 写 fb_var_screeninfo */
#define FBIOGET_FSCREENINFO    0x4602   /* 读 fb_fix_screeninfo */
#define FBIOGETCMAP            0x4604   /* 读颜色表 */
#define FBIOPUTCMAP            0x4605   /* 写颜色表 */
#define FBIOSYNC               0x4606   /* 等待显存同步（历史别名，见 FBIOPAN_DISPLAY） */
#define FBIOPAN_DISPLAY         0x4606   /* 缓冲区切换（FBIOSYNC 同值）：设置 yoffset 翻转显示 */
#define FBIO_WAITFORVSYNC      0x80044620  /* 等待垂直消隐 */

/* ── fb_fix_screeninfo.type ── */

#define FB_TYPE_PACKED_PIXELS      0
#define FB_TYPE_PLANES             1
#define FB_TYPE_INTERLEAVED        2
#define FB_TYPE_TEXT               3
#define FB_TYPE_VGA_PLANES         4

/* ── fb_fix_screeninfo.visual ── */

#define FB_VISUAL_MONO01               0
#define FB_VISUAL_MONO10               1
#define FB_VISUAL_TRUECOLOR            2
#define FB_VISUAL_PSEUDOCOLOR          3
#define FB_VISUAL_DIRECTCOLOR          4
#define FB_VISUAL_STATIC_PSEUDOCOLOR   5

/* ── fb_var_screeninfo.activate ── */

#define FB_ACTIVATE_NOW         0
#define FB_ACTIVATE_NXTOPEN     1
#define FB_ACTIVATE_TEST        2
#define FB_ACTIVATE_VBL         16
#define FB_ACTIVATE_ALL         64
#define FB_ACTIVATE_FORCE       128
#define FB_ACTIVATE_INV_MODE    256

/* ── 位域描述（每通道在像素中的位置） ── */

struct fb_bitfield {
    uint32_t offset;       /* 偏移（低位起） */
    uint32_t length;       /* 位数 */
    uint32_t msb_right;    /* 0: MSB 在左 */
};

/* ── 固定屏幕信息（fb_fix_screeninfo） ──
 *
 * 偏移计算（x86_64，自然对齐）：
 *    0  id[16]        —— char[16], 16B
 *   16  smem_start    —— unsigned long, 8B
 *   24  smem_len      —— uint32_t, 4B
 *   28  type          —— uint32_t, 4B
 *   32  type_aux      —— uint32_t, 4B
 *   36  visual        —— uint32_t, 4B
 *   40  xpanstep      —— uint16_t, 2B
 *   42  ypanstep      —— uint16_t, 2B
 *   44  ywrapstep     —— uint16_t, 2B
 *   46  [padding]     —— 2B 对齐到 4
 *   48  line_length   —— uint32_t, 4B
 *   52  [padding]     —— 4B 对齐到 8
 *   56  mmio_start    —— unsigned long, 8B
 *   64  mmio_len      —— uint32_t, 4B
 *   68  accel         —— uint32_t, 4B
 *   72  capabilities  —— uint16_t, 2B
 *   74  reserved[2]   —— uint16_t×2, 4B
 *   78  [tail pad]    —— 2B，总大小 80
 */

struct fb_fix_screeninfo {
    char           id[16];             /* 标识符，如 "TT Builtin"     */
    unsigned long  smem_start;         /* 帧缓冲物理地址             */
    uint32_t       smem_len;           /* 帧缓冲字节数               */
    uint32_t       type;               /* FB_TYPE_*                  */
    uint32_t       type_aux;
    uint32_t       visual;             /* FB_VISUAL_*                */
    uint16_t       xpanstep;           /* 硬件平移步长               */
    uint16_t       ypanstep;
    uint16_t       ywrapstep;
    uint32_t       line_length;        /* 每行字节数                 */
    unsigned long  mmio_start;         /* MMIO 物理地址              */
    uint32_t       mmio_len;           /* MMIO 长度                  */
    uint32_t       accel;
    uint16_t       capabilities;
    uint16_t       reserved[2];
};

/* ── 可变屏幕信息（fb_var_screeninfo） ──
 *
 * 偏移计算（x86_64，自然对齐）：
 *    0    xres           —— uint32_t, 4B
 *    4    yres           —— uint32_t, 4B
 *    8    xres_virtual   —— uint32_t, 4B
 *   12    yres_virtual   —— uint32_t, 4B
 *   16    xoffset        —— uint32_t, 4B
 *   20    yoffset        —— uint32_t, 4B
 *   24    bits_per_pixel —— uint32_t, 4B
 *   28    grayscale      —— uint32_t, 4B
 *   32    red            —— struct fb_bitfield, 12B
 *   44    green          —— struct fb_bitfield, 12B
 *   56    blue           —— struct fb_bitfield, 12B
 *   68    transp         —— struct fb_bitfield, 12B
 *   80    nonstd         —— uint32_t, 4B
 *   84    activate       —— uint32_t, 4B
 *   88    height         —— uint32_t, 4B
 *   92    width          —— uint32_t, 4B
 *   96    accel_flags    —— uint32_t, 4B
 *  100    pixclock       —— uint32_t, 4B
 *  104    left_margin    —— uint32_t, 4B
 *  108    right_margin   —— uint32_t, 4B
 *  112    upper_margin   —— uint32_t, 4B
 *  116    lower_margin   —— uint32_t, 4B
 *  120    hsync_len      —— uint32_t, 4B
 *  124    vsync_len      —— uint32_t, 4B
 *  128    sync           —— uint32_t, 4B
 *  132    vmode          —— uint32_t, 4B
 *  136    rotate         —— uint32_t, 4B
 *  140    colorspace     —— uint32_t, 4B
 *  144    reserved[4]    —— uint32_t×4, 16B
 *  160    总大小 160 字节
 */

struct fb_var_screeninfo {
    uint32_t       xres;               /* 可见分辨率宽 */
    uint32_t       yres;               /* 可见分辨率高 */
    uint32_t       xres_virtual;       /* 虚拟分辨率宽 */
    uint32_t       yres_virtual;
    uint32_t       xoffset;
    uint32_t       yoffset;
    uint32_t       bits_per_pixel;
    uint32_t       grayscale;
    struct fb_bitfield red;            /* RGB 位域 */
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;         /* Alpha 位域 */
    uint32_t       nonstd;             /* 非标准像素格式 */
    uint32_t       activate;           /* FB_ACTIVATE_* */
    uint32_t       height;             /* 屏幕物理高度(mm) */
    uint32_t       width;              /* 屏幕物理宽度(mm) */
    uint32_t       accel_flags;        /* 已废弃 */
    uint32_t       pixclock;           /* 像素时钟(ps) */
    uint32_t       left_margin;        /* 时序参数 */
    uint32_t       right_margin;
    uint32_t       upper_margin;
    uint32_t       lower_margin;
    uint32_t       hsync_len;
    uint32_t       vsync_len;
    uint32_t       sync;
    uint32_t       vmode;
    uint32_t       rotate;
    uint32_t       colorspace;
    uint32_t       reserved[4];
};

#endif /* __LINUX_FB_H */
