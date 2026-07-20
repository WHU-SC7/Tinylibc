/*
 * fb_invaders — 帧缓冲太空入侵者游戏
 *
 * 机制：evdev 鼠标/键盘控制飞船，射击并消灭入侵者波次。
 *       固定点物理步长、脏矩形绘制、clock_nanosleep 绝对帧同步。
 * 系统调用：openat, ioctl, mmap, munmap, close, poll, clock_gettime, clock_nanosleep
 *
 * 用法：
 *   fb_invaders              # 默认 3 条命
 *   fb_invaders 5            # 5 条命（练习模式）
 *
 * 操作：
 *   鼠标移动 / ← →  控制飞船
 *   左键/空格         射击 / 重新开始
 *   Esc               退出
 */

/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * 索引：
 *   main               入口
 *     init_game        初始化入侵者/掩体/游戏状态
 *     handle_input     统一处理鼠标 + 键盘事件
 *     update_game      物理更新 + 碰撞 + 状态迁移
 *     render_game      脏矩形绘制 + 消息屏幕
 *   draw_invader_*     三种入侵者绘制函数（含姿态动画）
 *   draw_player_ship   绘制飞船
 *   draw_bunkers       绘制固定掩体
 *   draw_panel         右侧信息面板（分数/生命/入侵者对照/说明）
 *   draw_summary       结算画面（按类型统计得分）
 */

#include "tlibc_everything.h"
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
#define INVADER_ROWS    3
#define INVADER_COLS    7
#define INVADER_GAP_X   4
#define INVADER_GAP_Y   5
#define INVADER_DROP_Y  32
#define INVADER_BASE_SPEED  45
#define INVADER_SPEED_PER_KILL 3

#define PLAYER_W        34
#define PLAYER_H        14
#define PLAYER_BOTTOM   28

#define BULLET_W        3
#define BULLET_H        8
#define PB_SPEED        600
#define MAX_PBULLETS    3
#define EB_SPEED        180
#define MAX_EBULLETS    10

#define MAX_LIVES       3
#define TARGET_FPS      60

/* ── 掩体 ── */
#define BUNKER_COUNT    4
#define BUNKER_COLS     8
#define BUNKER_ROWS     5
#define BUNKER_CELL     5
#define BUNKER_W        (BUNKER_COLS * BUNKER_CELL)
#define BUNKER_H        (BUNKER_ROWS * BUNKER_CELL)

/* ── 右侧面板 ── */
#define PANEL_W         220

/* ── 像素颜色（XRGB8888，B 在最低 8 位） ── */
#define COL_BG          0x0A0A1A
#define COL_GAME_BG     0x070712
#define COL_PANEL_BG    0x14142E
#define COL_WHITE       0xFFFFFF
#define COL_RED         0xFF4444
#define COL_ORANGE      0xFF8844
#define COL_YELLOW      0xDDDD44
#define COL_GREEN       0x44DD44
#define COL_PURPLE      0xCC44FF
#define COL_PLAYER      0x66DDFF
#define COL_BUNKER      0x33AA33
#define COL_SEPARATOR   0x2A2A50
#define COL_HUD_BG      0x161630
#define COL_PBULLET     0xFFFF88
#define COL_EBULLET     0xFF6644

/* ── 入侵者类型 ── */
enum { INV_SQUID, INV_CRAB, INV_BASIC };

static const int INV_TYPE[INVADER_ROWS] = {
    INV_SQUID, INV_CRAB, INV_BASIC
};
static const int INV_POINTS[INVADER_ROWS] = {
    30, 20, 10
};
static const uint32_t INV_COLOR[INVADER_ROWS] = {
    COL_PURPLE, COL_ORANGE, COL_GREEN
};

/* ── 掩体初始图案（V 形缺口的经典造型） ── */
static const unsigned char BUNKER_INIT[BUNKER_ROWS][BUNKER_COLS] = {
    {1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1},
    {1,1,1,0,0,1,1,1},
    {1,1,0,0,0,0,1,1},
};

/* ── 游戏阶段 ── */
enum { PHASE_SERVE, PHASE_PLAY, PHASE_LOST, PHASE_WIN };

/* ── 入侵者 ── */
typedef struct { int x, y, w, h; int alive; int type; } Invader;

/* ── 子弹 ── */
typedef struct { int x, y, w, h; int active; long ay; } Bullet;

/* ── 掩体（固定位置，逐格损毁） ── */
typedef struct {
    int x, y;
    unsigned char cells[BUNKER_ROWS][BUNKER_COLS];
} Bunker;

/* ── 游戏状态 ── */
typedef struct {
    /* 帧缓冲 */
    unsigned char *fbp;
    int scr_w, scr_h, ll;

    /* 游戏区 */
    int game_w, game_h;
    int view_x, view_y;

    /* 输入 */
    struct evdev_mouse *mouse;
    struct evdev_kbd   *kbd;
    int have_kbd;

    /* 玩家 */
    int px, py, pw, ph;

    /* 玩家子弹（多发） */
    Bullet pb[MAX_PBULLETS];
    int shoot_cooldown;

    /* 敌弹 */
    Bullet eb[MAX_EBULLETS];
    int eb_count;

    /* 入侵者 */
    Invader inv[INVADER_ROWS * INVADER_COLS];
    int inv_total;
    int inv_count;
    int inv_dir;
    long inv_ax;

    /* 入侵者动画姿态 */
    int inv_pose;

    /* 掩体 */
    Bunker bunkers[BUNKER_COUNT];
    int bunker_redraw;

    /* 各类型击杀统计 */
    int kills[INVADER_ROWS];

    /* 分数 / 生命 */
    int score;
    int lives;

    /* 帧计数器 */
    int frame;

    /* 阶段 */
    int phase;

    /* 脏数据 */
    int old_px;
    int old_score, old_lives;
    int inv_need_redraw;
    int old_ib[INVADER_ROWS * INVADER_COLS];
    int old_pb_active[MAX_PBULLETS], old_pb_x[MAX_PBULLETS], old_pb_y[MAX_PBULLETS];
    int old_eb_active[MAX_EBULLETS];
    int old_eb_x[MAX_EBULLETS], old_eb_y[MAX_EBULLETS];
    /* 入侵者上一帧包围盒（修正下降残影） */
    int old_inv_l, old_inv_t, old_inv_r, old_inv_b;
    int old_inv_valid;
    /* 掩体上一帧 cells 快照（修正消失残影） */
    unsigned char old_bunker_cells[BUNKER_COUNT][BUNKER_ROWS][BUNKER_COLS];
} Game;

