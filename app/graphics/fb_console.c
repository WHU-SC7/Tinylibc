/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * fb_console — 图形控制台：画布 + 命令行交互绘图
 *
 * 机制：屏幕分上下两区，上 80% 为画布（Canvas），下 20% 为控制台（Console）。
 *       画布用于显示绘图命令的结果，控制台用于输入命令和查看反馈。
 *       evdev 键盘输入，VGA 8×16 位图字体渲染文字。
 *
 * 内置命令：
 *   cls                   清空画布
 *   color <hex>           设置画笔颜色（RRGGBB）
 *   pixel <x> <y>         画点
 *   line <x1> <y1> <x2> <y2>  画直线
 *   rect <x> <y> <w> <h>       画矩形边框
 *   fill <x> <y> <w> <h>       画实心矩形
 *   circle <cx> <cy> <r>       画圆
 *   text <x> <y> <str>         在画布上写文字
 *   help                  显示帮助
 *   exit                  退出程序
 *
 * 系统调用：openat, ioctl(FBIOGET_VSCREENINFO/FSCREENINFO/KDSETMODE),
 *          mmap, munmap, close, poll, clock_gettime, clock_nanosleep
 *
 * 依赖：evdev_kbd, fb_draw, fb_font
 *
 * 用法：
 *   fb_console
 *
 * 退出：输入 exit 或按 Esc。
 */

/*
 * 索引：
 *   main                  入口 → 打开 fb0 → mmap → KD_GRAPHICS →
 *                         打开 evdev 键盘 → 主循环 → 清理退出
 *   execute_command       解析并执行绘图命令
 *   console_write         输出一行文字到控制台
 *   render_console        重绘控制台所有文字
 *   keycode_to_ascii      evdev 键码 → ASCII 字符
 */

#include "core.h"
#include "fcntl.h"
#include "mman.h"
#include "string.h"
#include "errno.h"
#include "linux_fb.h"
#include "linux_input.h"
#include "fb_draw.h"
#include "fb_font.h"
#include "evdev_kbd.h"
/* tty.h 定义的 KEY_UP/DOWN/LEFT/RIGHT (0x11-0x14) 与 linux_input.h 冲突，
 * 此处只前向声明需要的函数，不包含 tty.h。 */
int tlibc_set_term_raw_and_noecho(int fd);
int tlibc_restore_term(int fd);

/* snprintf 声明（定义在 lib/stdio/snprintf.c） */
int snprintf(char *str, unsigned long size, const char *fmt, ...);

/* sscanf 声明（定义在 lib/stdio/scanf.c，链接 tlibc.a 时可用） */
int sscanf(const char *str, const char *fmt, ...);

/* ── TTY 图形模式 ── */
#define KDSETMODE     0x4B3A
#define KD_TEXT       0x00
#define KD_GRAPHICS   0x01

/* ── 颜色 ── */
#define COL_BLACK      0x000000
#define COL_WHITE      0xFFFFFF
#define COL_GRAY       0x888888
#define COL_GRAY_DIM   0x444444
#define COL_GREEN      0x44FF44
#define COL_CYAN       0x44FFFF
#define COL_YELLOW     0xFFFF44
#define COL_RED        0xFF4444
#define COL_PROMPT     0x66FF66
#define COL_CONSOLE_BG 0x0A0E14
#define COL_CANVAS_BG  COL_BLACK
#define COL_SEPARATOR  0x2D4059

/* ── 字体缩放 ── */
#define FONT_SCALE   2       /* 2× 放大，16×32 像素/字符 */

/* ── 控制台缓冲区最大尺寸 ── */
#define CON_MAX_ROWS  64
#define CON_MAX_COLS  256

/* ════════════════════════════════════════════════════
 * 全局状态
 * ════════════════════════════════════════════════════ */

static unsigned char *g_fbp   = NULL;
static int           g_w      = 0;
static int           g_h      = 0;
static int           g_ll     = 0;          /* line_length */
static struct evdev_kbd *g_kbd = NULL;

