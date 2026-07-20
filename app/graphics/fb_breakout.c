/*
 * fb_breakout — 帧缓冲打砖块游戏
 *
 * 机制：evdev 鼠标/键盘控制挡板，反弹小球消除砖块。
 *       固定点物理步长、脏矩形绘制、clock_nanosleep 绝对帧同步。
 * 系统调用：openat, ioctl, mmap, munmap, close, poll, clock_gettime, clock_nanosleep
 *
 * 用法：
 *   fb_breakout              # 默认 3 条命，球速 350px/s
 *   fb_breakout 5            # 5 条命（练习模式）
 *   fb_breakout 3 500        # 3 条命，球速 500px/s（困难）
 *
 * 操作：
 *   鼠标移动 / ← →  控制挡板
 *   左键/空格         发球 / 重新开始
 *   Esc               退出
 */

/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * 索引：
 *   main              入口
 *     init_game       初始化砖块布局和游戏状态
 *     handle_input    统一处理鼠标 + 键盘事件
 *     update_game     物理更新 + 碰撞 + 状态迁移
 *     render_game     脏矩形绘制 + 消息屏幕
 *   ball_vs_rect      球 vs 矩形碰撞检测（AABB 最小穿透轴）
 *   draw_bricks       全量绘制砖块阵列
 *   draw_panel        右侧信息面板（分数/生命/操作说明）
 *   draw_message      居中消息（GAME OVER / YOU WIN）
 */

#include "tlibc_everything.h"  /* core + compat */
#include "fb_draw.h"
#include "fb_font.h"
#include "evdev_mouse.h"
#include "evdev_kbd.h"
#include "linux_fb.h"
#include "linux_input.h"

/* ── TTY 图形模式 ── */
#define KDSETMODE      0x4B3A
#define KD_TEXT        0x00
#define KD_GRAPHICS    0x01

/* ── 游戏参数 ── */
#define BALL_R         4
#define BALL_SPEED     350
#define PADDLE_W       80
#define PADDLE_H       10
#define PADDLE_BOTTOM  30         /* 距游戏区底边 */
#define BRICK_H        12
#define BRICK_GAP      2
#define BRICK_ROWS     3
#define BRICK_COLS     4
#define MAX_LIVES      3
#define TARGET_FPS     60

/* ── 右侧面板 ── */
#define PANEL_W        220

/* ── 像素颜色（XRGB8888，B 在最低 8 位） ── */
#define COL_BG         0x0A0A1A   /* 背景 */
#define COL_GAME_BG    0x070712   /* 游戏区背景 */
#define COL_PANEL_BG   0x14142E   /* 面板背景 */
#define COL_WHITE      0xFFFFFF
#define COL_RED        0xFF4444
#define COL_ORANGE     0xFF8844
#define COL_YELLOW     0xDDDD44
#define COL_GREEN      0x44DD44
#define COL_BLUE       0x4488FF
#define COL_PURPLE     0xCC44FF
#define COL_PADDLE     0x66DDFF
#define COL_BALL_FILL  0x88CCFF
#define COL_SEPARATOR  0x2A2A50   /* 分割线 */
#define COL_HUD_BG     0x161630   /* 消息面板背景 */

/* ── 砖块行属性 ── */
static const uint32_t ROW_COLOR[BRICK_ROWS] = {
    COL_RED, COL_ORANGE, COL_YELLOW
};
static const int ROW_POINTS[BRICK_ROWS] = { 30, 20, 10 };

/* ── 游戏阶段 ── */
enum { PHASE_SERVE, PHASE_PLAY, PHASE_LOST, PHASE_WIN };

/* ── 砖块 ── */
typedef struct { int x, y, w, h; int alive; } Brick;

