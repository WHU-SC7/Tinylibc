/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * fb_evdev_mouse — TTY 图形模式下 evdev 鼠标光标演示
 *
 * 机制：
 *   1. KD_GRAPHICS 模式下打开 /dev/fb0 帧缓冲
 *   2. 通过 evdev_mouse 库直接从 /dev/input/event* 读取鼠标事件
 *   3. 同时通过 evdev_kbd 库读取键盘，支持 Esc 退出
 *   4. 在帧缓冲上绘制可移动的十字准星光标
 *   5. 保存/恢复光标下的背景像素，避免拖尾
 *   6. 实时显示坐标、按键状态、滚轮信息
 *
 * 系统调用：openat, ioctl(FBIOGET_VSCREENINFO/FSCREENINFO/KDSETMODE),
 *          mmap, munmap, close, poll, clock_gettime, clock_nanosleep
 *
 * 依赖：evdev_mouse (lib/evdev_mouse.c), evdev_kbd (lib/evdev_kbd.c),
 *       fb_font (lib/graphics/fb_font.c), fb_draw (lib/graphics/fb_draw.c)
 *
 * 用法：
 *   fb_evdev_mouse
 *   移动鼠标 → 十字光标跟随
 *   点击鼠标 → 光标变色（左键=橙，右键=青，中键=绿）
 *   滚轮     → 状态栏显示滚动
 *   键盘 Esc → 退出
 *
 * 索引：
 *   main                   入口 → 打开 fb → mmap → KD_GRAPHICS →
 *                          打开 evdev 鼠标+键盘 → poll 循环 →
 *                          cursor_update → Esc 退出
 *     cursor_save_bg       保存光标区域背景像素
 *     cursor_restore_bg    恢复光标区域背景像素
 *     cursor_draw          绘制十字准星光标
 *     cursor_erase         擦除光标（恢复背景）
 *     draw_separator       绘制分隔线
 */

#include "tlibc_everything.h"
#include "linux_input.h"
#include "linux_fb.h"
#include "fb_draw.h"
#include "fb_font.h"
#include "evdev_mouse.h"
#include "evdev_kbd.h"

/* ═══════════════════════════════════════════════════
 * 颜色
 * ═══════════════════════════════════════════════════ */

#define COL_BLACK      0x000000
#define COL_WHITE      0xFFFFFF
#define COL_GRAY       0x888888
#define COL_GRAY_DIM   0x444444
#define COL_GRAY_DARK  0x222222
#define COL_GREEN      0x44FF44
#define COL_YELLOW     0xFFFF44
#define COL_RED        0xFF4444
#define COL_CYAN       0x44FFFF
#define COL_ORANGE     0xFF8800
#define COL_PINK       0xFF44FF

/* ═══════════════════════════════════════════════════
 * TTY 图形模式常量
 * ═══════════════════════════════════════════════════ */

#define KDSETMODE     0x4B3A
#define KD_TEXT       0x00
#define KD_GRAPHICS   0x01

/* ═══════════════════════════════════════════════════
 * 光标常量
 * ═══════════════════════════════════════════════════ */

/* 十字准星半径（从中心到臂端），含 gap → 总直径 = RADIUS*2+1 */
#define CURSOR_RADIUS  12
#define CURSOR_DIAM    (CURSOR_RADIUS * 2 + 1)
#define CURSOR_GAP     3   /* 中心 gap 半宽：不绘制 dx/dy ∈ [-GAP+1, GAP-1] */
#define CURSOR_BG_SIZE (CURSOR_DIAM * CURSOR_DIAM)  /* 保存背景像素数 */

/* ═══════════════════════════════════════════════════
 * 显存/屏幕常量
 * ═══════════════════════════════════════════════════ */

static int g_w, g_h, g_ll;
static unsigned char *g_fbp = 0;

/* ═══════════════════════════════════════════════════
 * 光标状态
 * ═══════════════════════════════════════════════════ */

static int g_cx = -1;           /* 当前光标 X（-1 = 未初始化） */
static int g_cy = -1;           /* 当前光标 Y */
static uint32_t g_cursor_bg[CURSOR_BG_SIZE];  /* 光标下背景缓存 */
static int g_old_cx = -1;       /* 上一帧光标 X */
static int g_old_cy = -1;       /* 上一帧光标 Y */
static int g_buttons = 0;       /* 当前鼠标按键状态 */