/* ── 布局（在 main 中根据分辨率计算） ── */
static int g_canvas_h;       /* 画布区高度（px） */
static int g_con_y;          /* 控制台区 Y 起点 */
static int g_con_h;          /* 控制台区高度（px） */
static int g_rows;           /* 控制台文字行数 */
static int g_cols;           /* 控制台每行字符数 */

/* ── 画笔 ── */
static uint32_t g_color = 0x44FF44;   /* 默认绿色 */

/* ── Shift 修饰键 ── */
static int g_shift = 0;

/* ── 控制台行缓冲区 ── */
static char g_buf[CON_MAX_ROWS][CON_MAX_COLS];
static int  g_cur_row    = 0;          /* 当前写入行 */
static int  g_input_col  = 2;          /* 输入光标在当前行的列位置 */
static char g_cmd[256];                /* 正在编辑的命令 */
static int  g_cmd_len    = 0;

static int  g_dirty      = 1;           /* 控制台需要重绘 */

/* ════════════════════════════════════════════════════
 * 键盘 → ASCII（US 布局，支持 Shift）
 * ════════════════════════════════════════════════════ */

static char keycode_to_ascii(int code)
{
	/*
	 * 注意：evdev KEY_* 码并非按字母序连续分配（如 KEY_A=30, KEY_B=48），
	 * 不能通过偏移量计算。此处用 switch 逐一映射，与 fb_evdev_kbd 做法一致。
	 */
	switch (code) {
	/* ── 字母键 ── */
	case KEY_Q: return g_shift ? 'Q' : 'q';
	case KEY_W: return g_shift ? 'W' : 'w';
	case KEY_E: return g_shift ? 'E' : 'e';
	case KEY_R: return g_shift ? 'R' : 'r';
	case KEY_T: return g_shift ? 'T' : 't';
	case KEY_Y: return g_shift ? 'Y' : 'y';
	case KEY_U: return g_shift ? 'U' : 'u';
	case KEY_I: return g_shift ? 'I' : 'i';
	case KEY_O: return g_shift ? 'O' : 'o';
	case KEY_P: return g_shift ? 'P' : 'p';
	case KEY_A: return g_shift ? 'A' : 'a';
	case KEY_S: return g_shift ? 'S' : 's';
	case KEY_D: return g_shift ? 'D' : 'd';
	case KEY_F: return g_shift ? 'F' : 'f';
	case KEY_G: return g_shift ? 'G' : 'g';
	case KEY_H: return g_shift ? 'H' : 'h';
	case KEY_J: return g_shift ? 'J' : 'j';
	case KEY_K: return g_shift ? 'K' : 'k';
	case KEY_L: return g_shift ? 'L' : 'l';
	case KEY_Z: return g_shift ? 'Z' : 'z';
	case KEY_X: return g_shift ? 'X' : 'x';
	case KEY_C: return g_shift ? 'C' : 'c';
	case KEY_V: return g_shift ? 'V' : 'v';
	case KEY_B: return g_shift ? 'B' : 'b';
	case KEY_N: return g_shift ? 'N' : 'n';
	case KEY_M: return g_shift ? 'M' : 'm';

	/* ── 数字键 ── */
	case KEY_1: return g_shift ? '!' : '1';
	case KEY_2: return g_shift ? '@' : '2';
	case KEY_3: return g_shift ? '#' : '3';
	case KEY_4: return g_shift ? '$' : '4';
	case KEY_5: return g_shift ? '%' : '5';
	case KEY_6: return g_shift ? '^' : '6';
	case KEY_7: return g_shift ? '&' : '7';
	case KEY_8: return g_shift ? '*' : '8';
	case KEY_9: return g_shift ? '(' : '9';
	case KEY_0: return g_shift ? ')' : '0';

	/* ── 符号键 ── */
	case KEY_SPACE:      return ' ';
	case KEY_MINUS:      return g_shift ? '_' : '-';
	case KEY_EQUAL:      return g_shift ? '+' : '=';
	case KEY_LEFTBRACE:  return g_shift ? '{' : '[';
	case KEY_RIGHTBRACE: return g_shift ? '}' : ']';
	case KEY_SEMICOLON:  return g_shift ? ':' : ';';
	case KEY_APOSTROPHE: return g_shift ? '"' : '\'';
	case KEY_GRAVE:      return g_shift ? '~' : '`';
	case KEY_BACKSLASH:  return g_shift ? '|' : '\\';
	case KEY_COMMA:      return g_shift ? '<' : ',';
	case KEY_DOT:        return g_shift ? '>' : '.';
	case KEY_SLASH:      return g_shift ? '?' : '/';
	}
	return 0;
}