/* ── 游戏状态 ── */
typedef struct {
    /* 帧缓冲 */
    unsigned char *fbp;
    int scr_w, scr_h, ll;

    /* 游戏区（世界坐标，通过 view_x/y 偏移到屏幕） */
    int game_w, game_h;
    int view_x, view_y;

    /* 输入 */
    struct evdev_mouse *mouse;
    struct evdev_kbd   *kbd;
    int have_kbd;

    /* 球 */
    int bx, by, r;
    int vx, vy;
    long ax, ay;

    /* 挡板 */
    int px, py, pw, ph;

    /* 砖块 */
    Brick bricks[BRICK_ROWS * BRICK_COLS];
    int brick_total;

    /* 分数 / 生命 */
    int score;
    int lives;

    /* 阶段 */
    int phase;

    /* 上一帧脏数据 */
    int old_bx, old_by;
    int old_px;
    int old_score, old_lives;
} Game;

/* ── utils ── */

static char *utoa_simple(unsigned val, char *buf, int size)
{
    char tmp[16];
    int tp = 0, bp = 0;
    if (val == 0) tmp[tp++] = '0';
    while (val) { tmp[tp++] = '0' + (val % 10); val /= 10; }
    while (tp > 0 && bp < size - 1) buf[bp++] = tmp[--tp];
    buf[bp] = '\0';
    return buf;
}

static void ts_add_ns(struct timespec *ts, long ns)
{
    ts->tv_nsec += ns;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec  += ts->tv_nsec / 1000000000L;
        ts->tv_nsec %= 1000000000L;
    }
}

static long ts_diff_us(const struct timespec *a, const struct timespec *b)
{
    return (a->tv_sec - b->tv_sec) * 1000000L
         + (a->tv_nsec - b->tv_nsec) / 1000L;
}

static int iabs(int x) { return x < 0 ? -x : x; }

/* ── 碰撞检测：球（圆形）vs 矩形（AABB）── */
static int ball_vs_rect(int *bx, int *by, int r,
                        int rx, int ry, int rw, int rh,
                        int *vx, int *vy)
{
    int bl = *bx - r, br = *bx + r;
    int bt = *by - r, bb = *by + r;

    if (br <= rx || bl >= rx + rw) return 0;
    if (bb <= ry || bt >= ry + rh) return 0;

    int overlap_l  = br - rx;
    int overlap_r  = (rx + rw) - bl;
    int overlap_t  = bb - ry;
    int overlap_b  = (ry + rh) - bt;

    int min_h = overlap_l < overlap_r ? overlap_l : overlap_r;
    int min_v = overlap_t < overlap_b ? overlap_t : overlap_b;

    if (min_h < min_v) {
        *vx = -*vx;
        if (overlap_l < overlap_r) *bx = rx - r - 1;
        else                       *bx = rx + rw + r + 1;
        return 'h';
    } else {
        *vy = -*vy;
        if (overlap_t < overlap_b) *by = ry - r - 1;
        else                       *by = ry + rh + r + 1;
        return 'v';
    }
}

/* ── 初始化砖块布局 ── */
static void init_bricks(Game *g)
{
    g->brick_total = BRICK_ROWS * BRICK_COLS;
    int bw = (g->game_w - 2 * 10 - (BRICK_COLS - 1) * BRICK_GAP) / BRICK_COLS;
    if (bw < 8) bw = 8;
    int used = BRICK_COLS * bw + (BRICK_COLS - 1) * BRICK_GAP;
    int start_x = (g->game_w - used) / 2;
    int start_y = 20;

    int idx = 0;
    for (int row = 0; row < BRICK_ROWS; row++) {
        for (int col = 0; col < BRICK_COLS; col++) {
            g->bricks[idx].alive = 1;
            g->bricks[idx].x = start_x + col * (bw + BRICK_GAP);
            g->bricks[idx].y = start_y + row * (BRICK_H + BRICK_GAP);
            g->bricks[idx].w = bw;
            g->bricks[idx].h = BRICK_H;
            idx++;
        }
    }
}

static void reset_ball(Game *g)
{
    g->bx = g->px + g->pw / 2;
    g->by = g->py - g->r - 1;
    g->vx = g->vy = 0;
    g->ax = g->ay = 0;
}