/* 滚轮事件环形缓冲，记录最近 N 次滚动方向 */
#define WHEEL_HIST 6
static int g_wheel_hist[WHEEL_HIST];  /* +1=上, -1=下 */
static int g_wheel_pos;               /* 下次写入位置 */

static int g_frame = 0;         /* 帧计数器，用于光标呼吸效果 */

/* ═══════════════════════════════════════════════════
 * 布局常量（1× 字体，8×16 + 1px 间距 = 9×18 pitch）
 * ═══════════════════════════════════════════════════ */

#define MARGIN_X    16
#define MARGIN_Y    8
#define FONT_PITCH  18      /* 行间距（像素） */
#define FONT_LINE_H 16      /* 字体高度 */
#define SEP_H       4       /* 分隔线上下留白 */
#define SEP_LINE_W  1       /* 分隔线粗细 */

/* 顶部区域 */
#define Y_TITLE     MARGIN_Y
#define Y_DEVICE    (Y_TITLE + FONT_PITCH)
#define Y_SEP1      (Y_DEVICE + FONT_PITCH + SEP_H * 2)

/* 底部区域（光标区下边界）— 需运行时从 g_h 计算 */
#define BOTTOM_PAD  (FONT_PITCH * 3 + SEP_H * 2 + 6)
static int g_y_sep2;       /* 底部分隔线 Y */
static int g_y_status;     /* 状态栏 Y */
static int g_y_footer;     /* 底部提示 Y */

/* ═══════════════════════════════════════════════════
 * 光标绘制工具
 * ═══════════════════════════════════════════════════ */

/* 保存光标周围背景像素（到 g_cursor_bg） */
static void cursor_save_bg(int cx, int cy)
{
    for (int dy = -CURSOR_RADIUS; dy <= CURSOR_RADIUS; dy++) {
        for (int dx = -CURSOR_RADIUS; dx <= CURSOR_RADIUS; dx++) {
            int px = cx + dx, py = cy + dy;
            int idx = (dy + CURSOR_RADIUS) * CURSOR_DIAM + (dx + CURSOR_RADIUS);
            if (px >= 0 && px < g_w && py >= 0 && py < g_h) {
                uint32_t *p = (uint32_t *)(g_fbp + py * g_ll + px * 4);
                g_cursor_bg[idx] = *p;
            } else {
                g_cursor_bg[idx] = COL_BLACK;
            }
        }
    }
}

/* 恢复光标周围背景像素 */
static void cursor_restore_bg(int cx, int cy)
{
    for (int dy = -CURSOR_RADIUS; dy <= CURSOR_RADIUS; dy++) {
        for (int dx = -CURSOR_RADIUS; dx <= CURSOR_RADIUS; dx++) {
            int px = cx + dx, py = cy + dy;
            if (px >= 0 && px < g_w && py >= 0 && py < g_h) {
                int idx = (dy + CURSOR_RADIUS) * CURSOR_DIAM + (dx + CURSOR_RADIUS);
                uint32_t *p = (uint32_t *)(g_fbp + py * g_ll + px * 4);
                *p = g_cursor_bg[idx];
            }
        }
    }
}

/* 获取当前光标颜色（基于按键状态和帧计数） */
static uint32_t cursor_color(void)
{
    /* 多个按键按下 → 颜色混合 */
    if (g_buttons & 7) {
        int r = 0, gr = 0, b = 0;
        if (g_buttons & (1 << 0)) { r  += 0xFF; gr += 0x44; b  += 0x44; }  /* 左键 */
        if (g_buttons & (1 << 1)) { r  += 0x44; gr += 0xFF; b  += 0xFF; }  /* 右键 */
        if (g_buttons & (1 << 2)) { r  += 0x44; gr += 0xFF; b  += 0x44; }  /* 中键 */
        int n = ((g_buttons & 1) ? 1 : 0) + ((g_buttons & 2) ? 1 : 0) + ((g_buttons & 4) ? 1 : 0);
        if (n > 1) { r /= n; gr /= n; b /= n; }
        return (r << 16) | (gr << 8) | b;
    }

    /* 无按键：白色，带微微的呼吸脉动 */
    int pulse = (g_frame / 8) % 6;  /* 0-5 渐变 */
    int v = 0x88 + pulse * 0x18;    /* 136 → 208 */
    return (v << 16) | (v << 8) | v;
}