/* ════════════════════════════════════════════════════
 * 控制台文本管理
 * ════════════════════════════════════════════════════ */

/* 所有文本行上移一行，丢弃最旧行 */
static void console_scroll(void)
{
	for (int i = 1; i < g_rows; i++)
		memcpy(g_buf[i - 1], g_buf[i], g_cols);
	__memset(g_buf[g_rows - 1], ' ', g_cols);
	g_cur_row--;
}

/* 在当前写入位置设置新的 "> " 提示符 */
static void console_setup_prompt(void)
{
	__memset(g_buf[g_cur_row], ' ', g_cols);
	g_buf[g_cur_row][0] = '>';
	g_buf[g_cur_row][1] = ' ';
	g_input_col = 2;
	g_cmd_len = 0;
	g_cmd[0] = '\0';
	g_dirty = 1;
}

/* 输出一行或多行文字到控制台（在新行上开始，支持 \n） */
static void console_write(const char *s)
{
	/* 换到新行 */
	g_cur_row++;
	if (g_cur_row >= g_rows) console_scroll();
	__memset(g_buf[g_cur_row], ' ', g_cols);

	for (int col = 0; *s; s++) {
		if (*s == '\n') {
			g_cur_row++;
			if (g_cur_row >= g_rows) console_scroll();
			__memset(g_buf[g_cur_row], ' ', g_cols);
			col = 0;
		} else if (col < g_cols - 1) {
			g_buf[g_cur_row][col++] = *s;
		}
	}
	g_dirty = 1;
}

/* ════════════════════════════════════════════════════
 * 命令执行
 * ════════════════════════════════════════════════════ */