static void launch_ball(Game *g)
{
    int dir = (g->bx < g->game_w / 2) ? 1 : -1;
    g->vx = 130 * dir;
    g->vy = -(BALL_SPEED + 30);
    g->ax = g->ay = 0;
    g->phase = PHASE_PLAY;
}

/* ── 绘制所有存活砖块（游戏区坐标，自动偏移） ── */
static void draw_bricks(Game *g)
{
    for (int i = 0; i < g->brick_total; i++) {
        if (!g->bricks[i].alive) continue;
        int row = i / BRICK_COLS;
        fb_fill_rect(g->fbp,
                     g->view_x + g->bricks[i].x,
                     g->view_y + g->bricks[i].y,
                     g->bricks[i].w, g->bricks[i].h,
                     ROW_COLOR[row], g->ll);
    }
}

/* ── 绘制右侧信息面板 ── */
static void draw_panel(Game *g)
{
    int px = g->view_x + g->game_w + 20;
    int pw = PANEL_W;
    int py = g->view_y;
    int ph = g->game_h;
    char buf[32];

    /* 面板背景 */
    fb_fill_rect(g->fbp, px, py, pw, ph, COL_PANEL_BG, g->ll);

    /* 标题（2× 缩放） */
    fb_draw_string_scaled(g->fbp,
        px + (pw - fb_string_width_scaled("BREAKOUT", 2)) / 2,
        py + 30, "BREAKOUT", COL_WHITE, g->ll, 2);

    /* 分割线 */
    fb_fill_rect(g->fbp, px + 20, py + 78, pw - 40, 1, COL_SEPARATOR, g->ll);

    /* ── 分数 ── */
    int ly = py + 100;
    fb_draw_string_bg(g->fbp, px + 20, ly, "SCORE", 0x8888AA, COL_PANEL_BG, g->ll);
    utoa_simple(g->score, buf, sizeof(buf));
    fb_draw_string_scaled(g->fbp, px + 20, ly + 22, buf, COL_YELLOW, g->ll, 2);

    /* 分割线 */
    fb_fill_rect(g->fbp, px + 20, ly + 58, pw - 40, 1, COL_SEPARATOR, g->ll);

    /* ── 生命 ── */
    int ly2 = ly + 78;
    fb_draw_string_bg(g->fbp, px + 20, ly2, "LIVES", 0x8888AA, COL_PANEL_BG, g->ll);
    for (int i = 0; i < g->lives; i++)
        fb_fill_circle(g->fbp, px + 24 + i * 24, ly2 + 28, 6, COL_RED, g->ll);

    /* 分割线 */
    fb_fill_rect(g->fbp, px + 20, ly2 + 50, pw - 40, 1, COL_SEPARATOR, g->ll);

    /* ── 操作说明 ── */
    int ly3 = ly2 + 68;
    fb_draw_string_bg(g->fbp, px + 20, ly3, "CONTROLS", 0x666688, COL_PANEL_BG, g->ll);
    fb_draw_string_bg(g->fbp, px + 20, ly3 + 22, "MOUSE  /  <- ->", 0x888888, COL_PANEL_BG, g->ll);
    fb_draw_string_bg(g->fbp, px + 20, ly3 + 40, "LCLICK / SPACE", 0x888888, COL_PANEL_BG, g->ll);
    fb_draw_string_bg(g->fbp, px + 20, ly3 + 58, "ESC  QUIT", 0x888888, COL_PANEL_BG, g->ll);

    g->old_score = g->score;
    g->old_lives = g->lives;
}

/* ── 绘制居中消息（游戏区居中）── */
static void draw_message(Game *g, const char *title,
                         const char *sub, const char *hint,
                         uint32_t title_color)
{
    int cx = g->view_x + g->game_w / 2;
    int cy = g->view_y + g->game_h / 2;

    fb_fill_rect(g->fbp, cx - 180, cy - 50, 360, 110, COL_HUD_BG, g->ll);
    fb_draw_rect(g->fbp, cx - 180, cy - 50, 360, 110, title_color, g->ll);

    fb_draw_string_bg(g->fbp, cx - fb_string_width(title) / 2, cy - 36,
                      title, title_color, COL_HUD_BG, g->ll);

    if (sub)
        fb_draw_string_bg(g->fbp, cx - fb_string_width(sub) / 2, cy - 10,
                          sub, COL_WHITE, COL_HUD_BG, g->ll);

    if (hint)
        fb_draw_string_bg(g->fbp, cx - fb_string_width(hint) / 2, cy + 20,
                          hint, 0x888888, COL_HUD_BG, g->ll);
}