/* 绘制十字准星光标 */
static void cursor_draw(int cx, int cy)
{
    uint32_t col = cursor_color();
    int min_gap = CURSOR_GAP - 1;

    /* 水平臂 */
    for (int dx = -CURSOR_RADIUS; dx <= CURSOR_RADIUS; dx++) {
        if (dx > -min_gap && dx < min_gap) continue;  /* 中心 gap */
        int px = cx + dx, py = cy;
        if (px >= 0 && px < g_w && py >= 0 && py < g_h) {
            uint32_t *p = (uint32_t *)(g_fbp + py * g_ll + px * 4);
            *p = col;
        }
    }

    /* 垂直臂 */
    for (int dy = -CURSOR_RADIUS; dy <= CURSOR_RADIUS; dy++) {
        if (dy > -min_gap && dy < min_gap) continue;  /* 中心 gap */
        int px = cx, py = cy + dy;
        if (px >= 0 && px < g_w && py >= 0 && py < g_h) {
            uint32_t *p = (uint32_t *)(g_fbp + py * g_ll + px * 4);
            *p = col;
        }
    }

    /* 中心小点（半透明效果：深灰） */
    if (cx >= 0 && cx < g_w && cy >= 0 && cy < g_h) {
        uint32_t *p = (uint32_t *)(g_fbp + cy * g_ll + cx * 4);
        *p = 0x666666;
    }

    /* 无外框微光（保持 save 区域 = ±RADIUS 一致，不留残影） */
}

/* 移动光标：擦除旧位置 → 保存新位置背景 → 绘制新位置 */
static void cursor_move_to(int new_x, int new_y)
{
    if (g_cx >= 0 && g_cy >= 0) {
        cursor_restore_bg(g_cx, g_cy);
    }

    g_cx = new_x;
    g_cy = new_y;
    cursor_save_bg(g_cx, g_cy);
    cursor_draw(g_cx, g_cy);
    g_old_cx = g_cx;
    g_old_cy = g_cy;
}

/* ═══════════════════════════════════════════════════
 * UI 绘制工具
 * ═══════════════════════════════════════════════════ */

/* 绘制分隔线（避开光标区域） */
static void draw_separator(int y)
{
    fb_draw_line(g_fbp, MARGIN_X, y, g_w - MARGIN_X, y, COL_GRAY_DIM, g_ll);
}

/* 初始化静态 UI（标题、设备名、分隔线） */
static void draw_static_ui(const char *dev_name)
{
    int cx = g_w / 2;

    /* 标题 */
    {
        const char *title = "fb_evdev_mouse — Mouse Tester";
        int tw = fb_string_width(title);
        int tx = cx - tw / 2;
        fb_draw_string(g_fbp, tx, Y_TITLE, title, COL_CYAN, g_ll);
    }

    /* 设备名 */
    {
        char devbuf[128];
        strcpy(devbuf, "Device: ");
        strcat(devbuf, dev_name);
        int tw = fb_string_width(devbuf);
        int tx = cx - tw / 2;
        fb_draw_string(g_fbp, tx, Y_DEVICE, devbuf, COL_GRAY, g_ll);
    }

    /* 分隔线 */
    draw_separator(Y_SEP1);
    draw_separator(g_y_sep2);

    /* 底部状态标签（静态部分） */
    {
        const char *footer = "Press ESC (keyboard) to quit";
        int tw = fb_string_width(footer);
        int tx = g_w - MARGIN_X - tw;
        fb_draw_string(g_fbp, tx, g_y_footer, footer, COL_GRAY_DIM, g_ll);
    }
}

