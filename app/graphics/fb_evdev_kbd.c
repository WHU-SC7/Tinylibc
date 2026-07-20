/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * fb_evdev_kbd — TTY 图形模式下通过 evdev 读取键盘的演示 / 测试工具
 *
 * 机制：
 *   1. KD_GRAPHICS 模式下 stdin 不再可靠，程序通过 evdev_kbd 库
 *      直接从 /dev/input/event* 读取键盘事件
 *   2. 2× 放大 VGA 8×16 位图字体（fb_font 库），清晰显示按下的键
 *   3. 实时修饰键状态条（Shift/Ctrl/Alt/Caps）
 *
 * 系统调用：openat, ioctl(FBIOGET_VSCREENINFO/FSCREENINFO/KDSETMODE),
 *          mmap, munmap, close, poll, clock_gettime, clock_nanosleep
 *
 * 依赖：evdev_kbd (lib/evdev_kbd.c), fb_font (lib/graphics/fb_font.c)
 *
 * 用法：
 *   fb_evdev_kbd
 *
 * 退出：Esc（或等待 5 分钟自动退出）。
 */

/*
 * 索引：
 *   main             入口 → 打开 fb → mmap → KD_GRAPHICS →
 *                    打开 evdev 键盘 → poll 循环 → Esc 退出
 *     key_name       键码（KEY_*）→ 可读名称（含按键符号）
 */

#include "core.h"
#include "fcntl.h"
#include "mman.h"
#include "string.h"
#include "errno.h"
#include "linux_input.h"
#include "linux_fb.h"
#include "fb_draw.h"
#include "fb_font.h"

/* tty.h 定义旧版 KEY_UP/DOWN/LEFT/RIGHT（0x11-0x14）与 evdev 冲突，
 * 此处只前向声明需要的函数，不包含 tty.h。 */
int tlibc_set_term_raw_and_noecho(int fd);
int tlibc_restore_term(int fd);

#include "evdev_kbd.h"

/* ═══════════════════════════════════════════════════
 * 颜色
 * ═══════════════════════════════════════════════════ */

#define COL_BLACK    0x000000
#define COL_WHITE    0xFFFFFF
#define COL_GRAY     0x555555
#define COL_GRAY_DIM 0x333333
#define COL_GREEN    0x44FF44
#define COL_YELLOW   0xFFFF44
#define COL_RED      0xFF4444
#define COL_CYAN     0x44FFFF
#define COL_ORANGE   0xFF8800

/* ═══════════════════════════════════════════════════
 * TTY 图形模式常量
 * ═══════════════════════════════════════════════════ */

#define KDSETMODE     0x4B3A
#define KD_TEXT       0x00
#define KD_GRAPHICS   0x01

/* ═══════════════════════════════════════════════════
 * 显示常量
 * ═══════════════════════════════════════════════════ */

/* 使用 fb_font VGA 8×16 字体，2× 放大
 *   glyph 尺寸: 8×2=16px 宽, 16×2=32px 高
 *   字符间距/行高: glyph + 2px 空白 */
#define FONT_SCALE  2
#define GLYPH_W     (FB_FONT_W * FONT_SCALE)    /* 16 */
#define GLYPH_H     (FB_FONT_H * FONT_SCALE)    /* 32 */
#define CHAR_W      (GLYPH_W + FONT_SCALE)       /* 16 + 2 = 18px */
#define LINE_H      (GLYPH_H + FONT_SCALE)       /* 32 + 2 = 34px */

#define MARGIN_X  16
#define MARGIN_TOP 10

/* ═══════════════════════════════════════════════════
 * 键码（KEY_*）→ 可读名称
 * ═══════════════════════════════════════════════════ */