/* ── 初始化游戏 ── */
static void init_game(Game *g, int lives)
{
    /* 计算游戏区尺寸（世界坐标）：缩小适配低速 fbdev */
    g->game_w = (g->scr_w - PANEL_W - 60) / 3;
    if (g->game_w < 300) g->game_w = 300;
    g->game_h = g->game_w * 3 / 4;
    if (g->game_h < 200) g->game_h = 200;

    g->view_x = (g->scr_w - g->game_w - 20 - PANEL_W) / 2;
    g->view_y = (g->scr_h - g->game_h) / 2;

    /* 游戏对象 */
    g->score = 0;
    g->lives = lives;
    g->phase = PHASE_SERVE;
    g->r = BALL_R;
    g->pw = PADDLE_W;
    g->ph = PADDLE_H;
    g->py = g->game_h - PADDLE_BOTTOM;
    g->px = (g->game_w - g->pw) / 2;

    init_bricks(g);
    reset_ball(g);

    g->old_bx = g->bx;
    g->old_by = g->by;
    g->old_px = g->px;
    g->old_score = -1;
    g->old_lives = -1;
}

/* ════════════════════════════════════════════
 * 主循环：处理输入
 * ════════════════════════════════════════════ */
static void handle_input(Game *g, struct evdev_mouse_state *ms,
                         int *kb_left, int *kb_right, int *kb_space)
{
    /* 鼠标 → 游戏区坐标 */
    if (evdev_mouse_read(g->mouse, ms) && ms->updated) {
        g->px = (ms->x - g->view_x) - g->pw / 2;
    }

    /* 键盘 */
    if (g->have_kbd) {
        int key, type;
        while ((key = evdev_kbd_read(g->kbd, &type)) > 0) {
            if (key == KEY_ESC && type == EVDEV_PRESS) {
                g->px = -9999;
                return;
            }
            if (key == KEY_LEFT)  *kb_left  = (type == EVDEV_PRESS || type == EVDEV_REPEAT);
            if (key == KEY_RIGHT) *kb_right = (type == EVDEV_PRESS || type == EVDEV_REPEAT);
            if (key == KEY_SPACE && type == EVDEV_PRESS) *kb_space = 1;
        }
    }

    if (*kb_left)  g->px -= 4;
    if (*kb_right) g->px += 4;

    /* 挡板边界（游戏区） */
    if (g->px < 0) g->px = 0;
    if (g->px + g->pw > g->game_w) g->px = g->game_w - g->pw;
}

/* ════════════════════════════════════════════
 * 主循环：物理更新
 * ════════════════════════════════════════════ */