/* 更新底部状态栏 */
static void draw_status(int x, int y, int buttons,
                        const int *wheel_hist, int wheel_pos)
{
    char buf[128];
    char wheel_str[WHEEL_HIST + 1];
    int wi = 0;

    /* 格式化滚轮历史：最新在右 */
    for (int i = 0; i < WHEEL_HIST; i++) {
        int v = wheel_hist[(wheel_pos + i) % WHEEL_HIST];
        if (v == 0) continue;          /* 跳过空槽 */
        wheel_str[wi++] = (v > 0) ? '+' : '-';
    }
    wheel_str[wi] = '\0';
    if (wi == 0) wheel_str[0] = '-', wheel_str[1] = '\0';

    snprintf(buf, sizeof(buf),
        "X: %d  Y: %d  |  L[%c] R[%c] M[%c]  |  Wheel: %s",
        x, y,
        (buttons & 1) ? 'X' : ' ',
        (buttons & 2) ? 'X' : ' ',
        (buttons & 4) ? 'X' : ' ',
        wheel_str);

    fb_fill_rect(g_fbp, MARGIN_X, g_y_status, g_w - MARGIN_X * 2,
                 FONT_LINE_H + 2, COL_BLACK, g_ll);
    fb_draw_string(g_fbp, MARGIN_X, g_y_status, buf, COL_GREEN, g_ll);
}

#if 0
/* 保留：绘制简易坐标网格（虚线），增强空间感（暂不使用，保持背景纯净） */
static void draw_grid(void)
{
    for (int x = MARGIN_X; x < g_w - MARGIN_X; x += 64) {
        fb_draw_line(g_fbp, x, Y_SEP1 + 2, x, g_y_sep2 - 2, COL_GRAY_DARK, g_ll);
    }
    for (int y = Y_SEP1 + 2 + 32; y < g_y_sep2 - 2; y += 64) {
        fb_draw_line(g_fbp, MARGIN_X, y, g_w - MARGIN_X, y, COL_GRAY_DARK, g_ll);
    }
}
#endif