/* ── 前向声明 ── */
static void draw_panel(Game *g);
static void draw_summary(Game *g, uint32_t title_color, const char *title);
static void draw_message(Game *g, const char *title, const char *sub,
                         const char *hint, uint32_t title_color);

/* ── 工具函数 ── */

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

/* ════════════════════════════════════════════
 * 入侵者绘制（3 种类型 × 2 姿态）
 * ════════════════════════════════════════════ */

static void draw_invader_squid(unsigned char *fbp, int x, int y,
                               int w, int h, uint32_t col, int pose, int ll)
{
    int hw = w / 2, qh = h / 4;

    fb_fill_rect(fbp, x + qh, y, hw, h / 2 + 1, col, ll);
    fb_fill_rect(fbp, x + qh - 2, y + h / 2, hw + 4, h / 2, col, ll);
    fb_fill_rect(fbp, x + hw - 4, y + qh, 3, 3, COL_WHITE, ll);
    fb_fill_rect(fbp, x + hw + 1, y + qh, 3, 3, COL_WHITE, ll);

    if (pose == 0) {
        fb_fill_rect(fbp, x + 2, y + h - 4, 4, 4, col, ll);
        fb_fill_rect(fbp, x + w - 6, y + h - 4, 4, 4, col, ll);
    } else {
        fb_fill_rect(fbp, x + 2, y + h - 4, 3, 4, col, ll);
        fb_fill_rect(fbp, x + hw - 1, y + h - 4, 3, 4, col, ll);
        fb_fill_rect(fbp, x + w - 5, y + h - 4, 3, 4, col, ll);
    }
    fb_draw_rect(fbp, x + qh, y, hw, h / 2 + 1, COL_WHITE, ll);
}

static void draw_invader_crab(unsigned char *fbp, int x, int y,
                              int w, int h, uint32_t col, int pose, int ll)
{
    int hw = w / 2, hh = h / 2, qh = h / 4;

    fb_fill_rect(fbp, x + 2, y + 2, w - 4, h - 4, col, ll);
    if (pose == 0) {
        fb_fill_rect(fbp, x - 2, y + qh, 4, hh - 2, col, ll);
        fb_fill_rect(fbp, x + w - 2, y + qh, 4, hh - 2, col, ll);
    } else {
        fb_fill_rect(fbp, x - 3, y + qh, 4, hh, col, ll);
        fb_fill_rect(fbp, x + w - 1, y + qh, 4, hh, col, ll);
    }
    fb_fill_rect(fbp, x + hw - 4, y + h / 3, 3, 3, COL_WHITE, ll);
    fb_fill_rect(fbp, x + hw + 1, y + h / 3, 3, 3, COL_WHITE, ll);
    fb_fill_rect(fbp, x + 3, y + h - 3, 3, 3, col, ll);
    fb_fill_rect(fbp, x + hw - 1, y + h - 3, 3, 3, col, ll);
    fb_fill_rect(fbp, x + w - 6, y + h - 3, 3, 3, col, ll);
    fb_draw_rect(fbp, x + 2, y + 2, w - 4, h - 4, COL_WHITE, ll);
}

static void draw_invader_basic(unsigned char *fbp, int x, int y,
                               int w, int h, uint32_t col, int pose, int ll)
{
    int hw = w / 2, hh = h / 2;
    (void)pose;

    fb_fill_rect(fbp, x + 3, y + 1, w - 6, h - 2, col, ll);
    fb_fill_rect(fbp, x + hw - 2, y, 4, 3, col, ll);
    fb_fill_rect(fbp, x + hw - 2, y + h - 3, 4, 3, col, ll);
    fb_fill_rect(fbp, x + hw - 5, y + hh - 2, 4, 4, COL_WHITE, ll);
    fb_fill_rect(fbp, x + hw + 1, y + hh - 2, 4, 4, COL_WHITE, ll);
    fb_draw_rect(fbp, x + 3, y + 1, w - 6, h - 2, COL_WHITE, ll);
}

static void draw_invader(Game *g, Invader *inv)
{
    int sx = g->view_x + inv->x;
    int sy = g->view_y + inv->y;
    int row = (int)(inv - g->inv) / INVADER_COLS;
    uint32_t col = INV_COLOR[row];

    switch (inv->type) {
    case INV_SQUID: draw_invader_squid(g->fbp, sx, sy, inv->w, inv->h, col, g->inv_pose, g->ll); break;
    case INV_CRAB:  draw_invader_crab(g->fbp, sx, sy, inv->w, inv->h, col, g->inv_pose, g->ll); break;
    case INV_BASIC: draw_invader_basic(g->fbp, sx, sy, inv->w, inv->h, col, g->inv_pose, g->ll); break;
    }
}

/* ── 清除已死入侵者残留像素 ── */
static void erase_dead_invaders(Game *g)
{
    for (int i = 0; i < g->inv_total; i++) {
        if (!g->inv[i].alive && g->old_ib[i]) {
            fb_fill_rect(g->fbp,
                         g->view_x + g->inv[i].x - 2,
                         g->view_y + g->inv[i].y - 2,
                         g->inv[i].w + 5, g->inv[i].h + 5,
                         COL_GAME_BG, g->ll);
        }
    }
}