/* ── demo: 示例场景「夜空小镇」，展示所有绘图原语 ── */
static void cmd_demo(void)
{
	int cw = g_w, ch = g_canvas_h;
	int ground = ch * 55 / 100;

	/* 夜空 — fill_rect */
	fb_fill_rect(g_fbp, 0, 0, cw, ground, 0x0B0B2E, g_ll);

	/* 地面 — fill_rect */
	fb_fill_rect(g_fbp, 0, ground, cw, ch - ground, 0x1A2A1A, g_ll);

	/* 月亮 — fill_circle + draw_circle */
	fb_fill_circle(g_fbp, cw * 3 / 4, ch * 17 / 100, 40, 0xEEEEAA, g_ll);
	fb_draw_circle(g_fbp, cw * 3 / 4, ch * 17 / 100, 40, 0xFFFECC, g_ll);

	/* 星星 — put_pixel */
	{
		int sx[] = { 80,200,350,500,650, 80,220,400,580,750, 150,320,480,620,850 };
		int sy[] = { 15, 35, 12, 42, 18, 55, 70, 50, 28, 45, 25, 60,  8, 38, 20 };
		for (int i = 0; i < 15; i++)
			fb_put_pixel(g_fbp, sx[i] * cw / 900, sy[i] * ground / 100, 0xFFFFFF, g_ll);
	}

	/* 山脉剪影 — 用 fill_triangle 填充 + 亮色山脊线 */
	{
		int px[] = { 0,150,280,420,550,700,900 };
		int py[] = { 70, 40, 65, 30, 55, 35, 60 };
		for (int i = 0; i < 6; i++) {
			int x1 = px[i] * cw / 900, y1 = py[i] * ground / 100;
			int x2 = px[i + 1] * cw / 900, y2 = py[i + 1] * ground / 100;
			/* 每个山脊段拆成两个三角形填满到地面 */
			fb_fill_triangle(g_fbp, x1, y1, x2, y2, x2, ground, 0x2A2A5E, g_ll);
			fb_fill_triangle(g_fbp, x1, y1, x2, ground, x1, ground, 0x2A2A5E, g_ll);
			fb_draw_line(g_fbp, x1, y1, x2, y2, 0x4A5A8A, g_ll);
		}
	}

	/* 房子 — fill_rect + draw_rect + draw_line */
	{
		int hx = cw * 27 / 100, hy = ground - 65, hw = 110, hh = 85;
		fb_fill_rect(g_fbp, hx, hy, hw, hh, 0x8B5E3C, g_ll);
		fb_draw_rect(g_fbp, hx, hy, hw, hh, 0xAA7B5A, g_ll);
		fb_fill_triangle(g_fbp, hx - 10, hy, hx + hw / 2, hy - 45, hx + hw + 10, hy, 0xCC4444, g_ll);
		fb_fill_rect(g_fbp, hx + hw / 2 - 10, hy + hh - 40, 20, 40, 0x5C3A1E, g_ll);
		fb_draw_rect(g_fbp, hx + 15, hy + 18, 22, 22, 0x5599FF, g_ll);
		fb_draw_line(g_fbp, hx + 15, hy + 29, hx + 37, hy + 29, 0x5599FF, g_ll);
		fb_draw_line(g_fbp, hx + 26, hy + 18, hx + 26, hy + 40, 0x5599FF, g_ll);
		fb_fill_rect(g_fbp, hx + 16, hy + 19, 20, 20, 0xFFDD66, g_ll); /* 窗灯 */
	}

	/* 树 — fill_rect + fill_circle + draw_circle */
	{
		int tx = cw * 62 / 100, ty = ground;
		fb_fill_rect(g_fbp, tx - 6, ty - 30, 12, 50, 0x5C3A1E, g_ll);
		fb_fill_circle(g_fbp, tx, ty - 42, 30, 0x2D8B2D, g_ll);
		fb_fill_circle(g_fbp, tx - 16, ty - 18, 20, 0x3A9A3A, g_ll);
		fb_fill_circle(g_fbp, tx + 16, ty - 18, 20, 0x3A9A3A, g_ll);
		fb_draw_circle(g_fbp, tx, ty - 42, 30, 0x4CCC4C, g_ll);
	}

	/* 小径 — draw_line */
	fb_draw_line(g_fbp, cw * 27 / 100 + 45, ground + 20, cw * 40 / 100, ch - 10, 0x6B5D4B, g_ll);
	fb_draw_line(g_fbp, cw * 27 / 100 + 65, ground + 20, cw * 53 / 100, ch - 10, 0x6B5D4B, g_ll);

	/* 标题 — draw_string_scaled */
	fb_draw_string_scaled(g_fbp, cw / 2 - 110, 12, "Night in Tiny Town", 0x88BBFF, g_ll, 2);

	/* 分隔线 + 色块 */
	fb_draw_line(g_fbp, 0, g_canvas_h, cw, g_canvas_h, COL_SEPARATOR, g_ll);
	{
		int sx = cw - 40, sy = g_canvas_h - 34;
		fb_fill_rect(g_fbp, sx, sy, 24, 24, g_color, g_ll);
		fb_draw_rect(g_fbp, sx, sy, 24, 24, COL_GRAY, g_ll);
	}
}