static const char *key_name(int code)
{
    static char buf[12];

    /* ── 字母键（evdev 键码非连续非字母序，逐一手动映射） ── */
    switch (code) {
    case KEY_Q: return "Q"; case KEY_W: return "W"; case KEY_E: return "E";
    case KEY_R: return "R"; case KEY_T: return "T"; case KEY_Y: return "Y";
    case KEY_U: return "U"; case KEY_I: return "I"; case KEY_O: return "O";
    case KEY_P: return "P";
    case KEY_A: return "A"; case KEY_S: return "S"; case KEY_D: return "D";
    case KEY_F: return "F"; case KEY_G: return "G"; case KEY_H: return "H";
    case KEY_J: return "J"; case KEY_K: return "K"; case KEY_L: return "L";
    case KEY_Z: return "Z"; case KEY_X: return "X"; case KEY_C: return "C";
    case KEY_V: return "V"; case KEY_B: return "B"; case KEY_N: return "N";
    case KEY_M: return "M";

    /* ── 数字键 ── */
    case KEY_1: return "1"; case KEY_2: return "2"; case KEY_3: return "3";
    case KEY_4: return "4"; case KEY_5: return "5"; case KEY_6: return "6";
    case KEY_7: return "7"; case KEY_8: return "8"; case KEY_9: return "9";
    case KEY_0: return "0";

    /* ── 符号键 ── */
    case KEY_MINUS:      return "-";
    case KEY_EQUAL:      return "=";
    case KEY_LEFTBRACE:  return "[";
    case KEY_RIGHTBRACE: return "]";
    case KEY_SEMICOLON:  return ";";
    case KEY_APOSTROPHE: return "'";
    case KEY_GRAVE:      return "`";
    case KEY_BACKSLASH:  return "\\";
    case KEY_COMMA:      return ",";
    case KEY_DOT:        return ".";
    case KEY_SLASH:      return "/";
    case KEY_SPACE:      return "Spc";

    /* ── 特殊键 ── */
    case KEY_ESC:       return "Esc";
    case KEY_TAB:       return "Tab";
    case KEY_ENTER:     return "Enter";
    case KEY_BACKSPACE: return "BS";
    case KEY_LEFTSHIFT: return "LShift";
    case KEY_RIGHTSHIFT:return "RShift";
    case KEY_LEFTCTRL:  return "LCtrl";
    case KEY_RIGHTCTRL: return "RCtrl";
    case KEY_LEFTALT:   return "LAlt";
    case KEY_RIGHTALT:  return "RAlt";
    case KEY_CAPSLOCK:  return "Caps";
    case KEY_UP:        return "Up";
    case KEY_DOWN:      return "Dn";
    case KEY_LEFT:      return "Lt";
    case KEY_RIGHT:     return "Rt";
    case KEY_HOME:      return "Home";
    case KEY_END:       return "End";
    case KEY_PAGEUP:    return "PgUp";
    case KEY_PAGEDOWN:  return "PgDn";
    case KEY_INSERT:    return "Ins";
    case KEY_DELETE:    return "Del";
    }

    /* ── F1–F12 ── */
    if (code >= KEY_F1 && code <= KEY_F12) {
        int n = code - KEY_F1 + 1;
        buf[0] = 'F';
        if (n >= 10) { buf[1] = '0' + n / 10; buf[2] = '0' + n % 10; buf[3] = 0; }
        else         { buf[1] = '0' + n; buf[2] = 0; }
        return buf;
    }

    /* ── 未识别：显示十六进制编码 ── */
    buf[0] = '#';
    buf[1] = "0123456789abcdef"[(code >> 4) & 0xF];
    buf[2] = "0123456789abcdef"[code & 0xF];
    buf[3] = '\0';
    return buf;
}

/* ═══════════════════════════════════════════════════
 * 字体工具 — 委托给 fb_font 库
 * ═══════════════════════════════════════════════════ */

/* 计算字符串渲染宽度（像素，含缩放） */
static int text_width(const char *s)
{
    return fb_string_width_scaled(s, FONT_SCALE);
}

/* ═══════════════════════════════════════════════════
 * 修饰键状态
 * ═══════════════════════════════════════════════════ */

static int g_lshift  = 0;
static int g_rshift  = 0;
static int g_lctrl   = 0;
static int g_rctrl   = 0;
static int g_lalt    = 0;
static int g_ralt    = 0;
static int g_caps    = 0;

static void update_mod(int code, int press)
{
    switch (code) {
    case KEY_LEFTSHIFT:  g_lshift = press; break;
    case KEY_RIGHTSHIFT: g_rshift = press; break;
    case KEY_LEFTCTRL:   g_lctrl  = press; break;
    case KEY_RIGHTCTRL:  g_rctrl  = press; break;
    case KEY_LEFTALT:    g_lalt   = press; break;
    case KEY_RIGHTALT:   g_ralt   = press; break;
    case KEY_CAPSLOCK:
        if (press) g_caps = !g_caps;
        break;
    }
}

/* 格式化修饰键状态字符串到 buf */
static void format_mods(char *buf, int size)
{
    int pos = 0;
    if (g_lshift)  { strcpy(buf + pos, "LShift "); pos += 7; }
    if (g_rshift)  { strcpy(buf + pos, "RShift "); pos += 7; }
    if (g_lctrl)   { strcpy(buf + pos, "LCtrl ");  pos += 6; }
    if (g_rctrl)   { strcpy(buf + pos, "RCtrl ");  pos += 6; }
    if (g_lalt)    { strcpy(buf + pos, "LAlt ");   pos += 5; }
    if (g_ralt)    { strcpy(buf + pos, "RAlt ");   pos += 5; }
    if (g_caps)    { strcpy(buf + pos, "Caps");    pos += 4; }
    if (pos == 0)  strcpy(buf, "(none)");
}