/* ════════════════════════════════════════════
 * 玩家飞船绘制
 * ════════════════════════════════════════════ */

static void draw_player_ship(unsigned char *fbp, int x, int y,
                             int w, int h, uint32_t col, int ll)
{
    fb_fill_rect(fbp, x + w/2 - 2, y, 4, 3, col, ll);
    fb_fill_rect(fbp, x + w/2 - 4, y + 3, 8, 3, col, ll);
    fb_fill_rect(fbp, x + 1, y + 6, w - 2, h - 6, col, ll);
    fb_fill_rect(fbp, x - 1, y + h - 5, 3, 4, col, ll);
    fb_fill_rect(fbp, x + w - 2, y + h - 5, 3, 4, col, ll);
    fb_fill_rect(fbp, x + w/2 - 3, y + h - 5, 6, 4, 0xAADDFF, ll);
    fb_draw_rect(fbp, x + 1, y + 6, w - 2, h - 6, COL_WHITE, ll);
}

/* ════════════════════════════════════════════
 * 掩体绘制 & 碰撞
 * ════════════════════════════════════════════ */

static void draw_bunkers(Game *g)
{
    for (int b = 0; b < BUNKER_COUNT; b++) {
        for (int r = 0; r < BUNKER_ROWS; r++) {
            for (int c = 0; c < BUNKER_COLS; c++) {
                if (!g->bunkers[b].cells[r][c]) continue;
                int px = g->view_x + g->bunkers[b].x + c * BUNKER_CELL;
                int py = g->view_y + g->bunkers[b].y + r * BUNKER_CELL;
                fb_fill_rect(g->fbp, px, py, BUNKER_CELL, BUNKER_CELL, COL_BUNKER, g->ll);
            }
        }
    }
}

/* 清除掩体被破坏的格子（对比 old_bunker_cells） */
static void erase_bunker_dead_cells(Game *g)
{
    for (int b = 0; b < BUNKER_COUNT; b++) {
        for (int r = 0; r < BUNKER_ROWS; r++) {
            for (int c = 0; c < BUNKER_COLS; c++) {
                if (!g->bunkers[b].cells[r][c] && g->old_bunker_cells[b][r][c]) {
                    int px = g->view_x + g->bunkers[b].x + c * BUNKER_CELL;
                    int py = g->view_y + g->bunkers[b].y + r * BUNKER_CELL;
                    fb_fill_rect(g->fbp, px, py, BUNKER_CELL, BUNKER_CELL,
                                 COL_GAME_BG, g->ll);
                }
            }
        }
    }
}

/* 子弹 vs 掩体碰撞（返回 1 表示子弹被阻挡） */
static int bullet_vs_bunkers(Game *g, int bx, int by, int bw, int bh)
{
    for (int b = 0; b < BUNKER_COUNT; b++) {
        int bnx = g->bunkers[b].x, bny = g->bunkers[b].y;
        for (int r = 0; r < BUNKER_ROWS; r++) {
            for (int c = 0; c < BUNKER_COLS; c++) {
                if (!g->bunkers[b].cells[r][c]) continue;
                int cx = bnx + c * BUNKER_CELL;
                int cy = bny + r * BUNKER_CELL;
                if (bx + bw <= cx) continue;
                if (bx >= cx + BUNKER_CELL) continue;
                if (by + bh <= cy) continue;
                if (by >= cy + BUNKER_CELL) continue;
                /* 命中！摧毁这一格 */
                g->bunkers[b].cells[r][c] = 0;
                g->bunker_redraw = 1;
                return 1;
            }
        }
    }
    return 0;
}

/* ════════════════════════════════════════════
 * 初始化
 * ════════════════════════════════════════════ */

static void init_bunkers(Game *g)
{
    int gap = (g->game_w - BUNKER_COUNT * BUNKER_W) / (BUNKER_COUNT + 1);
    int by = g->py - BUNKER_H - 12;
    if (by < 40) by = 40;

    for (int b = 0; b < BUNKER_COUNT; b++) {
        g->bunkers[b].x = gap + b * (BUNKER_W + gap);
        g->bunkers[b].y = by;
        for (int r = 0; r < BUNKER_ROWS; r++)
            for (int c = 0; c < BUNKER_COLS; c++)
                g->bunkers[b].cells[r][c] = BUNKER_INIT[r][c];
    }
    g->bunker_redraw = 0;
}

static void init_invaders(Game *g)
{
    g->inv_total = INVADER_ROWS * INVADER_COLS;
    g->inv_count = g->inv_total;
    g->inv_dir = 1;
    g->inv_ax = 0;
    g->inv_pose = 0;

    int iw = (g->game_w - 2 * 20 - (INVADER_COLS - 1) * INVADER_GAP_X)
             / INVADER_COLS;
    if (iw < 16) iw = 16;
    if (iw > 38) iw = 38;
    int ih = iw * 3 / 4;
    if (ih < 12) ih = 12;

    int used = INVADER_COLS * iw + (INVADER_COLS - 1) * INVADER_GAP_X;
    int start_x = (g->game_w - used) / 2;
    int start_y = 16;

    int idx = 0;
    for (int row = 0; row < INVADER_ROWS; row++) {
        for (int col = 0; col < INVADER_COLS; col++) {
            g->inv[idx].alive = 1;
            g->inv[idx].x = start_x + col * (iw + INVADER_GAP_X);
            g->inv[idx].y = start_y + row * (ih + INVADER_GAP_Y);
            g->inv[idx].w = iw;
            g->inv[idx].h = ih;
            g->inv[idx].type = INV_TYPE[row];
            g->old_ib[idx] = 1;
            idx++;
        }
    }
}