static void execute_command(const char *cmd)
{
	int x1, y1, x2, y2, r;
	unsigned int hex;
	char str[64];

	if (strcmp(cmd, "cls") == 0) {
		fb_fill_rect(g_fbp, 0, 0, g_w, g_canvas_h, COL_CANVAS_BG, g_ll);
		/* 重绘画布 UI 元素 */
		fb_draw_line(g_fbp, 0, g_canvas_h, g_w, g_canvas_h, COL_SEPARATOR, g_ll);
		{
			const char *title = "fb_console - Interactive Drawing Console";
			int tx = (g_w - fb_string_width_scaled(title, 2)) / 2;
			fb_draw_string_scaled(g_fbp, tx, 24, title, COL_CYAN, g_ll, 2);
		}
		{
			const char *hint = "Enter 'help' for available commands";
			int tx = (g_w - fb_string_width(hint)) / 2;
			fb_draw_string(g_fbp, tx, 68, hint, COL_GRAY, g_ll);
		}
		{
			int sx = g_w - 40, sy = g_canvas_h - 34;
			fb_fill_rect(g_fbp, sx, sy, 24, 24, g_color, g_ll);
			fb_draw_rect(g_fbp, sx, sy, 24, 24, COL_GRAY, g_ll);
		}
	}
		else if (strcmp(cmd, "demo") == 0) {
			cmd_demo();
		}
	else if (strcmp(cmd, "help") == 0) {
		console_write("  cls                     Clear the canvas");
		console_write("  color <RRGGBB>          Set pen color (e.g. FF0000 = red)");
		console_write("  pixel <x> <y>           Draw a pixel at (x,y)");
		console_write("  line <x1> <y1> <x2> <y2>  Draw line from (x1,y1) to (x2,y2)");
		console_write("  rect <x> <y> <w> <h>    Draw rectangle outline at (x,y) size (w,h)");
		console_write("  fill <x> <y> <w> <h>    Fill solid rectangle at (x,y) size (w,h)");
		console_write("  circle <cx> <cy> <r>    Draw circle centered at (cx,cy) radius r");
		console_write("  text <x> <y> <string>   Draw text string at (x,y)");
		console_write("  demo                    Draw a demo night scene");
		console_write("--- System ---");
		console_write("  help                    Show this help");
		console_write("  exit                    Quit fb_console");
	}
	else if (sscanf(cmd, "color %x", &hex) == 1) {
		g_color = hex & 0xFFFFFF;
		int sx = g_w - 40, sy = g_canvas_h - 34;
		fb_fill_rect(g_fbp, sx, sy, 24, 24, g_color, g_ll);
		fb_draw_rect(g_fbp, sx, sy, 24, 24, COL_GRAY, g_ll);
	}
	else if (sscanf(cmd, "pixel %d %d", &x1, &y1) == 2) {
		if (x1 >= 0 && x1 < g_w && y1 >= 0 && y1 < g_canvas_h)
			fb_put_pixel(g_fbp, x1, y1, g_color, g_ll);
		else
			console_write("Error: coordinates out of range");
	}
	else if (sscanf(cmd, "line %d %d %d %d", &x1, &y1, &x2, &y2) == 4)
		fb_draw_line(g_fbp, x1, y1, x2, y2, g_color, g_ll);
	else if (sscanf(cmd, "rect %d %d %d %d", &x1, &y1, &x2, &y2) == 4)
		fb_draw_rect(g_fbp, x1, y1, x2, y2, g_color, g_ll);
	else if (sscanf(cmd, "fill %d %d %d %d", &x1, &y1, &x2, &y2) == 4)
		fb_fill_rect(g_fbp, x1, y1, x2, y2, g_color, g_ll);
	else if (sscanf(cmd, "circle %d %d %d", &x1, &y1, &r) == 3)
		fb_draw_circle(g_fbp, x1, y1, r, g_color, g_ll);
	else if (sscanf(cmd, "text %d %d %63s", &x1, &y1, str) == 3)
		fb_draw_string(g_fbp, x1, y1, str, g_color, g_ll);
	else {
			char ebuf[128];
			snprintf(ebuf, 128, "unknown: \"%s\"  (try 'help')", cmd);
			console_write(ebuf);
		}
}

/* ════════════════════════════════════════════════════
 * 控制台渲染
 * ════════════════════════════════════════════════════ */

