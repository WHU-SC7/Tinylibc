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

/* ── 三角形 ── */

void fb_draw_triangle(unsigned char *fbp,
                      int x0, int y0, int x1, int y1, int x2, int y2,
                      uint32_t color, int line_length)
{
	fb_draw_line(fbp, x0, y0, x1, y1, color, line_length);
	fb_draw_line(fbp, x1, y1, x2, y2, color, line_length);
	fb_draw_line(fbp, x2, y2, x0, y0, color, line_length);
}

void fb_fill_triangle(unsigned char *fbp,
                      int x0, int y0, int x1, int y1, int x2, int y2,
                      uint32_t color, int line_length)
{
	int t;

	/* 按 Y 递增排序三个顶点 */
	if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
	if (y1 > y2) { t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
	if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

	if (y0 == y2) return;                       /* 零高度退化三角形 */

	int dy_total = y2 - y0;
	int dy_high  = y1 - y0;
	int dy_low   = y2 - y1;

	/* 叉积符号：中点 (x1,y1) 在长边 (x0,y0)-(x2,y2) 的哪一侧 */
	int side = (x2 - x0) * (y1 - y0) - (x1 - x0) * (y2 - y0);

	for (int y = y0; y <= y2; y++) {
		int dist = y - y0;

		/* 长边 x 插值 */
		int x_long = x0 + (x2 - x0) * dist / dy_total;

		/* 短边 x 插值（上半段 + 下半段） */
		int x_other;
		if (dy_high > 0 && y < y1)
			x_other = x0 + (x1 - x0) * dist / dy_high;
		else if (dy_low > 0)
			x_other = x1 + (x2 - x1) * (y - y1) / dy_low;
		else
			x_other = x_long;

		int x_left  = (side > 0) ? x_other : x_long;
		int x_right = (side > 0) ? x_long : x_other;

		if (x_left > x_right) { t = x_left; x_left = x_right; x_right = t; }

		uint32_t *row = (uint32_t *)(fbp + y * line_length);
		for (int x = x_left; x <= x_right; x++)
			row[x] = color;
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