static void init_game(Game *g, int lives)
{
    g->game_w = (g->scr_w - PANEL_W - 60) / 3;
    if (g->game_w < 300) g->game_w = 300;
    g->game_h = g->game_w * 3 / 4;
    if (g->game_h < 200) g->game_h = 200;

    g->view_x = (g->scr_w - g->game_w - 20 - PANEL_W) / 2;
    g->view_y = (g->scr_h - g->game_h) / 2;

    g->score = 0;
    g->lives = lives;
    g->phase = PHASE_SERVE;
    g->frame = 0;

    g->pw = PLAYER_W;
    g->ph = PLAYER_H;
    g->py = g->game_h - PLAYER_BOTTOM;
    g->px = (g->game_w - g->pw) / 2;

    for (int i = 0; i < MAX_PBULLETS; i++) {
        g->pb[i].active = 0;
        g->pb[i].w = BULLET_W;
        g->pb[i].h = BULLET_H;
        g->old_pb_active[i] = 0;
    }
    g->shoot_cooldown = 0;

    g->eb_count = 0;
    for (int i = 0; i < MAX_EBULLETS; i++) {
        g->eb[i].active = 0;
        g->eb[i].w = BULLET_W;
        g->eb[i].h = BULLET_H;
        g->old_eb_active[i] = 0;
    }

    memset(g->kills, 0, sizeof(g->kills));
    init_invaders(g);
    init_bunkers(g);

    g->inv_need_redraw = 1;
    g->old_px = g->px;
    g->old_score = -1;
    g->old_lives = -1;
    g->old_inv_valid = 0;

    /* 初始化掩体 old_bunker_cells */
    for (int b = 0; b < BUNKER_COUNT; b++)
        for (int r = 0; r < BUNKER_ROWS; r++)
            for (int c = 0; c < BUNKER_COLS; c++)
                g->old_bunker_cells[b][r][c] = BUNKER_INIT[r][c];
}

static int invaders_at_bottom(Game *g)
{
    for (int i = 0; i < g->inv_total; i++) {
        if (!g->inv[i].alive) continue;
        if (g->inv[i].y + g->inv[i].h >= g->py) return 1;
    }
    return 0;
}

/* ════════════════════════════════════════════
 * 输入处理
 * ════════════════════════════════════════════ */

static void handle_input(Game *g, struct evdev_mouse_state *ms,
                         int *kb_left, int *kb_right,
                         int *kb_space, int *kb_shoot)
{
    (void)kb_shoot;

    if (evdev_mouse_read(g->mouse, ms) && ms->updated)
        g->px = (ms->x - g->view_x) - g->pw / 2;

    if (g->have_kbd) {
        int key, type;
        while ((key = evdev_kbd_read(g->kbd, &type)) > 0) {
            if (key == KEY_ESC && type == EVDEV_PRESS) { g->px = -9999; return; }
            if (key == KEY_LEFT)  *kb_left  = (type == EVDEV_PRESS || type == EVDEV_REPEAT);
            if (key == KEY_RIGHT) *kb_right = (type == EVDEV_PRESS || type == EVDEV_REPEAT);
            if (key == KEY_SPACE && type == EVDEV_PRESS) *kb_space = 1;
        }
    }
    if (*kb_left)  g->px -= 4;
    if (*kb_right) g->px += 4;
    if (g->px < 0) g->px = 0;
    if (g->px + g->pw > g->game_w) g->px = g->game_w - g->pw;
}

/* ════════════════════════════════════════════
 * 物理更新
 * ════════════════════════════════════════════ */