static void render_console(void)
{
	int gh = FB_FONT_H * FONT_SCALE;   /* 32px */
	int gw = FB_FONT_W * FONT_SCALE;   /* 16px */

	/* 填充控制台区背景 */
	fb_fill_rect(g_fbp, 0, g_con_y, g_w, g_con_h, COL_CONSOLE_BG, g_ll);

	/* 逐字符绘制控制台文字 */
	for (int r = 0; r < g_rows; r++) {
		int y = g_con_y + r * gh;
		for (int c = 0; c < g_cols; c++) {
			char ch = g_buf[r][c];
			if (ch == ' ') continue;
			uint32_t col = (ch == '>') ? COL_PROMPT : COL_WHITE;
			fb_draw_char_scaled(g_fbp, c * gw, y,
			                    (unsigned char)ch, col, g_ll, FONT_SCALE);
		}
	}

	/* 光标 — 在输入行以半透明块显示当前位置 */
	{
		int cy = g_con_y + g_cur_row * gh;
		int cx = g_input_col * gw;
		/* 半透明白色块作为光标 */
		for (int row = 0; row < gh; row++) {
			uint32_t *cp = (uint32_t *)(g_fbp + (cy + row) * g_ll + cx * 4);
			for (int col = 0; col < gw; col++)
				cp[col] = 0x66FFFFFF;
		}
	}

	g_dirty = 0;
}

