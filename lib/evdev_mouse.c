/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * evdev_mouse — TTY 图形模式下的 evdev 鼠标输入抽象
 *
 * 机制：扫描 /dev/input/event[0..31]，通过 ioctl 识别鼠标设备
 *       （有 EV_REL 能力，且 REL_X/REL_Y 有效），
 *       然后从中读取 struct input_event。
 * 系统调用：openat, ioctl(EVIOCGBIT/EVIOCGNAME), read, fcntl, close
 *
 * 鼠标事件处理：
 *   EV_REL → REL_X/REL_Y 为相对位移，REL_WHEEL 为滚轮增量
 *   EV_KEY → BTN_LEFT/RIGHT/MIDDLE 为按键状态
 *   EV_SYN → SYN_REPORT 标记一个完整事件包结束
 *
 * 本模块被编译到 tlibc.a 中，应用代码链接即可使用。
 *
 * 索引：
 *   evdev_mouse_open         扫描所有 evdev 设备 → 找到第一个鼠标 → 打开
 *     is_mouse               用 EVIOCGBIT 检查设备能力位图
 *   evdev_mouse_read         读取累积的鼠标事件 → 更新内部状态
 *   evdev_mouse_close        关闭 fd 并释放内存
 */

#include "core.h"
#include "string.h"
#include "fcntl.h"
#include "errno.h"
#include "linux_input.h"
#include "evdev_mouse.h"

/* ── 扫描上限 ── */
#define MAX_EVENT_DEV  32

/* ── 内部结构 ── */
struct evdev_mouse {
    int fd;
    int max_x;
    int max_y;
    int accum_x;         /* 累积 X（原始像素，未 clamp） */
    int accum_y;         /* 累积 Y（原始像素，未 clamp） */
    int buttons;         /* 当前按键状态 */
    int wheel_accum;     /* 本次 poll 的滚轮累计 */
    int state_updated;   /* 内部状态有变化标志 */
    char name[128];
};

/* ── 判断 fd 是否为鼠标设备，并返回「鼠标度」评分（0=不是鼠标） ──
 *
 * 评分规则：
 *   必须项：有 EV_REL + REL_X + REL_Y
 *   +2  REL_WHEEL（滚轮 → 更像真鼠标）
 *   +1  per 2 BTN 按键（最多 +6）
 *   按键 > 8 个 → 0（键盘/多功能设备）
 */
static int mouse_score(int fd)
{
    unsigned char evbits[8] = {0};
    int score = 0;

    /* 读取事件类型能力位图 */
    if (__ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return 0;

    /* 必须有相对坐标能力（EV_REL） */
    if (!test_bit(EV_REL, evbits, sizeof(evbits)))
        return 0;

    /* 检查 REL_X / REL_Y / REL_WHEEL */
    {
        unsigned char relbits[8] = {0};
        if (__ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relbits)), relbits) < 0)
            return 0;
        if (!test_bit(REL_X, relbits, sizeof(relbits)))
            return 0;
        if (!test_bit(REL_Y, relbits, sizeof(relbits)))
            return 0;
        if (test_bit(REL_WHEEL, relbits, sizeof(relbits)))
            score += 2;     /* 有滚轮 → 更像是真鼠标 */
    }

    /* 统计 BTN 按键数量 */
    {
        unsigned char keybits[128] = {0};
        int len = sizeof(keybits);
        if (__ioctl(fd, EVIOCGBIT(EV_KEY, len), keybits) >= 0) {
            int count = 0;
            for (int i = BTN_LEFT; i < len * 8 && i < BTN_LEFT + 16; i++) {
                if (test_bit(i, keybits, len))
                    count++;
            }
            if (count > 8)
                return 0;   /* 太多按键，可能是键盘/多功能设备 */
            score += count / 2;
            if (score > 8) score = 8;
        }
    }

    return score > 0 ? score : 1;
}

/*
 * 拼接 "/dev/input/event<N>" 到 buf（最大 32 字节）
 */
static void make_event_path(char *buf, int num)
{
    const char *prefix = "/dev/input/event";
    while (*prefix) *buf++ = *prefix++;
    if (num >= 10) *buf++ = '0' + (num / 10);
    *buf++ = '0' + (num % 10);
    *buf = '\0';
}

/* ═══════════════════════════════════════════════
 * 公开 API
 * ═══════════════════════════════════════════════ */

struct evdev_mouse *evdev_mouse_open(int max_x, int max_y)
{
    struct evdev_mouse *mouse;

    mouse = (struct evdev_mouse *)tlibc_malloc(sizeof(struct evdev_mouse));
    if (!mouse) return NULL;

    mouse->fd = -1;
    mouse->max_x = max_x > 0 ? max_x : 1024;
    mouse->max_y = max_y > 0 ? max_y : 768;
    mouse->accum_x = mouse->max_x / 2;
    mouse->accum_y = mouse->max_y / 2;
    mouse->buttons = 0;
    mouse->wheel_accum = 0;
    mouse->state_updated = 0;
    mouse->name[0] = '\0';

