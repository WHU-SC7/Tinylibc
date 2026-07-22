/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * fb_ball — 流畅弹跳小球 + 实时 FPS 显示
 *
 * 机制：
 *   1. 脏矩形——仅擦除旧球 + 绘制新球，单帧开销与球面积成正比
 *   2. 绝对时间同步——clock_nanosleep(TIMER_ABSTIME) 锁定帧率
 *   3. 固定点物理时间步长——速度单位像素/秒，dt 由实际时钟计算，各帧率速度一致
 *   4. 5×7 位图字体屏幕 FPS 显示——每秒刷新，不影响帧率检测准确性
 *
 * ⚠ 帧率说明：
 *   屏幕上显示的 FPS 是动画循环的帧率（clock_nanosleep 锁定的目标帧率），
 *   反映的是 CPU 侧每秒钟执行了多少次绘制迭代，而非 fbdev 硬件的实际刷新率。
 *   fbdev 没有标准的垂直同步查询接口（FBIO_WAITFORVSYNC 在部分驱动不可用），
 *   因此本程序不测量也不显示显示器的真实刷新率。
 *
 * 系统调用：openat, ioctl(FBIOGET_VSCREENINFO/FSCREENINFO), mmap, munmap,
 *          close, poll, clock_gettime, clock_nanosleep
 *
 * 注意：需要在原始 TTY（Ctrl+Alt+F3）下运行。
 *
 * 用法：
 *   fb_ball                 # 60fps 慢速弹跳
 *   fb_ball 30              # 30fps 更低帧率
 *   fb_ball 60 300          # 60fps 速度 300px/s
 *   fb_ball 30 100          # 30fps 速度 100px/s（极慢）
 *
 * 退出：按 q 或 Q，自动恢复终端模式并打印 FPS 统计。
 */

/*
 * 索引：
 *   main                 入口：打开设备 → mmap → 清屏 →
 *                         动画循环（clock_nanosleep→记旧位→移球→擦旧→画新→FPS显示）
 *                         → 恢复退出并打印统计
 *   draw_fps_str         用 5×7 位图字体在显存上渲染 FPS 数字
 *   clear_fps_str        用黑色矩形擦除旧 FPS 文字区域
 */

#include "core.h"
#include "fcntl.h"
#include "mman.h"
#include "linux_fb.h"
#include "fb_draw.h"
#include "fb_font.h"
#include "tty.h"

/* ── 颜色 ── */
#define COL_WHITE  0xFFFFFF
#define COL_BLACK  0x000000
#define COL_CYAN   0x00FFFF
#define COL_GREEN  0x22FF22

/* ── 默认速度（像素/秒）——慢速可观察 ── */
#define SPEED_X  150
#define SPEED_Y  100

/* ── TTY 图形模式 ── */
#define KDSETMODE     0x4B3A
#define KD_TEXT       0x00
#define KD_GRAPHICS   0x01

/* ── 安全超时（秒）── */
#define MAX_SEC    300

/*
 * 5×7 位图字体：数字 0-9
 * 每字符 7 字节，最高位为左，0=透明 1=前景色
 */
static const unsigned char font5x7[10][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},  /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},  /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},  /* 2 */
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},  /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},  /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},  /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},  /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},  /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},  /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},  /* 9 */
};

/* ── 用 5×7 字体渲染数字字符串到 framebuffer ── */
static void draw_fps_str(unsigned char *fbp, int x, int y,
                         const char *s, uint32_t color, int ll)
{
    while (*s) {
        int ch = *s - '0';
        if (ch >= 0 && ch <= 9) {
            for (int row = 0; row < 7; row++) {
                unsigned char bits = font5x7[ch][row];
                if (bits == 0) continue;          /* 空行直接跳过 */
                for (int col = 0; col < 5; col++) {
                    if (bits & (1 << (4 - col)))
                        fb_put_pixel(fbp, x + col, y + row, color, ll);
                }
            }
        } else if (*s == ':') {
            fb_put_pixel(fbp, x + 1, y + 2, color, ll);
            fb_put_pixel(fbp, x + 1, y + 4, color, ll);
        } else if (*s == '.') {
            fb_put_pixel(fbp, x + 2, y + 6, color, ll);
        }
        x += 7;   /* 5px 字符 + 2px 间距 */
        s++;
    }
}

/* ── 黑色矩形擦除 fps 显示区域 ── */
static void clear_fps_str(unsigned char *fbp, int x, int y, int len, int ll)
{
    fb_fill_rect(fbp, x, y, len * 7, 7, COL_BLACK, ll);
}