/* ════════════════════════════════════════════════════
 * 入口
 * ════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
	(void)argc; (void)argv;

	/* ── 打开 /dev/fb0 ── */
	int fb_fd = __openat(AT_FDCWD, "/dev/fb0", O_RDWR, 0);
	if (fb_fd < 0) {
		__printf("fb_console: cannot open /dev/fb0\n");
		return 1;
	}

	struct fb_var_screeninfo var;
	if (__ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0) {
		__printf("fb_console: FBIOGET_VSCREENINFO failed\n");
		__close(fb_fd);
		return 1;
	}
	struct fb_fix_screeninfo fix;
	if (__ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0) {
		__printf("fb_console: FBIOGET_FSCREENINFO failed\n");
		__close(fb_fd);
		return 1;
	}

	g_w  = var.xres;
	g_h  = var.yres;
	g_ll = fix.line_length;
	size_t screensize = (size_t)var.yres_virtual * g_ll;

	g_fbp = __mmap(0, screensize, PROT_READ | PROT_WRITE,
	               MAP_SHARED, fb_fd, 0);
	if (g_fbp == MAP_FAILED) {
		__close(fb_fd);
		return 1;
	}

	/* ── 计算布局（2× 缩放: 字宽 16px, 字高 32px） ── */
	g_canvas_h = g_h * 7 / 10;    /* 画布 70% */
	g_con_y    = g_canvas_h + 2;  /* 分隔线下方 */
	g_con_h    = g_h - g_con_y;   /* 控制台 30% */
	g_rows     = g_con_h / (FB_FONT_H * FONT_SCALE);
	g_cols     = g_w / (FB_FONT_W * FONT_SCALE);

	if (g_rows > CON_MAX_ROWS) g_rows = CON_MAX_ROWS;
	if (g_cols > CON_MAX_COLS) g_cols = CON_MAX_COLS;

	/* ── KD_GRAPHICS 模式 ── */
	int graphics = 0;
	tlibc_set_term_raw_and_noecho(0);
	graphics = 1;
	{ int km = KD_GRAPHICS; __ioctl(0, KDSETMODE, &km); }

	/* ── 打开 evdev 键盘 ── */
	g_kbd = evdev_kbd_open();
	if (!g_kbd) {
		__fprintf(2, "fb_console: no keyboard found via evdev\n");
		goto cleanup;
	}
	int kbd_fd = evdev_kbd_fd(g_kbd);

	/* ── 绘制初始画面 ── */

	/* 画布背景 */
	fb_fill_rect(g_fbp, 0, 0, g_w, g_canvas_h, COL_CANVAS_BG, g_ll);

	/* 标题 — 画布中央，2× 缩放 */
	{
		const char *title = "fb_console - Interactive Drawing Console";
		int tx = (g_w - fb_string_width_scaled(title, 2)) / 2;
		fb_draw_string_scaled(g_fbp, tx, 24, title, COL_CYAN, g_ll, 2);
	}
	/* 提示 — 标题下方，1× */
	{
		const char *hint = "Enter 'help' for available commands";
		int tx = (g_w - fb_string_width(hint)) / 2;
		fb_draw_string(g_fbp, tx, 68, hint, COL_GRAY, g_ll);
	}

	/* 分隔线 */
	fb_draw_line(g_fbp, 0, g_canvas_h, g_w, g_canvas_h, COL_SEPARATOR, g_ll);

	/* 颜色预览区 — 右下角（24×24 色块） */
	{
		int sx = g_w - 40, sy = g_canvas_h - 34;
		fb_fill_rect(g_fbp, sx, sy, 24, 24, g_color, g_ll);
		fb_draw_rect(g_fbp, sx, sy, 24, 24, COL_GRAY, g_ll);
	}

	/* 初始化控制台文本 */
	__memset(g_buf, ' ', sizeof(g_buf));
	g_cur_row = 0;
	console_setup_prompt();
	render_console();

	/* ── 帧率控制 ── */
	long interval_ns = 1000000000L / 60;
	struct timespec next_frame;
	__clock_gettime(CLOCK_MONOTONIC, &next_frame);

	/* ── 主循环 ── */
	int running = 1;

	while (running) {
		__clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame, NULL);
		next_frame.tv_nsec += interval_ns;
		if (next_frame.tv_nsec >= 1000000000L) {
			next_frame.tv_sec  += next_frame.tv_nsec / 1000000000L;
			next_frame.tv_nsec %= 1000000000L;
		}

		/* ── poll evdev 键盘 ── */
		struct pollfd pfd = {kbd_fd, POLLIN, 0};
		if (__poll(&pfd, 1, 0) > 0) {
			while (1) {
				int type, code = evdev_kbd_read(g_kbd, &type);
				if (code == 0) break;

				/* 跟踪 Shift */
				if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
					g_shift = (type == EVDEV_PRESS);
					continue;
				}

				if (type != EVDEV_PRESS)
					continue;

				if (code == KEY_ESC) {
					running = 0;
					break;
				}

				if (code == KEY_ENTER) {
					/* 当前行变为历史记录 */
					g_buf[g_cur_row][0] = ' ';
					g_buf[g_cur_row][1] = ' ';

					/* 执行命令 */
					if (g_cmd_len > 0) {
						g_cmd[g_cmd_len] = '\0';
						if (strcmp(g_cmd, "exit") == 0) {
							running = 0;
							break;
						}
						execute_command(g_cmd);
					}

					/* 换行 → 新提示符 */
					g_cur_row++;
					if (g_cur_row >= g_rows) console_scroll();
					console_setup_prompt();

				} else if (code == KEY_BACKSPACE) {
					if (g_cmd_len > 0) {
						g_cmd_len--;
						g_input_col--;
						g_buf[g_cur_row][g_input_col] = ' ';
						g_dirty = 1;
					}
				} else {
					char ch = keycode_to_ascii(code);
					if (ch && g_cmd_len < 255 && g_input_col < g_cols - 1) {
						g_cmd[g_cmd_len++] = ch;
						g_buf[g_cur_row][g_input_col++] = ch;
						g_dirty = 1;
					}
				}
			}
		}

		/* ── 重绘控制台 ── */
		if (g_dirty)
			render_console();
	}

	/* ════════════════════════════════════════════════
	 * 清理退出
	 * ════════════════════════════════════════════════ */

	if (graphics) {
		{ int km = KD_TEXT; __ioctl(0, KDSETMODE, &km); }
		/* 清空 stdin 防止残留 */
		{ struct pollfd spfd = {0, POLLIN, 0};
		  while (__poll(&spfd, 1, 0) > 0) {
		      char d[64];
		      if (__read(0, d, sizeof(d)) <= 0) break;
		  } }
		tlibc_restore_term(0);
	}

	if (g_kbd) evdev_kbd_close(g_kbd);
	__munmap(g_fbp, screensize);
	__close(fb_fd);

	__printf("fb_console: exit\n");
	return 0;

cleanup:
	if (graphics) {
		{ int km = KD_TEXT; __ioctl(0, KDSETMODE, &km); }
		tlibc_restore_term(0);
	}
	__munmap(g_fbp, screensize);
	__close(fb_fd);
	return 1;
}