    /* 扫描 /dev/input/event[0..MAX_EVENT_DEV)，选评分最高的鼠标 */
    {
        int best_fd = -1;
        int best_score = 0;
        char best_name[128];
        best_name[0] = '\0';

        for (int i = 0; i < MAX_EVENT_DEV; i++) {
            char path[32];
            make_event_path(path, i);

            int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
            if (fd < 0)
                continue;

            int score = mouse_score(fd);
            if (score > best_score) {
                best_score = score;
                best_fd = fd;

                /* 读取设备名称 */
                char tmp_name[128];
                if (__ioctl(fd, EVIOCGNAME(sizeof(tmp_name)), tmp_name) < 0)
                    tmp_name[0] = '\0';
                tmp_name[sizeof(tmp_name) - 1] = '\0';
                strncpy(best_name, tmp_name, sizeof(best_name) - 1);
                best_name[sizeof(best_name) - 1] = '\0';
            } else {
                __close(fd);
            }
        }

        if (best_fd >= 0) {
            mouse->fd = best_fd;
            strncpy(mouse->name, best_name, sizeof(mouse->name) - 1);
            mouse->name[sizeof(mouse->name) - 1] = '\0';

            /* 设为非阻塞 */
            __fcntl(best_fd, F_SETFL, O_NONBLOCK);
        }
    }

    if (mouse->fd < 0) {
        tlibc_free(mouse);
        return NULL;
    }

    return mouse;
}

int evdev_mouse_fd(struct evdev_mouse *mouse)
{
    if (!mouse) return -1;
    return mouse->fd;
}

int evdev_mouse_read(struct evdev_mouse *mouse, struct evdev_mouse_state *state)
{
    struct input_event ev;
    int got_events = 0;

    if (!mouse || mouse->fd < 0) {
        if (state) {
            state->x = 0;
            state->y = 0;
            state->buttons = 0;
            state->wheel = 0;
            state->updated = 0;
        }
        return 0;
    }

    /* 读取所有待处理事件 */
    while (1) {
        int n = __read(mouse->fd, &ev, sizeof(ev));
        if (n != (int)sizeof(ev))
            break;

        got_events = 1;

        switch (ev.type) {
        case EV_REL:
            switch (ev.code) {
            case REL_X:
                mouse->accum_x += ev.value;
                mouse->state_updated = 1;
                break;
            case REL_Y:
                mouse->accum_y += ev.value;
                mouse->state_updated = 1;
                break;
            case REL_WHEEL:
                mouse->wheel_accum += ev.value;
                mouse->state_updated = 1;
                break;
            }
            break;

        case EV_KEY:
            switch (ev.code) {
            case BTN_LEFT:
                if (ev.value) mouse->buttons |= (1 << 0);
                else          mouse->buttons &= ~(1 << 0);
                mouse->state_updated = 1;
                break;
            case BTN_RIGHT:
                if (ev.value) mouse->buttons |= (1 << 1);
                else          mouse->buttons &= ~(1 << 1);
                mouse->state_updated = 1;
                break;
            case BTN_MIDDLE:
                if (ev.value) mouse->buttons |= (1 << 2);
                else          mouse->buttons &= ~(1 << 2);
                mouse->state_updated = 1;
                break;
            }
            break;

        case EV_SYN:
            /* SYN_REPORT 标记事件包结束，我们已处理完一组 */
            break;
        }
    }

    /* clamp 坐标 */
    if (mouse->accum_x < 0) mouse->accum_x = 0;
    if (mouse->accum_x >= mouse->max_x) mouse->accum_x = mouse->max_x - 1;
    if (mouse->accum_y < 0) mouse->accum_y = 0;
    if (mouse->accum_y >= mouse->max_y) mouse->accum_y = mouse->max_y - 1;

    /* 填充输出状态 */
    if (state) {
        state->x = mouse->accum_x;
        state->y = mouse->accum_y;
        state->buttons = mouse->buttons;
        state->wheel = mouse->wheel_accum;
        state->updated = mouse->state_updated || got_events;
    }

    int ret = mouse->state_updated || got_events;

    /* 重置滚轮累计（每次读取后清除） */
    mouse->wheel_accum = 0;
    mouse->state_updated = 0;

    return ret;
}

void evdev_mouse_close(struct evdev_mouse *mouse)
{
    if (!mouse) return;
    if (mouse->fd >= 0)
        __close(mouse->fd);
    tlibc_free(mouse);
}

const char *evdev_mouse_name(struct evdev_mouse *mouse)
{
    if (!mouse) return "";
    return mouse->name;
}

void evdev_mouse_recenter(struct evdev_mouse *mouse)
{
    if (!mouse) return;
    mouse->accum_x = mouse->max_x / 2;
    mouse->accum_y = mouse->max_y / 2;
    mouse->state_updated = 1;
}