static void update_game(Game *g, long dt_us)
{
    if (g->phase == PHASE_SERVE) {
        g->bx = g->px + g->pw / 2;
        g->by = g->py - g->r - 1;
        return;
    }

    if (g->phase != PHASE_PLAY)
        return;

    /* ── 固定点物理 ── */
    g->ax += g->vx * dt_us;
    g->ay += g->vy * dt_us;
    g->bx += (int)(g->ax / 1000000);
    g->by += (int)(g->ay / 1000000);
    g->ax %= 1000000;
    g->ay %= 1000000;

    /* ── 墙壁碰撞（游戏区边界）── */
    if (g->by - g->r < 0) { g->by = g->r; g->vy = iabs(g->vy); }
    if (g->bx - g->r < 0) { g->bx = g->r; g->vx = iabs(g->vx); }
    if (g->bx + g->r >= g->game_w) {
        g->bx = g->game_w - g->r - 1;
        g->vx = -iabs(g->vx);
    }

    /* ── 挡板碰撞 ── */
    if (g->vy > 0) {
        int bl = g->bx - g->r, br = g->bx + g->r;
        int bt = g->by - g->r, bb = g->by + g->r;
        if (br > g->px && bl < g->px + g->pw
            && bb > g->py && bt < g->py + g->ph) {
            int offset = g->bx - (g->px + g->pw / 2);
            g->vy = -(BALL_SPEED + 30);
            g->vx = offset * 3;
            if (g->vx > 280) g->vx = 280;
            if (g->vx < -280) g->vx = -280;
            g->by = g->py - g->r - 1;
        }
    }

    /* ── 砖块碰撞 ── */
    for (int i = 0; i < g->brick_total; i++) {
        if (!g->bricks[i].alive) continue;
        int hit = ball_vs_rect(&g->bx, &g->by, g->r,
                               g->bricks[i].x, g->bricks[i].y,
                               g->bricks[i].w, g->bricks[i].h,
                               &g->vx, &g->vy);
        if (hit) {
            g->bricks[i].alive = 0;
            g->score += ROW_POINTS[i / BRICK_COLS];
            fb_fill_rect(g->fbp,
                         g->view_x + g->bricks[i].x,
                         g->view_y + g->bricks[i].y,
                         g->bricks[i].w, g->bricks[i].h,
                         COL_GAME_BG, g->ll);
            break;
        }
    }

    /* ── 掉出底边 ── */
    if (g->by - g->r > g->game_h + 60) {
        g->lives--;
        if (g->lives <= 0) { g->phase = PHASE_LOST; }
        else                { reset_ball(g); g->phase = PHASE_SERVE; }
        return;
    }

    /* ── 胜利判定 ── */
    int all_dead = 1;
    for (int i = 0; i < g->brick_total; i++)
        if (g->bricks[i].alive) { all_dead = 0; break; }
    if (all_dead) g->phase = PHASE_WIN;

    /* ── 速度限制 ── */
    int spd = iabs(g->vx) + iabs(g->vy);
    if (spd > 850) { g->vx = g->vx * 850 / spd; g->vy = g->vy * 850 / spd; }
    if (g->vy > -120 && g->vy < 0) g->vy = -120;
    if (g->vy > 0 && g->vy < 120)  g->vy = 120;
}

/* ════════════════════════════════════════════
 * 主循环：渲染
 * ════════════════════════════════════════════ */
static void render_game(Game *g)
{
    int vx = g->view_x, vy = g->view_y;

    /* ── 擦除旧球／挡板 ── */
    fb_fill_circle(g->fbp, vx + g->old_bx, vy + g->old_by, g->r + 1, COL_GAME_BG, g->ll);
    if (g->px != g->old_px)
        fb_fill_rect(g->fbp, vx + g->old_px, vy + g->py, g->pw, g->ph, COL_GAME_BG, g->ll);

    /* ── 绘制新球 ── */
    if (g->phase != PHASE_LOST && g->phase != PHASE_WIN) {
        fb_fill_circle(g->fbp, vx + g->bx, vy + g->by, g->r, COL_BALL_FILL, g->ll);
        fb_draw_circle(g->fbp, vx + g->bx, vy + g->by, g->r, COL_WHITE, g->ll);
    }

    /* ── 绘制挡板 ── */
    fb_fill_rect(g->fbp, vx + g->px, vy + g->py, g->pw, g->ph, COL_PADDLE, g->ll);
    fb_draw_rect(g->fbp, vx + g->px, vy + g->py, g->pw, g->ph, COL_WHITE, g->ll);

    /* ── 面板（变化时重绘）── */
    if (g->score != g->old_score || g->lives != g->old_lives)
        draw_panel(g);

    /* ── 消息 ── */
    if (g->phase == PHASE_LOST) {
        char buf[32], msg[64];
        utoa_simple(g->score, buf, sizeof(buf));
        snprintf(msg, sizeof(msg), "SCORE  %s", buf);
        draw_message(g, "GAME OVER", msg,
                     "SPACE TO RESTART   ESC TO QUIT", COL_RED);
    } else if (g->phase == PHASE_WIN) {
        char buf[32], msg[64];
        utoa_simple(g->score, buf, sizeof(buf));
        snprintf(msg, sizeof(msg), "FINAL SCORE  %s", buf);
        draw_message(g, "YOU WIN!", msg,
                     "SPACE TO PLAY AGAIN   ESC TO QUIT", COL_YELLOW);
    }

    g->old_bx = g->bx;
    g->old_by = g->by;
    g->old_px = g->px;
}