static void update_game(Game *g, long dt_us)
{
    g->frame++;

    if (g->phase == PHASE_SERVE) {
        if (g->frame % 3 == 0) g->inv_pose = !g->inv_pose;
        return;
    }
    if (g->phase != PHASE_PLAY) return;

    /* ── 入侵者动画 ── */
    if (g->frame % 3 == 0) {
        g->inv_pose = !g->inv_pose;
        g->inv_need_redraw = 1;
    }

    /* ── 入侵者横向移动 ── */
    if (g->inv_count > 0) {
        int speed = INVADER_BASE_SPEED
                  + (g->inv_total - g->inv_count) * INVADER_SPEED_PER_KILL;
        g->inv_ax += speed * g->inv_dir * dt_us;
        int dx = (int)(g->inv_ax / 1000000);
        g->inv_ax %= 1000000;

        if (dx != 0) {
            int leftmost = g->game_w, rightmost = 0;
            for (int i = 0; i < g->inv_total; i++) {
                if (!g->inv[i].alive) continue;
                int l = g->inv[i].x, r = g->inv[i].x + g->inv[i].w;
                if (l < leftmost)  leftmost  = l;
                if (r > rightmost) rightmost = r;
            }

            int new_l = leftmost + dx, new_r = rightmost + dx;

            if (new_l < 0) {
                int shift = -new_l;
                for (int i = 0; i < g->inv_total; i++)
                    if (g->inv[i].alive) g->inv[i].x += shift;
                g->inv_dir = 1;
                for (int i = 0; i < g->inv_total; i++)
                    if (g->inv[i].alive) g->inv[i].y += INVADER_DROP_Y;
                g->inv_need_redraw = 1;
            } else if (new_r > g->game_w) {
                int over = new_r - g->game_w;
                for (int i = 0; i < g->inv_total; i++)
                    if (g->inv[i].alive) g->inv[i].x -= over;
                g->inv_dir = -1;
                for (int i = 0; i < g->inv_total; i++)
                    if (g->inv[i].alive) g->inv[i].y += INVADER_DROP_Y;
                g->inv_need_redraw = 1;
            } else {
                for (int i = 0; i < g->inv_total; i++)
                    if (g->inv[i].alive) g->inv[i].x += dx;
                g->inv_need_redraw = 1;
            }

            if (invaders_at_bottom(g)) { g->phase = PHASE_LOST; return; }
        }
    }

    /* ── 玩家子弹移动 ── */
    for (int i = 0; i < MAX_PBULLETS; i++) {
        if (g->pb[i].active) {
            g->pb[i].ay -= PB_SPEED * dt_us;
            int dy = (int)(g->pb[i].ay / 1000000);
            g->pb[i].ay %= 1000000;
            g->pb[i].y += dy;
            if (g->pb[i].y + g->pb[i].h < 0)
                g->pb[i].active = 0;
        }
    }

    /* ── 敌弹移动 ── */
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (g->eb[i].active) {
            g->eb[i].ay += EB_SPEED * dt_us;
            int dy = (int)(g->eb[i].ay / 1000000);
            g->eb[i].ay %= 1000000;
            g->eb[i].y += dy;
            if (g->eb[i].y > g->game_h) {
                g->eb[i].active = 0;
                g->eb_count--;
            }
        }
    }

    /* ── 敌弹 vs 掩体 ── */
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (g->eb[i].active) {
            if (bullet_vs_bunkers(g, g->eb[i].x, g->eb[i].y,
                                  g->eb[i].w, g->eb[i].h)) {
                g->eb[i].active = 0;
                g->eb_count--;
            }
        }
    }
    /* ── 玩家子弹 vs 入侵者 ── */
    for (int b = 0; b < MAX_PBULLETS; b++) {
        if (!g->pb[b].active) continue;
        for (int i = 0; i < g->inv_total; i++) {
            if (!g->inv[i].alive) continue;
            if (g->pb[b].x + g->pb[b].w <= g->inv[i].x) continue;
            if (g->pb[b].x >= g->inv[i].x + g->inv[i].w) continue;
            if (g->pb[b].y + g->pb[b].h <= g->inv[i].y) continue;
            if (g->pb[b].y >= g->inv[i].y + g->inv[i].h) continue;
            g->inv[i].alive = 0;
            g->inv_count--;
            g->kills[i / INVADER_COLS]++;
            g->score += INV_POINTS[i / INVADER_COLS];
            g->pb[b].active = 0;
            g->inv_need_redraw = 1;
            if (g->inv_count == 0) { g->phase = PHASE_WIN; return; }
            break;
        }
    }

    /* ── 敌弹 vs 玩家碰撞 ── */
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (!g->eb[i].active) continue;
        if (g->eb[i].x + g->eb[i].w <= g->px) continue;
        if (g->eb[i].x >= g->px + g->pw) continue;
        if (g->eb[i].y + g->eb[i].h <= g->py) continue;
        if (g->eb[i].y >= g->py + g->ph) continue;

        g->eb[i].active = 0;
        g->eb_count--;
        g->lives--;
        if (g->lives <= 0) g->phase = PHASE_LOST;
        return;
    }

    /* ── 敌人射击 ── */
    if (g->inv_count > 0 && g->eb_count < MAX_EBULLETS
        && (g->frame % 40) == 0) {
        int col = (g->frame / 40) % INVADER_COLS;
        int bottom_idx = -1;
        for (int row = INVADER_ROWS - 1; row >= 0; row--) {
            int idx = row * INVADER_COLS + col;
            if (g->inv[idx].alive) { bottom_idx = idx; break; }
        }
        if (bottom_idx >= 0) {
            for (int i = 0; i < MAX_EBULLETS; i++) {
                if (!g->eb[i].active) {
                    g->eb[i].active = 1;
                    g->eb[i].x = g->inv[bottom_idx].x
                               + g->inv[bottom_idx].w / 2 - BULLET_W / 2;
                    g->eb[i].y = g->inv[bottom_idx].y + g->inv[bottom_idx].h;
                    g->eb[i].ay = 0;
                    g->eb_count++;
                    break;
                }
            }
        }
    }
}

/* ════════════════════════════════════════════
 * 渲染
 * ════════════════════════════════════════════ */