/* ═══════════════════════════════════════════════════
 * 入口
 * ═══════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* ── 打开 /dev/fb0 ── */
    int fb_fd = __openat(AT_FDCWD, "/dev/fb0", O_RDWR, 0);
    if (fb_fd < 0) {
        __printf("fb_evdev_mouse: cannot open /dev/fb0\n");
        return 1;
    }

    struct fb_var_screeninfo var;
    if (__ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0) {
        __printf("fb_evdev_mouse: FBIOGET_VSCREENINFO failed\n");
        __close(fb_fd);
        return 1;
    }

    struct fb_fix_screeninfo fix;
    if (__ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        __printf("fb_evdev_mouse: FBIOGET_FSCREENINFO failed\n");
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
        __printf("fb_evdev_mouse: mmap failed\n");
        __close(fb_fd);
        return 1;
    }

    /* ── KD_GRAPHICS 模式 ── */
    int graphics_mode = 0;
    tlibc_set_term_raw_and_noecho(0);
    graphics_mode = 1;
    { int km = KD_GRAPHICS; __ioctl(0, KDSETMODE, &km); }

    /* ── 初始化布局坐标（在 g_h 就绪后计算） ── */
    g_y_sep2   = g_h - BOTTOM_PAD;
    g_y_status = g_y_sep2 + SEP_H * 2;
    g_y_footer = g_y_status + FONT_PITCH;

    /* ── 全黑清屏 ── */
    fb_fill_rect(g_fbp, 0, 0, g_w, g_h, COL_BLACK, g_ll);

    /* ── 打开 evdev 鼠标 ── */
    struct evdev_mouse *mouse = evdev_mouse_open(g_w, g_h);
    if (!mouse) {
        __fprintf(2, "fb_evdev_mouse: no mouse found via evdev\n");
        goto cleanup;
    }
    int mouse_fd = evdev_mouse_fd(mouse);
    const char *dev_name = evdev_mouse_name(mouse);
    if (!dev_name || !dev_name[0]) dev_name = "(unknown)";

    /* ── 打开 evdev 键盘 ── */
    struct evdev_kbd *kbd = evdev_kbd_open();
    if (!kbd) {
        __fprintf(2, "fb_evdev_mouse: no keyboard found via evdev\n");
        evdev_mouse_close(mouse);
        goto cleanup;
    }
    int kbd_fd = evdev_kbd_fd(kbd);

    /* ── 绘制静态 UI ── */
    draw_static_ui(dev_name);

    /* ── 光标初始位置：光标区中心 ── */
    int start_x = g_w / 2;
    int start_y = (Y_SEP1 + g_y_sep2) / 2;
    cursor_move_to(start_x, start_y);

    /* ── 帧率控制（120fps 鼠标轮询） ── */
    long interval_ns = 1000000000L / 120;
    struct timespec next_frame;
    __clock_gettime(CLOCK_MONOTONIC, &next_frame);

    /* ── 主循环 ── */
    int running = 1;
    struct pollfd pfds[2];

    pfds[0].fd     = mouse_fd;
    pfds[0].events = POLLIN;
    pfds[1].fd     = kbd_fd;
    pfds[1].events = POLLIN;

    while (running) {
        __clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame, NULL);
        next_frame.tv_nsec += interval_ns;
        if (next_frame.tv_nsec >= 1000000000L) {
            next_frame.tv_sec += next_frame.tv_nsec / 1000000000L;
            next_frame.tv_nsec %= 1000000000L;
        }

        /* ── poll 鼠标 + 键盘 ── */
        int pr = __poll(pfds, 2, 0);
        if (pr < 0) break;

        int mouse_updated = 0;

        /* ── 处理鼠标事件 ── */
        if (pfds[0].revents & POLLIN) {
            struct evdev_mouse_state st;
            if (evdev_mouse_read(mouse, &st)) {
                if (st.updated) {
                    /* 有新坐标时移动光标 */
                    if (st.x != g_cx || st.y != g_cy) {
                        cursor_move_to(st.x, st.y);
                    }

                    /* 更新按钮状态（先保存旧值，用于下文的变色检测） */
                    int old_buttons = g_buttons;
                    g_buttons = st.buttons;

                    /* 滚轮事件入环形缓冲 */
                    if (st.wheel != 0) {
                        int dir = (st.wheel > 0) ? 1 : -1;
                        g_wheel_hist[g_wheel_pos] = dir;
                        g_wheel_pos = (g_wheel_pos + 1) % WHEEL_HIST;
                    }
                    mouse_updated = 1;

                    /* 位置没变但按钮/滚轮变了 → 原地重绘光标（颜色可能改变） */
                    if (st.x == g_cx && st.y == g_cy &&
                        (old_buttons != st.buttons || st.wheel != 0)) {
                        /* 在当前位置重新保存背景（光标可能变色） */
                        cursor_restore_bg(g_cx, g_cy);
                        cursor_save_bg(g_cx, g_cy);
                        cursor_draw(g_cx, g_cy);
                    }
                }
            }
        }

        /* ── 处理键盘事件 ── */
        if (pfds[1].revents & POLLIN) {
            int type;
            while (1) {
                int key = evdev_kbd_read(kbd, &type);
                if (key == 0) break;
                if (key == KEY_ESC && type == EVDEV_PRESS) {
                    running = 0;
                }
            }
        }

        /* ── 更新状态栏 ── */
        /* 每 4 帧至少刷一次状态栏（即使坐标没变，帧计数需要更新呼吸效果） */
        g_frame++;
        if (mouse_updated || (g_frame & 3) == 0) {
            draw_status(g_cx, g_cy, g_buttons,
                        g_wheel_hist, g_wheel_pos);

            /* 如果位置没变但颜色需更新（呼吸），重绘光标 */
            if (!mouse_updated && (g_frame & 7) == 0) {
                cursor_restore_bg(g_cx, g_cy);
                cursor_save_bg(g_cx, g_cy);
                cursor_draw(g_cx, g_cy);
            }
        }
    }

    /* ════════════════════════════════════════════════
     * 清理退出
     * ════════════════════════════════════════════════ */

    if (graphics_mode) {
        { int km = KD_TEXT; __ioctl(0, KDSETMODE, &km); }

        /* 清空 stdin 缓冲区 */
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
    if (mouse) evdev_mouse_close(mouse);

    {
        int saved_fd = fb_fd;
        unsigned char *saved_fbp = g_fbp;
        size_t saved_ss = screensize;
        fb_fd = -1; g_fbp = 0;
        __munmap(saved_fbp, saved_ss);
        __close(saved_fd);
    }

    __printf("\nfb_evdev_mouse: exit\n");
    return 0;

cleanup:
    if (graphics_mode) {
        { int km = KD_TEXT; __ioctl(0, KDSETMODE, &km); }
        tlibc_restore_term(0);
    }
    __munmap(g_fbp, screensize);
    __close(fb_fd);
    return 1;
}