/* ═══════════════════════════════════════════════════
 * 历史记录 — 最近按下的按键
 * ═══════════════════════════════════════════════════ */

#define HISTORY_MAX  8
static int  g_history_codes[HISTORY_MAX] = {0};
static int  g_history_count = 0;

/* 记录一个按键到历史。达到上限时丢弃最旧的。 */
static void push_history(int code)
{
    if (g_history_count < HISTORY_MAX) {
        g_history_codes[g_history_count++] = code;
    } else {
        for (int i = 0; i < HISTORY_MAX - 1; i++)
            g_history_codes[i] = g_history_codes[i + 1];
        g_history_codes[HISTORY_MAX - 1] = code;
    }
}

/* 格式化历史记录字符串到 buf */
static void format_history(char *buf, int size)
{
    int pos = 0;
    int start = (g_history_count < HISTORY_MAX) ? 0 : g_history_count - HISTORY_MAX;
    int n = (g_history_count < HISTORY_MAX) ? g_history_count : HISTORY_MAX;
    for (int i = start; i < start + n; i++) {
        const char *kn = key_name(g_history_codes[i]);
        while (*kn && pos < size - 2) buf[pos++] = *kn++;
        if (pos < size - 2) buf[pos++] = ' ';
    }
    buf[pos] = '\0';
}