static void render_game(Game *g)
{
    int vx = g->view_x, vy = g->view_y;
    /* ── 擦除旧玩家子弹 ── */
    for (int i = 0; i < MAX_PBULLETS; i++) {
        if (g->old_pb_active[i]) {
            fb_fill_rect(g->fbp, vx + g->old_pb_x[i], vy + g->old_pb_y[i],
                         g->pb[i].w, g->pb[i].h, COL_GAME_BG, g->ll);
        }
    }

    /* ── 擦除旧敌弹 ── */
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (g->old_eb_active[i]) {
            fb_fill_rect(g->fbp, vx + g->old_eb_x[i], vy + g->old_eb_y[i],
                         g->eb[i].w, g->eb[i].h, COL_GAME_BG, g->ll);
        }
    }

    /* ── 清除已死入侵者 ── */
    erase_dead_invaders(g);

    /* ── 清除已损坏掩体格子 ── */
    if (g->bunker_redraw) {
        erase_bunker_dead_cells(g);
        g->bunker_redraw = 0;
    }

    /* ── 重绘入侵者（旧包围盒防残影 + 大边距应对触壁反弹） ── */
    if (g->inv_need_redraw) {
        int new_l = g->game_w, new_t = g->game_h;
        int new_r = 0, new_b = 0;
        for (int i = 0; i < g->inv_total; i++) {
            if (!g->inv[i].alive) continue;
            if (g->inv[i].x < new_l) new_l = g->inv[i].x;
            if (g->inv[i].y < new_t) new_t = g->inv[i].y;
            int r = g->inv[i].x + g->inv[i].w;
            int b = g->inv[i].y + g->inv[i].h;
            if (r > new_r) new_r = r;
            if (b > new_b) new_b = b;
        }

        /* 合并已保存的上一帧包围盒（覆盖所有旧像素） */
        if (g->old_inv_valid) {
            if (g->old_inv_l < new_l) new_l = g->old_inv_l;
            if (g->old_inv_t < new_t) new_t = g->old_inv_t;
            if (g->old_inv_r > new_r) new_r = g->old_inv_r;
            if (g->old_inv_b > new_b) new_b = g->old_inv_b;
        }

        int margin = 8; /* 大边距：应对触壁反弹位移 + 蟹钳伸出 */
        int dirty_l = (new_l > margin) ? new_l - margin : 0;
        int dirty_t = (new_t > margin) ? new_t - margin : 0;
        int dirty_r = (new_r + margin < g->game_w) ? new_r + margin : g->game_w - 1;
        int dirty_b = (new_b + margin < g->game_h) ? new_b + margin : g->game_h - 1;

        fb_fill_rect(g->fbp, vx + dirty_l, vy + dirty_t,
                     dirty_r - dirty_l, dirty_b - dirty_t, COL_GAME_BG, g->ll);

        for (int i = 0; i < g->inv_total; i++)
            if (g->inv[i].alive) draw_invader(g, &g->inv[i]);

        g->inv_need_redraw = 0;
    }

    /* ── 绘制掩体 ── */
    draw_bunkers(g);

    for (int i = 0; i < MAX_PBULLETS; i++) {
        if (g->pb[i].active) {
            fb_fill_rect(g->fbp, vx + g->pb[i].x, vy + g->pb[i].y,
                         g->pb[i].w, g->pb[i].h, COL_PBULLET, g->ll);
        }
    }

    /* ── 绘制敌弹 ── */
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (g->eb[i].active) {
            fb_fill_rect(g->fbp, vx + g->eb[i].x, vy + g->eb[i].y,
                         g->eb[i].w, g->eb[i].h, COL_EBULLET, g->ll);
        }
    }

    /* ── 擦除旧玩家 + 绘制新位置（扩展区域覆盖翼尖） ── */
    if (g->px != g->old_px) {
        fb_fill_rect(g->fbp, vx + g->old_px - 1, vy + g->py,
                     g->pw + 2, g->ph + 1, COL_GAME_BG, g->ll);
    }
    draw_player_ship(g->fbp, vx + g->px, vy + g->py,
                     g->pw, g->ph, COL_PLAYER, g->ll);

    /* ── 游戏区外保护性清空 + 边框重绘 ── */
    fb_fill_rect(g->fbp, vx - 4, vy - 4, g->game_w + 8, 4, COL_BG, g->ll);
    fb_fill_rect(g->fbp, vx - 4, vy + g->game_h, g->game_w + 8, 4, COL_BG, g->ll);
    fb_fill_rect(g->fbp, vx - 4, vy, 4, g->game_h, COL_BG, g->ll);
    fb_fill_rect(g->fbp, vx + g->game_w, vy, 4, g->game_h, COL_BG, g->ll);
    fb_draw_rect(g->fbp, vx, vy, g->game_w, g->game_h, 0x2A2A4A, g->ll);

    /* ── 面板 ── */
    if (g->score != g->old_score || g->lives != g->old_lives)
        draw_panel(g);

    /* ── 结算/消息 ── */
    if (g->phase == PHASE_LOST)
        draw_summary(g, COL_RED, "GAME OVER");
    else if (g->phase == PHASE_WIN)
        draw_summary(g, COL_YELLOW, "YOU WIN!");

    /* ── 保存本帧状态 ── */
    g->old_px = g->px;
    for (int i = 0; i < MAX_PBULLETS; i++) {
        g->old_pb_active[i] = g->pb[i].active;
        g->old_pb_x[i] = g->pb[i].x;
        g->old_pb_y[i] = g->pb[i].y;
    }
    for (int i = 0; i < MAX_EBULLETS; i++) {
        g->old_eb_active[i] = g->eb[i].active;
        g->old_eb_x[i] = g->eb[i].x;
        g->old_eb_y[i] = g->eb[i].y;
    }
    for (int i = 0; i < g->inv_total; i++)
        g->old_ib[i] = g->inv[i].alive;
    for (int b = 0; b < BUNKER_COUNT; b++)
        for (int r = 0; r < BUNKER_ROWS; r++)
            for (int c = 0; c < BUNKER_COLS; c++)
                g->old_bunker_cells[b][r][c] = g->bunkers[b].cells[r][c];
    /* ── 无条件保存入侵者包围盒（确保 old_inv 始终反映实际绘制状态） ── */
    {
        int ol = g->game_w, ot = g->game_h, or_ = 0, ob = 0, any = 0;
        for (int i = 0; i < g->inv_total; i++) {
            if (!g->inv[i].alive) continue;
            any = 1;
            if (g->inv[i].x < ol) ol = g->inv[i].x;
            if (g->inv[i].y < ot) ot = g->inv[i].y;
            int r = g->inv[i].x + g->inv[i].w;
            int b = g->inv[i].y + g->inv[i].h;
            if (r > or_) or_ = r;
            if (b > ob) ob = b;
        }
        if (any) {
            g->old_inv_l = ol; g->old_inv_t = ot;
            g->old_inv_r = or_; g->old_inv_b = ob;
            g->old_inv_valid = 1;
        } else {
            g->old_inv_valid = 0;
        }
    }
}
/* ════════════════════════════════════════════
 * 右侧信息面板
 * ════════════════════════════════════════════ */