/* ════════════════════════════════════════════
 * 启动画面
 * ════════════════════════════════════════════ */
static void draw_title_screen(Game *g)
{
    /* 全屏背景 */
    fb_fill_rect(g->fbp, 0, 0, g->scr_w, g->scr_h, COL_BG, g->ll);

    /* 游戏区背景 + 边框 */
    fb_fill_rect(g->fbp, g->view_x, g->view_y, g->game_w, g->game_h,
                 COL_GAME_BG, g->ll);
    fb_draw_rect(g->fbp, g->view_x, g->view_y, g->game_w, g->game_h,
                 0x2A2A4A, g->ll);

    draw_bricks(g);
    fb_fill_rect(g->fbp, g->view_x + g->px, g->view_y + g->py,
                 g->pw, g->ph, COL_PADDLE, g->ll);
    fb_draw_rect(g->fbp, g->view_x + g->px, g->view_y + g->py,
                 g->pw, g->ph, COL_WHITE, g->ll);
    fb_fill_circle(g->fbp, g->view_x + g->bx, g->view_y + g->by,
                   g->r, COL_BALL_FILL, g->ll);
    fb_draw_circle(g->fbp, g->view_x + g->bx, g->view_y + g->by,
                   g->r, COL_WHITE, g->ll);

    draw_panel(g);

    /* "CLICK TO START" 居中于游戏区 */
    int cx = g->view_x + g->game_w / 2;
    int cy = g->view_y + g->game_h / 2;
    fb_fill_rect(g->fbp, cx - 160, cy - 16, 320, 32, COL_HUD_BG, g->ll);
    fb_draw_string_bg(g->fbp, cx - fb_string_width("CLICK TO START") / 2,
                      cy - 10, "CLICK TO START", COL_WHITE, COL_HUD_BG, g->ll);
}

/* ════════════════════════════════════════════
 * 主函数
 * ════════════════════════════════════════════ */
