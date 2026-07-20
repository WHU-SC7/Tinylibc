/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

#ifndef FB_DRAW_H
#define FB_DRAW_H

#include "tlibc_types.h"

/*
 * fb_draw — 帧缓冲 2D 图元库
 *
 * 坐标原点在屏幕左上角，x 向右、y 向下。
 * 所有函数接收显存指针（fbp）和行长（line_length）作为上下文参数。
 * 像素格式：XRGB8888（uint32_t，B 在最低 8 位）。
 *
 * 依赖：lib/graphics/fb_draw.c
 */

/* ── 像素操作 ── */

void fb_put_pixel(unsigned char *fbp, int x, int y,
                  uint32_t color, int line_length);

/* ── 直线（Bresenham） ── */

void fb_draw_line(unsigned char *fbp,
                  int x0, int y0, int x1, int y1,
                  uint32_t color, int line_length);

/* ── 矩形 ── */

void fb_draw_rect(unsigned char *fbp,
                  int x, int y, int w, int h,
                  uint32_t color, int line_length);

void fb_fill_rect(unsigned char *fbp,
                  int x, int y, int w, int h,
                  uint32_t color, int line_length);

/* ── 三角形（Bresenham 边 + 扫描线填充） ── */

void fb_draw_triangle(unsigned char *fbp,
                      int x0, int y0, int x1, int y1, int x2, int y2,
                      uint32_t color, int line_length);

void fb_fill_triangle(unsigned char *fbp,
                      int x0, int y0, int x1, int y1, int x2, int y2,
                      uint32_t color, int line_length);

/* ── 圆形（中点圆算法） ── */

void fb_draw_circle(unsigned char *fbp,
                    int cx, int cy, int r,
                    uint32_t color, int line_length);

void fb_fill_circle(unsigned char *fbp,
                    int cx, int cy, int r,
                    uint32_t color, int line_length);

/* ── 显存保存/恢复（恢复原始 TTY 内容） ── */

void *fb_save(unsigned char *fbp, size_t size);
void  fb_restore(unsigned char *fbp, void *buf, size_t size);

#endif /* FB_DRAW_H */