static void draw_panel(Game *g)
{
    int px = g->view_x + g->game_w + 20;
    int pw = PANEL_W;
    int py = g->view_y;
    int ph = g->game_h;
    char buf[32];

    fb_fill_rect(g->fbp, px, py, pw, ph, COL_PANEL_BG, g->ll);

    fb_draw_string_scaled(g->fbp,
        px + (pw - fb_string_width_scaled("INVADERS", 2)) / 2,
        py + 30, "INVADERS", COL_WHITE, g->ll, 2);
    fb_fill_rect(g->fbp, px + 20, py + 78, pw - 40, 1, COL_SEPARATOR, g->ll);

    int ly = py + 100;
    fb_draw_string_bg(g->fbp, px + 20, ly, "SCORE", 0x8888AA, COL_PANEL_BG, g->ll);
    utoa_simple(g->score, buf, sizeof(buf));
    fb_draw_string_scaled(g->fbp, px + 20, ly + 22, buf, COL_YELLOW, g->ll, 2);
    fb_fill_rect(g->fbp, px + 20, ly + 58, pw - 40, 1, COL_SEPARATOR, g->ll);

    int ly2 = ly + 78;
    fb_draw_string_bg(g->fbp, px + 20, ly2, "LIVES", 0x8888AA, COL_PANEL_BG, g->ll);
    for (int i = 0; i < g->lives; i++)
        fb_fill_circle(g->fbp, px + 24 + i * 24, ly2 + 28, 6, COL_RED, g->ll);
    fb_fill_rect(g->fbp, px + 20, ly2 + 50, pw - 40, 1, COL_SEPARATOR, g->ll);

    int ly3 = ly2 + 68;
    fb_draw_string_bg(g->fbp, px + 20, ly3, "ALIENS", 0x8888AA, COL_PANEL_BG, g->ll);
    int ax = px + 20;
    draw_invader_squid(g->fbp, ax, ly3 + 22, 18, 14, COL_PURPLE, 0, g->ll);
    ax += 38;
    draw_invader_crab(g->fbp, ax, ly3 + 22, 18, 14, COL_ORANGE, 0, g->ll);
    ax += 38;
    draw_invader_basic(g->fbp, ax, ly3 + 22, 18, 14, COL_GREEN, 0, g->ll);
    fb_fill_rect(g->fbp, px + 20, ly3 + 46, pw - 40, 1, COL_SEPARATOR, g->ll);

    int ly4 = ly3 + 60;
    fb_draw_string_bg(g->fbp, px + 20, ly4, "CONTROLS", 0x666688, COL_PANEL_BG, g->ll);
    fb_draw_string_bg(g->fbp, px + 20, ly4 + 22, "MOUSE  /  <- ->", 0x888888, COL_PANEL_BG, g->ll);
    fb_draw_string_bg(g->fbp, px + 20, ly4 + 40, "LCLICK / SPACE", 0x888888, COL_PANEL_BG, g->ll);
    fb_draw_string_bg(g->fbp, px + 20, ly4 + 58, "ESC  QUIT", 0x888888, COL_PANEL_BG, g->ll);

    g->old_score = g->score;
    g->old_lives = g->lives;
}

/* ════════════════════════════════════════════
 * 结算画面（分类型统计）
 * ════════════════════════════════════════════ */

static void draw_summary(Game *g, uint32_t title_color, const char *title)
{
    int cx = g->view_x + g->game_w / 2;
    int cy = g->view_y + g->game_h / 2;
    int box_w = 340, box_h = 210;
    int left = cx - box_w / 2;
    int top  = cy - box_h / 2;
    char buf[64];

    fb_fill_rect(g->fbp, left, top, box_w, box_h, COL_HUD_BG, g->ll);
    fb_draw_rect(g->fbp, left, top, box_w, box_h, title_color, g->ll);

    /* 标题 */
    fb_draw_string_bg(g->fbp, cx - fb_string_width(title) / 2, top + 14,
                      title, title_color, COL_HUD_BG, g->ll);

    /* 分割线 */
    fb_fill_rect(g->fbp, left + 20, top + 40, box_w - 40, 1, COL_SEPARATOR, g->ll);

    /* 统计表头：类型 | 击杀 | 分数 */
    int line_y = top + 50;
    int col1 = left + 30, col2 = left + 160, col3 = left + 250;

    /* 按入侵者类型（去重）统计 */
    int seen_types[3] = {0};
    int type_kills[3] = {0}, type_score[3] = {0};
    const char *type_names[3] = { "SQUID", "CRAB", "BASIC" };
    uint32_t type_colors[3] = { COL_PURPLE, COL_ORANGE, COL_GREEN };

    for (int row = 0; row < INVADER_ROWS; row++) {
        int t = INV_TYPE[row];
        seen_types[t] = 1;
        type_kills[t] += g->kills[row];
        type_score[t] += g->kills[row] * INV_POINTS[row];
    }

    for (int t = 0; t < 3; t++) {
        if (!seen_types[t]) continue;
        char line[48];
        snprintf(line, sizeof(line), "x %d", type_kills[t]);
        fb_draw_string_bg(g->fbp, col1, line_y, type_names[t], type_colors[t], COL_HUD_BG, g->ll);
        fb_draw_string_bg(g->fbp, col2, line_y, line, COL_WHITE, COL_HUD_BG, g->ll);
        utoa_simple(type_score[t], buf, sizeof(buf));
        fb_draw_string_bg(g->fbp, col3, line_y, buf, COL_YELLOW, COL_HUD_BG, g->ll);
        line_y += 24;
    }

    /* 总分分割线 */
    line_y += 4;
    fb_fill_rect(g->fbp, left + 20, line_y, box_w - 40, 1, COL_SEPARATOR, g->ll);
    line_y += 10;

    /* 总分 */
    fb_draw_string_bg(g->fbp, col1, line_y, "TOTAL", 0x8888AA, COL_HUD_BG, g->ll);
    utoa_simple(g->score, buf, sizeof(buf));
    fb_fill_rect(g->fbp, col3 - 2, line_y, fb_string_width_scaled(buf, 2) + 4,
                 fb_font_height_scaled(2), COL_HUD_BG, g->ll);
    fb_draw_string_scaled(g->fbp, col3, line_y, buf, COL_YELLOW, g->ll, 2);

    /* 底部提示 */
    fb_draw_string_bg(g->fbp, cx - fb_string_width("SPACE TO RESTART  ESC TO QUIT") / 2,
                      top + box_h - 22, "SPACE TO RESTART  ESC TO QUIT",
                      0x888888, COL_HUD_BG, g->ll);
}

/* ════════════════════════════════════════════
 * 居中消息（HUD 覆层，通用）
 * ════════════════════════════════════════════ */

static void draw_message(Game *g, const char *title, const char *sub,
                         const char *hint, uint32_t title_color)
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

/* ════════════════════════════════════════════
 * 启动画面
 * ════════════════════════════════════════════ */