int main(int argc, char *argv[])
{
    int lives = MAX_LIVES;
    if (argc > 1) {
        int n = 0; const char *p = argv[1];
        while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
        if (n >= 1 && n <= 99) lives = n;
    }

    int fd = __openat(AT_FDCWD, "/dev/fb0", O_RDWR, 0);
    if (fd < 0) { __printf("fb_breakout: cannot open /dev/fb0 (%d)\n", fd); return 1; }

    struct fb_var_screeninfo var;
    if (__ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0)
        { __printf("fb_breakout: FBIOGET_VSCREENINFO failed\n"); __close(fd); return 1; }

    struct fb_fix_screeninfo fix;
    if (__ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0)
        { __printf("fb_breakout: FBIOGET_FSCREENINFO failed\n"); __close(fd); return 1; }

    int scr_w = var.xres, scr_h = var.yres;
    int ll = fix.line_length;
    size_t screensize = (size_t)var.yres_virtual * ll;

    unsigned char *fbp = __mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fbp == MAP_FAILED)
        { __printf("fb_breakout: mmap failed\n"); __close(fd); return 1; }

    /* ── 输入设备 ── */
    struct evdev_mouse *mouse = evdev_mouse_open(scr_w, scr_h);
    if (!mouse) { __printf("fb_breakout: no mouse found\n"); __munmap(fbp, screensize); __close(fd); return 1; }

    struct evdev_kbd *kbd = evdev_kbd_open();
    if (!kbd) __printf("fb_breakout: warning — no keyboard\n");

    /* ── 游戏状态 ── */
    Game game;
    memset(&game, 0, sizeof(game));
    game.fbp     = fbp;
    game.scr_w   = scr_w;
    game.scr_h   = scr_h;
    game.ll      = ll;
    game.mouse   = mouse;
    game.kbd     = kbd;
    game.have_kbd = (kbd != NULL);

    init_game(&game, lives);

    /* ── 图形模式 ── */
    int graphics_mode = 0;
    tlibc_set_term_raw_and_noecho(0);
    graphics_mode = 1;
    { int km = KD_GRAPHICS; __ioctl(0, KDSETMODE, &km); }

    __printf("fb_breakout: %u×%u  game %dx%d  %d lives\n",
             scr_w, scr_h, game.game_w, game.game_h, lives);

    draw_title_screen(&game);

    /* ── 帧计时 ── */
    long interval_ns = 1000000000L / TARGET_FPS;
    struct timespec t_start, t_last, next_frame;
    __clock_gettime(CLOCK_MONOTONIC, &t_start);
    t_last = next_frame = t_start;
    ts_add_ns(&next_frame, interval_ns);

    int running = 1;
    struct evdev_mouse_state ms = {0};
    int prev_buttons = 0;
    int kb_left = 0, kb_right = 0, kb_space = 0;

    /* ── 主循环 ── */
    while (running) {
        __clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame, NULL);

        struct timespec now;
        __clock_gettime(CLOCK_MONOTONIC, &now);
        ts_add_ns(&next_frame, interval_ns);

        long behind_us = ts_diff_us(&next_frame, &now);
        if (behind_us < 0) {
            behind_us = -behind_us;
            if ((unsigned long)behind_us > (unsigned long)(interval_ns / 1000))
                next_frame = now;
        }

        long dt_us = ts_diff_us(&now, &t_last);
        if (dt_us > 100000) dt_us = 100000;
        if (dt_us < 0) dt_us = 0;
        t_last = now;

        /* ── 输入 ── */
        {
            struct pollfd pfds[2];
            int npfd = 0;
            pfds[npfd].fd = evdev_mouse_fd(mouse);
            pfds[npfd].events = POLLIN; npfd++;
            if (kbd) { pfds[npfd].fd = evdev_kbd_fd(kbd); pfds[npfd].events = POLLIN; npfd++; }
            if (__poll(pfds, npfd, 0) > 0)
                handle_input(&game, &ms, &kb_left, &kb_right, &kb_space);
            if (game.px < -1000) { running = 0; break; }
        }

        int click = (ms.buttons & 1) && !(prev_buttons & 1);
        prev_buttons = ms.buttons;

        /* ── 游戏逻辑 ── */
        {
            if (game.phase == PHASE_SERVE) {
                if (click || kb_space) {
                    /* 清除 "CLICK TO START" 提示 */
                    int cx = game.view_x + game.game_w / 2;
                    int cy = game.view_y + game.game_h / 2;
                    fb_fill_rect(game.fbp, cx - 162, cy - 18, 324, 36,
                                 COL_GAME_BG, game.ll);
                    launch_ball(&game);
                    kb_space = 0;
                }
            }

            update_game(&game, dt_us);

            if (game.phase == PHASE_LOST || game.phase == PHASE_WIN) {
                if (click || kb_space) {
                    kb_space = 0;
                    init_game(&game, lives);
                    draw_title_screen(&game);
                    continue;
                }
            }
            kb_space = 0;
        }

        /* ── 渲染 ── */
        render_game(&game);

        if (now.tv_sec - t_start.tv_sec >= 600) {
            __printf("fb_breakout: timed out after 10 minutes\n");
            running = 0;
        }
    }

    /* ── 清理 ── */
    if (graphics_mode) { { int km = KD_TEXT; __ioctl(0, KDSETMODE, &km); } tlibc_restore_term(0); }
    if (kbd) evdev_kbd_close(kbd);
    evdev_mouse_close(mouse);
    __munmap(fbp, screensize);
    __close(fd);
    __printf("fb_breakout:  score %d\n", game.score);
    return 0;
}