/* ── poll 检测 stdin 是否有输入 ── */
static int poll_stdin(void)
{
    struct pollfd pfd;
    pfd.fd = 0;
    pfd.events = POLLIN;
    if (__poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
        return 1;
    return 0;
}

static int read_key(void)
{
    unsigned char c;
    if (__read(0, &c, 1) == 1) return c;
    return 0;
}

/* ── timespec 工具 ── */

static void ts_add_ns(struct timespec *ts, long ns)
{
    ts->tv_nsec += ns;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += ts->tv_nsec / 1000000000L;
        ts->tv_nsec %= 1000000000L;
    }
}

static long ts_diff_us(const struct timespec *a, const struct timespec *b)
{
    return (a->tv_sec - b->tv_sec) * 1000000L
         + (a->tv_nsec - b->tv_nsec) / 1000L;
}

int main(int argc, char *argv[])
{
    /* ── 解析参数 ── */
    int target_fps = 60;
    int speed_x = SPEED_X;
    int speed_y = SPEED_Y;

    if (argc > 1) {
        int n = 0;
        const char *p = argv[1];
        while (*p >= '0' && *p <= '9')
            n = n * 10 + (*p++ - '0');
        if (n >= 1 && n <= 60) target_fps = n;
    }
    if (argc > 2) {
        int n = 0;
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9')
            n = n * 10 + (*p++ - '0');
        if (n >= 10 && n <= 3000) {
            speed_x = n;
            speed_y = n * 2 / 3;
        }
    }

    /* ── 打开 /dev/fb0 ── */
    int fd = __openat(AT_FDCWD, "/dev/fb0", O_RDWR, 0);
    if (fd < 0) {
        __printf("fb_ball: cannot open /dev/fb0 (%d)\n", fd);
        return 1;
    }

    struct fb_var_screeninfo var;
    if (__ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        __printf("fb_ball: FBIOGET_VSCREENINFO failed\n");
        __close(fd);
        return 1;
    }

    struct fb_fix_screeninfo fix;
    if (__ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        __printf("fb_ball: FBIOGET_FSCREENINFO failed\n");
        __close(fd);
        return 1;
    }

    int w = var.xres;
    int h = var.yres;
    int ll = fix.line_length;
    size_t screensize = (size_t)var.yres_virtual * ll;

    unsigned char *fbp = __mmap(0, screensize, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, 0);
    if (fbp == MAP_FAILED) {
        __printf("fb_ball: mmap failed\n");
        __close(fd);
        return 1;
    }

    /* ── 球参数 ── */
    int bx = w / 2, by = h / 2;
    int r = 25;
    int vx = speed_x;
    int vy = speed_y;
    long acc_x = 0, acc_y = 0;

    /* ── 切换到图形模式 ── */
    int graphics_mode = 0;
    tlibc_set_term_raw_and_noecho(0);
    graphics_mode = 1;
    { int km = KD_GRAPHICS; __ioctl(0, KDSETMODE, &km); }

    __printf("fb_ball: %u×%u  target %dfps  speed %dpx/s  press q to quit\n",
             w, h, target_fps, speed_x);

    /* ── 初始清屏（全黑）── */
    fb_fill_rect(fbp, 0, 0, w, h, COL_BLACK, ll);

    /* ── 底部提示 ── */
    fb_draw_string(fbp, (w - fb_string_width("Bouncing Ball Demo  |  Press Q to quit")) / 2,
                   h - FB_FONT_H - 8, "Bouncing Ball Demo  |  Press Q to quit",
                   0x666666, ll);

    /* ── 帧计时初始化 ── */
    long interval_ns = 1000000000L / target_fps;
    struct timespec t_start, t_last, next_frame, t_fps;
    __clock_gettime(CLOCK_MONOTONIC, &t_start);
    t_last = next_frame = t_fps = t_start;
    ts_add_ns(&next_frame, interval_ns);

    /* ── FPS 统计 ── */
    int frame_count = 0;
    int max_fps_seen = 0;
    int min_fps_seen = 999;
    int total_frames = 0;
    int running = 1;

    /* FPS 显示缓存：只在上次值变化时才重新绘制 */
    int shown_fps = 0;
    int fps_x = 8, fps_y = 8;
    int fps_str_len = 12;   /* "fps 999" 最多 12 字符宽 */

    while (running) {
        /* ── 等待到目标帧绝对时间 ──
         * clock_nanosleep(ABSTIME) 被信号中断也不会提前返回，
         * 目标已过时立即返回 0。这是帧率稳定的关键。 */
        __clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame, NULL);

        struct timespec now;
        __clock_gettime(CLOCK_MONOTONIC, &now);

        /* 推进帧目标 */
        ts_add_ns(&next_frame, interval_ns);

        /* 落后超过 1 帧时重置（跳过追赶，避免爆帧） */
        long behind_us = ts_diff_us(&next_frame, &now);
        if (behind_us < 0) {
            behind_us = -behind_us;
            if ((unsigned long)behind_us > (unsigned long)(interval_ns / 1000))
                next_frame = now;
        }

        /* ── 物理时间步长 ── */
        long dt_us = ts_diff_us(&now, &t_last);
        if (dt_us > 100000) dt_us = 100000;   /* 上限 100ms */
        if (dt_us < 0) dt_us = 0;
        t_last = now;

        /* ── 键盘检测 ── */
        if (poll_stdin()) {
            int c = read_key();
            if (c == 'q' || c == 'Q')
                running = 0;
        }

        /* ── 记下旧位置（擦除用）── */
        int old_bx = bx, old_by = by;

        /* ── 更新球位置（固定点累加，避免 dt_us 太小被截断）── */
        acc_x += vx * dt_us;
        acc_y += vy * dt_us;

        bx += (int)(acc_x / 1000000);
        by += (int)(acc_y / 1000000);
        acc_x %= 1000000;
        acc_y %= 1000000;

        /* 边界反弹 */
        if (bx - r < 0)     { bx = r;     vx = -vx; acc_x = 0; }
        if (bx + r >= w)    { bx = w - r; vx = -vx; acc_x = 0; }
        if (by - r < 0)     { by = r;     vy = -vy; acc_y = 0; }
        if (by + r >= h)    { by = h - r; vy = -vy; acc_y = 0; }

        /* ── 脏矩形绘制 ──
         *    擦除旧球（半径+1 确保覆盖白色边缘）
         *    绘制新球 + 白色描边                           */
        fb_fill_circle(fbp, old_bx, old_by, r + 1, COL_BLACK, ll);
        fb_fill_circle(fbp, bx, by, r, COL_CYAN, ll);
        fb_draw_circle(fbp, bx, by, r, COL_WHITE, ll);

        /* ── 每秒更新 FPS 显示 ── */
        frame_count++;
        total_frames++;
        long fps_elapsed = ts_diff_us(&now, &t_fps);
        if (fps_elapsed >= 1000000 || frame_count >= target_fps * 2) {
            int actual = (int)((long)frame_count * 1000000L / fps_elapsed);
            if (actual < 1) actual = 1;
            if (actual > 999) actual = 999;

            if (actual > max_fps_seen) max_fps_seen = actual;
            if (actual < min_fps_seen) min_fps_seen = actual;

            /* 只在数字变化时重绘（节约带宽/显存写入） */
            if (actual != shown_fps) {
                shown_fps = actual;
                /* 格式化缓冲区：itoa 简单反转 */
                char buf[16];
                int val = actual, p = 0;
                if (val == 0) { buf[p++] = '0'; }
                else {
                    char tmp[8];
                    int tp = 0;
                    while (val > 0) {
                        tmp[tp++] = '0' + (val % 10);
                        val /= 10;
                    }
                    while (tp > 0) buf[p++] = tmp[--tp];
                }
                buf[p] = '\0';

                clear_fps_str(fbp, fps_x, fps_y, fps_str_len, ll);
                draw_fps_str(fbp, fps_x, fps_y, buf, COL_GREEN, ll);
            }

            frame_count = 0;
            t_fps = now;
        }

        /* ── 安全超时 ── */
        if (now.tv_sec - t_start.tv_sec >= MAX_SEC)
            running = 0;
    }

    /* ── 清理 ── */
    if (graphics_mode) {
        { int km = KD_TEXT; __ioctl(0, KDSETMODE, &km); }
        tlibc_restore_term(0);
    }

    __munmap(fbp, screensize);
    __close(fd);

    /* ── 打印运行统计 ── */
    struct timespec t_end;
    __clock_gettime(CLOCK_MONOTONIC, &t_end);
    long total_us = ts_diff_us(&t_end, &t_start);
    if (total_us <= 0) total_us = 1;
    int avg_fps = (int)((long)total_frames * 1000000L / total_us);
    if (avg_fps < 1) avg_fps = 1;

    /* 手动格式化耗时（秒.毫秒），避免依赖 %f 对 64 位整数的 double 精度 */
    long sec = total_us / 1000000;
    long frac = (total_us % 1000000) / 10000;  /* 厘秒（百分位） */
    __printf("\nfb_ball:  %d frames in %ld.%02lds  avg %d fps  [%d – %d]\n",
             total_frames, sec, frac,
             avg_fps, min_fps_seen, max_fps_seen);

    return 0;
}
