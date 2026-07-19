/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * fb_draw.c — 帧缓冲 2D 图元渲染
 *
 * 像素格式：XRGB8888（32bpp B≤G≤R 低位→高位）。
 * 坐标原点：屏幕左上角，x→右，y→下。
 *
 * 所有函数接受 fbp（mmap 后的显存指针）和 line_length（每行字节数）。
 *
 * 算法：
 *   直线       — Bresenham 整数增量算法
 *   圆(空心)   — 中点圆算法（八对称点同时画）
 *   圆(实心)   — 包围盒 x²+y²≤r² 判断填充
 *   矩形       — 空心四边 / 实心 uint32_t 批量写入
 */

#include "fb_draw.h"
#include "core.h"
#include "string.h"

/* ── 像素操作 ── */

void fb_put_pixel(unsigned char *fbp, int x, int y,
                  uint32_t color, int line_length)
{
    uint32_t *ptr = (uint32_t *)(fbp + y * line_length + x * 4);
    *ptr = color;
}

/* ── 辅助：画圆的 8 个对称点 ── */

static void circle_points(unsigned char *fbp, int cx, int cy,
                          int x, int y,
                          uint32_t color, int line_length)
{
    fb_put_pixel(fbp, cx + x, cy + y, color, line_length);
    fb_put_pixel(fbp, cx - x, cy + y, color, line_length);
    fb_put_pixel(fbp, cx + x, cy - y, color, line_length);
    fb_put_pixel(fbp, cx - x, cy - y, color, line_length);
    if (x != y) {
        fb_put_pixel(fbp, cx + y, cy + x, color, line_length);
        fb_put_pixel(fbp, cx - y, cy + x, color, line_length);
        fb_put_pixel(fbp, cx + y, cy - x, color, line_length);
        fb_put_pixel(fbp, cx - y, cy - x, color, line_length);
    }
}

/* ── 直线（Bresenham） ── */

void fb_draw_line(unsigned char *fbp,
                  int x0, int y0, int x1, int y1,
                  uint32_t color, int line_length)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        fb_put_pixel(fbp, x0, y0, color, line_length);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ── 空心矩形 ── */

void fb_draw_rect(unsigned char *fbp,
                  int x, int y, int w, int h,
                  uint32_t color, int line_length)
{
    for (int i = 0; i < w; i++) {
        fb_put_pixel(fbp, x + i, y, color, line_length);
        fb_put_pixel(fbp, x + i, y + h - 1, color, line_length);
    }
    for (int i = 1; i < h - 1; i++) {
        fb_put_pixel(fbp, x, y + i, color, line_length);
        fb_put_pixel(fbp, x + w - 1, y + i, color, line_length);
    }
}

/* ── 实心矩形（uint32_t 批量写入） ── */

void fb_fill_rect(unsigned char *fbp,
                  int x, int y, int w, int h,
                  uint32_t color, int line_length)
{
    for (int row = 0; row < h; row++) {
        uint32_t *start = (uint32_t *)(fbp + (y + row) * line_length + x * 4);
        for (int col = 0; col < w; col++)
            start[col] = color;
    }
}

/* ── 空心圆（中点圆算法） ── */

void fb_draw_circle(unsigned char *fbp,
                    int cx, int cy, int r,
                    uint32_t color, int line_length)
{
    if (r < 1) { fb_put_pixel(fbp, cx, cy, color, line_length); return; }
    int x = 0, y = r;
    int d = 1 - r;

    circle_points(fbp, cx, cy, x, y, color, line_length);
    while (x < y) {
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
        circle_points(fbp, cx, cy, x, y, color, line_length);
    }
}

/* ── 实心圆（包围盒填充） ── */

void fb_fill_circle(unsigned char *fbp,
                    int cx, int cy, int r,
                    uint32_t color, int line_length)
{
    if (r < 0) return;
    int left   = cx - r;
    int right  = cx + r;
    int top    = cy - r;
    int bottom = cy + r;

    for (int row = top; row <= bottom; row++) {
        for (int col = left; col <= right; col++) {
            int dx = col - cx;
            int dy = row - cy;
            if (dx * dx + dy * dy <= r * r)
                fb_put_pixel(fbp, col, row, color, line_length);
        }
    }
}

/* ── 显存保存/恢复（memcpy 已优化为 8 字节/次） ── */

void *fb_save(unsigned char *fbp, size_t size)
{
    void *buf = tlibc_malloc(size);
    if (buf) memcpy(buf, fbp, size);
    return buf;
}

void fb_restore(unsigned char *fbp, void *buf, size_t size)
{
    if (buf) {
        memcpy(fbp, buf, size);
        tlibc_free(buf);
    }
}
