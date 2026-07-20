/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

#ifndef __FB_FONT_H
#define __FB_FONT_H

/*
 * fb_font — VGA 8×16 位图字体渲染
 *
 * 基于经典 VGA ROM 字体的 8×16 点阵位图，覆盖 ASCII 可打印字符 (0x20-0x7E)。
 * 坐标原点在屏幕左上角，x→右，y→下。
 * 像素格式：XRGB8888（同 fb_draw.h 约定）。
 *
 * 依赖：lib/graphics/fb_font.c
 *       需要 fb_draw.h (fb_put_pixel)
 */

#include "tlibc_types.h"

/* ── 字体尺寸（固定宽高） ── */
#define FB_FONT_W   8       /* 每个字符宽度（像素） */
#define FB_FONT_H   16      /* 每个字符高度（像素） */

/* ── 绘制单个字符（透明背景） ──
 *
 * 只绘制前景色像素，透明区域不写入显存。
 * 适合叠加在已有图形之上。 */
void fb_draw_char(unsigned char *fbp, int x, int y,
                  unsigned char ch, uint32_t color, int line_length);

/* ── 绘制字符串（透明背景） ──
 *
 * 逐字符调用 fb_draw_char，遇到 '\n' 换行、'\t' 跳到下一 4-字符对齐位置。
 * 超出屏幕右侧的部分不绘制。 */
void fb_draw_string(unsigned char *fbp, int x, int y,
                    const char *str, uint32_t color, int line_length);

/* ── 绘制单个字符（前景色 + 背景色） ──
 *
 * 用 bg 填充整个 8×16 字符区域，再绘制前景色像素。
 * 适合控制台风格文字。 */
void fb_draw_char_bg(unsigned char *fbp, int x, int y,
                     unsigned char ch, uint32_t fg, uint32_t bg,
                     int line_length);

/* ── 绘制字符串（前景色 + 背景色） ──
 *
 * 同上，带背景填充和 '\n' / '\t' 处理。 */
void fb_draw_string_bg(unsigned char *fbp, int x, int y,
                       const char *str, uint32_t fg, uint32_t bg,
                       int line_length);

/* ── 缩放绘制（scale ≥ 1，1 = 原始大小） ──
 *
 * 每个字体像素放大为 scale×scale 像素块。
 * 适合大屏展示（如 fb_evdev_kbd 的 2× 放大）。 */

void fb_draw_char_scaled(unsigned char *fbp, int x, int y,
                         unsigned char ch, uint32_t color,
                         int line_length, int scale);

void fb_draw_string_scaled(unsigned char *fbp, int x, int y,
                           const char *str, uint32_t color,
                           int line_length, int scale);

/* ── 查询函数 ── */

/* 返回字符串像素宽度（不处理 \n，仅计算水平像素和） */
int  fb_string_width(const char *str);

/* 返回字符串像素宽度（缩放版） */
int  fb_string_width_scaled(const char *str, int scale);

/* 返回字符宽度 */
int  fb_char_width(void);

/* 返回字符宽度（缩放版） */
int  fb_char_width_scaled(int scale);

/* 返回字体高度 */
int  fb_font_height(void);

/* 返回字体高度（缩放版） */
int  fb_font_height_scaled(int scale);

#endif /* __FB_FONT_H */