static void draw_title_screen(Game *g)
{
    fb_fill_rect(g->fbp, 0, 0, g->scr_w, g->scr_h, COL_BG, g->ll);

    fb_fill_rect(g->fbp, g->view_x, g->view_y, g->game_w, g->game_h, COL_GAME_BG, g->ll);
    fb_draw_rect(g->fbp, g->view_x, g->view_y, g->game_w, g->game_h, 0x2A2A4A, g->ll);

    for (int i = 0; i < g->inv_total; i++)
        if (g->inv[i].alive) draw_invader(g, &g->inv[i]);

    draw_bunkers(g);

    draw_player_ship(g->fbp, g->view_x + g->px, g->view_y + g->py,
                     g->pw, g->ph, COL_PLAYER, g->ll);
    draw_panel(g);

    int cx = g->view_x + g->game_w / 2;
    int cy = g->view_y + g->game_h / 2;
    fb_fill_rect(g->fbp, cx - 160, cy - 16, 320, 32, COL_HUD_BG, g->ll);
    fb_draw_string_bg(g->fbp, cx - fb_string_width("CLICK TO START") / 2,
                      cy - 10, "CLICK TO START", COL_WHITE, COL_HUD_BG, g->ll);

    g->inv_need_redraw = 0;
    g->old_inv_valid = 0;
}

/* ════════════════════════════════════════════
 * 主函数
 * ════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    int lives = MAX_LIVES;
    if (argc > 1) {
        int n = 0;
        const char *p = argv[1];
        while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
        if (n >= 1 && n <= 99) lives = n;
    }

    int fd = __openat(AT_FDCWD, "/dev/fb0", O_RDWR, 0);
    if (fd < 0) { __printf("fb_invaders: cannot open /dev/fb0 (%d)\n", fd); return 1; }

    struct fb_var_screeninfo var;
    if (__ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0)
        { __printf("fb_invaders: FBIOGET_VSCREENINFO failed\n"); __close(fd); return 1; }

    struct fb_fix_screeninfo fix;
    if (__ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0)
        { __printf("fb_invaders: FBIOGET_FSCREENINFO failed\n"); __close(fd); return 1; }

    int scr_w = var.xres, scr_h = var.yres;
    int ll = fix.line_length;
    size_t screensize = (size_t)var.yres_virtual * ll;

    unsigned char *fbp = __mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fbp == MAP_FAILED) { __printf("fb_invaders: mmap failed\n"); __close(fd); return 1; }

    struct evdev_mouse *mouse = evdev_mouse_open(scr_w, scr_h);
    if (!mouse) { __printf("fb_invaders: no mouse found\n"); __munmap(fbp, screensize); __close(fd); return 1; }

    struct evdev_kbd *kbd = evdev_kbd_open();
    if (!kbd) __printf("fb_invaders: warning - no keyboard\n");

    Game game;
    memset(&game, 0, sizeof(game));
    game.fbp = fbp; game.scr_w = scr_w; game.scr_h = scr_h; game.ll = ll;
    game.mouse = mouse; game.kbd = kbd; game.have_kbd = (kbd != NULL);

    init_game(&game, lives);

    int graphics_mode = 0;
    tlibc_set_term_raw_and_noecho(0);
    graphics_mode = 1;
    { int km = KD_GRAPHICS; __ioctl(0, KDSETMODE, &km); }

    __printf("fb_invaders: %u x %u  game %dx%d  %d lives\n",
             scr_w, scr_h, game.game_w, game.game_h, lives);

    draw_title_screen(&game);

    long interval_ns = 1000000000L / TARGET_FPS;
    struct timespec t_start, t_last, next_frame;
    __clock_gettime(CLOCK_MONOTONIC, &t_start);
    t_last = next_frame = t_start;
    ts_add_ns(&next_frame, interval_ns);

    int running = 1;
    struct evdev_mouse_state ms = {0};
    int prev_buttons = 0;
    int kb_left = 0, kb_right = 0, kb_space = 0, kb_shoot = 0;

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
                handle_input(&game, &ms, &kb_left, &kb_right, &kb_space, &kb_shoot);
            if (game.px < -1000) { running = 0; break; }
        }

        int click = (ms.buttons & 1) && !(prev_buttons & 1);
        prev_buttons = ms.buttons;

        /* ── 游戏逻辑 ── */
        {
            if (game.phase == PHASE_SERVE) {
                if (click || kb_space) {
                    int cx = game.view_x + game.game_w / 2;
                    int cy = game.view_y + game.game_h / 2;
                    fb_fill_rect(game.fbp, cx - 162, cy - 18, 324, 36, COL_GAME_BG, game.ll);
                    game.phase = PHASE_PLAY;
                    kb_space = 0;
                }
            }

            if (game.phase == PHASE_PLAY && game.shoot_cooldown <= 0) {
                if (click || kb_space) {
                    for (int i = 0; i < MAX_PBULLETS; i++) {
                        if (!game.pb[i].active) {
                            game.pb[i].active = 1;
                            game.pb[i].x = game.px + game.pw / 2 - BULLET_W / 2;
                            game.pb[i].y = game.py - BULLET_H;
                            game.pb[i].ay = 0;
                            break;
                        }
                    }
                    game.shoot_cooldown = 5;
                    kb_space = 0;
                }
            }
            if (game.shoot_cooldown > 0) game.shoot_cooldown--;

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

        render_game(&game);

        if (now.tv_sec - t_start.tv_sec >= 600) {
            __printf("fb_invaders: timed out after 10 minutes\n");
            running = 0;
        }
    }

    if (graphics_mode) { { int km = KD_TEXT; __ioctl(0, KDSETMODE, &km); } tlibc_restore_term(0); }
    if (kbd) evdev_kbd_close(kbd);
    evdev_mouse_close(mouse);
    __munmap(fbp, screensize);
    __close(fd);
    __printf("fb_invaders:  score %d\n", game.score);
    return 0;
}