/* ═══════════════════════════════════════════════════
 * 入口
 * ═══════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* ── 打开 /dev/fb0 ── */
    int fb_fd = __openat(AT_FDCWD, "/dev/fb0", O_RDWR, 0);
    if (fb_fd < 0) {
        __printf("fb_evdev_kbd: cannot open /dev/fb0\n");
        return 1;
    }

    struct fb_var_screeninfo var;
    if (__ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0) {
        __printf("fb_evdev_kbd: FBIOGET_VSCREENINFO failed\n");
        __close(fb_fd);
        return 1;
    }

    struct fb_fix_screeninfo fix;
    if (__ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        __printf("fb_evdev_kbd: FBIOGET_FSCREENINFO failed\n");
        __close(fb_fd);
        return 1;
    }

    int w = var.xres;
    int h = var.yres;
    int ll = fix.line_length;
    size_t screensize = (size_t)var.yres_virtual * ll;

    unsigned char *fbp = __mmap(0, screensize, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fb_fd, 0);
    if (fbp == MAP_FAILED) {
        __printf("fb_evdev_kbd: mmap failed\n");
        __close(fb_fd);
        return 1;
    }

    /* ── KD_GRAPHICS 模式 ── */
    int graphics_mode = 0;
    tlibc_set_term_raw_and_noecho(0);
    graphics_mode = 1;
    { int km = KD_GRAPHICS; __ioctl(0, KDSETMODE, &km); }

    /* ── 打开 evdev 键盘 ── */
    struct evdev_kbd *kbd = evdev_kbd_open();
    if (!kbd) {
        __fprintf(2, "fb_evdev_kbd: no keyboard found via evdev\n");
        goto cleanup;
    }

    int kbd_fd = evdev_kbd_fd(kbd);
    const char *dev_name = evdev_kbd_name(kbd);
    if (!dev_name || !dev_name[0]) dev_name = "(unknown)";

    /* ── 全黑清屏 ── */
    fb_fill_rect(fbp, 0, 0, w, h, COL_BLACK, ll);

    /* ── 屏幕中心坐标 ── */
    int cx = w / 2;

    /* ── 布局：各行 Y 坐标 ──
     *   ┌────────────────────────────────┐
     *   │  Title (static)               │  y_title
     *   │  Device: xxx (static)         │  y_devline
     *   ├────────────────────────────────┤  y_sep1
     *   │  History: (updating)          │  y_history
     *   ├────────────────────────────────┤  y_sep2
     *   │  Key: <name>   (updating)     │  ← centered
     *   │  Code: 0xNN (NNN)             │
     *   ├────────────────────────────────┤
     *   │  Modifiers: ... (updating)    │  y_mods
     *   │  Footer (static)              │  y_footer
     *   └────────────────────────────────┘
     * ── */
    int y_title   = MARGIN_TOP;
    int y_devline = y_title + LINE_H + 2;
    int y_sep1    = y_devline + LINE_H + 4;

    /* 历史记录标签 */
    int y_history_label = y_sep1 + 4;
    int y_history_text  = y_history_label + LINE_H + 2;

    /* 分隔线2（历史下方） */
    int y_sep2    = y_history_text + LINE_H + 2;

    /* 底部固定区域 */
    int y_footer  = h - GLYPH_H - MARGIN_TOP;              /* 最底行 */
    int y_mods    = y_footer - LINE_H - 6;                  /* 修饰键行（紧贴底部之上） */

    /* 主显示区（Key / Code）居中于 sep2 和 mods 之间 */
    int center_y  = (y_sep2 + y_mods) / 2;
    int y_key_lbl = center_y - LINE_H - 4;                  /* "Key:" */
    int y_key_val = y_key_lbl;                              /* <按键名> 与 Key: 同行 */
    int y_code    = center_y + GLYPH_H / 2 + 6;             /* "Code: 0xNN" 在键名下方 */

    /* ── 绘制静态 UI（不会变化的部分） ── */

    /* 标题 — 居中 */
    {
        const char *title = "fb_evdev_kbd — Keyboard Tester";
        int tx = cx - text_width(title) / 2;
        fb_draw_string_scaled(fbp, tx, y_title, title, COL_CYAN, ll, FONT_SCALE);
    }

    /* 设备名 — 居中 */
    {
        char devbuf[128];
        strcpy(devbuf, "Device: ");
        strcat(devbuf, dev_name);
        int tx = cx - text_width(devbuf) / 2;
        fb_draw_string_scaled(fbp, tx, y_devline, devbuf, COL_GRAY, ll, FONT_SCALE);
    }

    /* 分隔线1 */
    fb_draw_line(fbp, MARGIN_X, y_sep1, w - MARGIN_X, y_sep1, COL_GRAY_DIM, ll);

    /* 历史记录标签（静态）— 内容在循环中更新 */
    fb_draw_string_scaled(fbp, MARGIN_X, y_history_label, "History:", COL_GRAY, ll, FONT_SCALE);

    /* 分隔线2（历史内容下方，循环中维护，此处先画一条） */
    fb_draw_line(fbp, MARGIN_X, y_sep2, w - MARGIN_X, y_sep2, COL_GRAY_DIM, ll);

    /* 底部提示（仅绘一次） */
    {
        const char *footer = "Press ESC to quit";
        int fx = cx - text_width(footer) / 2;
        fb_draw_string_scaled(fbp, fx, y_footer, footer, COL_GRAY, ll, FONT_SCALE);
    }

    /* ── 主循环缓冲 ── */
    char last_key_str[24] = {0};
    int  last_key_code   = 0;
    char last_mod_str[64] = {0};
    char last_code_str[12] = {0};
    char last_hist_str[128] = {0};
    int  last_hist_len    = 0;

    /* ── 帧率控制（60fps） ── */
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

        /* ── poll evdev ── */
        struct pollfd pfd = {kbd_fd, POLLIN, 0};
        int pr = __poll(&pfd, 1, 0);
        if (pr < 0) break;

        int key_pressed = 0;
        int key_code    = 0;

        while (1) {
            int type;
            int code = evdev_kbd_read(kbd, &type);
            if (code == 0) break;

            update_mod(code, type == EVDEV_PRESS);

            if (type == EVDEV_PRESS) {
                key_pressed = 1;
                key_code    = code;
                if (code == KEY_ESC) { running = 0; break; }
            }
        }
        if (!running) break;

        /* ── 检查是否需要更新显示 ── */
        int need_redraw = 0;

        /* 当前修饰键字符串 */
        char mod_str[64] = {0};
        format_mods(mod_str, sizeof(mod_str));

        if (strcmp(mod_str, last_mod_str) != 0) {
            strncpy(last_mod_str, mod_str, sizeof(last_mod_str) - 1);
            need_redraw = 1;
        }

        if (key_pressed) {
            const char *name = key_name(key_code);
            strncpy(last_key_str, name, sizeof(last_key_str) - 1);
            last_key_str[sizeof(last_key_str) - 1] = '\0';
            last_key_code = key_code;

            /* 格式化 hex 码 */
            char h[12];
            strcpy(h, "0x");
            int hi = (key_code >> 4) & 0xF;
            int lo = key_code & 0xF;
            h[2] = (hi < 10) ? ('0' + hi) : ('a' + hi - 10);
            h[3] = (lo < 10) ? ('0' + lo) : ('a' + lo - 10);
            h[4] = '\0';
            strncpy(last_code_str, h, sizeof(last_code_str) - 1);

            /* 记录到历史 */
            push_history(key_code);

            need_redraw = 1;
        }

        if (!need_redraw) continue;

        /* ════════════════════════════════════════════════
         * 重绘
         * ════════════════════════════════════════════════ */

        /* ── 历史记录 ── */
        {
            char hist_buf[128] = {0};
            format_history(hist_buf, sizeof(hist_buf));
            if (strcmp(hist_buf, last_hist_str) != 0 ||
                (int)strlen(hist_buf) != last_hist_len) {
                strncpy(last_hist_str, hist_buf, sizeof(last_hist_str) - 1);
                last_hist_len = (int)strlen(hist_buf);

                fb_fill_rect(fbp, MARGIN_X, y_history_text, w - MARGIN_X * 2, LINE_H, COL_BLACK, ll);
                fb_draw_string_scaled(fbp, MARGIN_X, y_history_text, hist_buf, COL_WHITE, ll, FONT_SCALE);
            }
        }

        /* ── 主键显示区（分两块：键名区 + 修饰键行，独立清除防重叠） ── */

        /* 键名/编码区（Key / Code）— 完全擦除后重绘 */
        fb_fill_rect(fbp, MARGIN_X, y_sep2 + 2, w - MARGIN_X * 2,
                     y_mods - y_sep2 - 6, COL_BLACK, ll);

        /* "Key:  <按键名>" */
        {
            const char *label = "Key:";
            char keybuf[32];
            strcpy(keybuf, label);
            strcat(keybuf, "  ");
            strcat(keybuf, last_key_str);

            int tx = cx - text_width(keybuf) / 2;
            fb_draw_string_scaled(fbp, tx, y_key_lbl, label, COL_YELLOW, ll, FONT_SCALE);
            fb_draw_string_scaled(fbp, tx + fb_string_width_scaled(label, FONT_SCALE) + 4,
                                  y_key_lbl, last_key_str, COL_WHITE, ll, FONT_SCALE);
        }

        /* "Code:  0xNN  (NNN)" — 显示 hex + decimal */
        {
            char cbuf[48];
            strcpy(cbuf, "Code:  ");
            strcat(cbuf, last_code_str);

            if (last_key_code > 0) {
                char dec[16];
                int val = last_key_code;
                char *p = dec;
                if (val >= 100) { *p++ = '0' + val / 100; val %= 100; }
                if (val >= 10)  { *p++ = '0' + val / 10;  val %= 10;  }
                *p++ = '0' + val;
                *p = '\0';
                strcat(cbuf, "  (");
                strcat(cbuf, dec);
                strcat(cbuf, ")");
            }

            int tx = cx - text_width(cbuf) / 2;
            fb_draw_string_scaled(fbp, tx, y_code, cbuf, COL_CYAN, ll, FONT_SCALE);
        }

        /* ── Modifiers — 独立清除防重叠 ── */
        {
            fb_fill_rect(fbp, MARGIN_X, y_mods - 2, w - MARGIN_X * 2, LINE_H + 4, COL_BLACK, ll);
            char mbuf[96];
            strcpy(mbuf, "Modifiers:  ");
            strcat(mbuf, last_mod_str);

            int tx = cx - text_width(mbuf) / 2;
            uint32_t mcolor = (strcmp(last_mod_str, "(none)") != 0)
                              ? COL_GREEN : COL_GRAY;
            fb_draw_string_scaled(fbp, tx, y_mods, mbuf, mcolor, ll, FONT_SCALE);
        }
    }

    /* ════════════════════════════════════════════════
     * 清理退出
     * ════════════════════════════════════════════════ */

    if (graphics_mode) {
        { int km = KD_TEXT; __ioctl(0, KDSETMODE, &km); }

        /* 清空 stdin 缓冲区，防止残留字符被 bash 读取 */
        {
            struct pollfd spfd = {0, POLLIN, 0};
            while (__poll(&spfd, 1, 0) > 0) {
                char discard[64];
                if (__read(0, discard, sizeof(discard)) <= 0) break;
            }
        }

        tlibc_restore_term(0);
    }

    if (kbd) evdev_kbd_close(kbd);

    {
        int saved_fb = fb_fd;
        unsigned char *saved_fbp = fbp;
        size_t saved_ss = screensize;
        fb_fd = -1; fbp = 0;  /* 防止第二次清理 */
        __munmap(saved_fbp, saved_ss);
        __close(saved_fb);
    }

    __printf("\nfb_evdev_kbd: exit\n");
    return 0;

cleanup:
    if (graphics_mode) {
        { int km = KD_TEXT; __ioctl(0, KDSETMODE, &km); }
        tlibc_restore_term(0);
    }
    __munmap(fbp, screensize);
    __close(fb_fd);
    return 1;
}
