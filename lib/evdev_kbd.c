/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * evdev_kbd — TTY 图形模式下的 evdev 键盘输入抽象
 *
 * 机制：扫描 /dev/input/event[0..31]，通过 ioctl 识别纯键盘设备，
 *       然后从该设备读取 struct input_event。
 * 系统调用：openat, ioctl(EVIOCGBIT/EVIOCGNAME), read, fcntl, close
 *
 * 本模块被编译到 tlibc.a 中，应用代码链接即可使用。
 *
 * 索引：
 *   evdev_kbd_open     扫描所有 evdev 设备 → 找到第一个键盘 → 打开
 *     is_keyboard       用 EVIOCGBIT 检查设备能力位图
 *   evdev_kbd_fd       返回底层 fd，供 poll/select 使用
 *   evdev_kbd_read     从设备读取 struct input_event → 提取 KEY_* 编码
 *   evdev_kbd_close    关闭 fd 并释放内存
 *   evdev_kbd_name     返回设备名称
 */

#include "core.h"
#include "fcntl.h"
#include "errno.h"
#include "linux_input.h"
#include "evdev_kbd.h"

/* ── 扫描上限 ── */
#define MAX_EVENT_DEV  32

/* ── 内部结构 ── */
struct evdev_kbd {
    int fd;
    char name[128];
};

/* ── 判断 fd 是否为真正的键盘设备 ── */
static int is_keyboard(int fd)
{
    unsigned char evbits[8] = {0};

    /* 读事件类型能力位图 */
    if (__ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return 0;

    /* 必须有按键事件能力 */
    if (!test_bit(EV_KEY, evbits, sizeof(evbits)))
        return 0;

    /* 有 REL → 鼠标，排除 */
    if (test_bit(EV_REL, evbits, sizeof(evbits)))
        return 0;

    /* 有 ABS → 触摸板/触摸屏，排除 */
    if (test_bit(EV_ABS, evbits, sizeof(evbits)))
        return 0;

    /*
     * 检查按键数量：真正的键盘通常有 100+ 个按键，
     * 而电源按钮/睡眠按钮等设备只有 1-2 个。
     * 阈值设在 20 以排除这些非键盘设备。
     */
    {
        unsigned char keybits[128] = {0};
        int len = sizeof(keybits);
        if (__ioctl(fd, EVIOCGBIT(EV_KEY, len), keybits) >= 0) {
            int count = 0;
            for (int i = 0; i < len * 8; i++) {
                if (test_bit(i, keybits, len))
                    count++;
            }
            if (count < 20)
                return 0;
        }
    }

    return 1;   /* 真正的键盘 */
}

/*
 * 拼接 "/dev/input/event<N>" 到 buf（最大 32 字节），
 * 避免依赖 snprintf 无法 inline 的问题。
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

struct evdev_kbd *evdev_kbd_open(void)
{
    struct evdev_kbd *kbd;

    kbd = (struct evdev_kbd *)tlibc_malloc(sizeof(struct evdev_kbd));
    if (!kbd) return NULL;

    kbd->fd = -1;
    kbd->name[0] = '\0';

    /* 扫描 /dev/input/event[0..MAX_EVENT_DEV) */
    for (int i = 0; i < MAX_EVENT_DEV; i++) {
        char path[32];
        make_event_path(path, i);

        int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
        if (fd < 0)
            continue;

        if (is_keyboard(fd)) {
            kbd->fd = fd;

            /* 读设备名字 */
            if (__ioctl(fd, EVIOCGNAME(sizeof(kbd->name)), kbd->name) < 0)
                kbd->name[0] = '\0';
            kbd->name[sizeof(kbd->name) - 1] = '\0';

            /* 设为非阻塞 */
            __fcntl(fd, F_SETFL, O_NONBLOCK);

            return kbd;
        }

        __close(fd);
    }

    /* 没找到键盘设备 */
    tlibc_free(kbd);
    return NULL;
}

int evdev_kbd_fd(struct evdev_kbd *kbd)
{
    if (!kbd) return -1;
    return kbd->fd;
}

int evdev_kbd_read(struct evdev_kbd *kbd, int *type_out)
{
    struct input_event ev = {0};
    int n;

    if (!kbd || kbd->fd < 0)
        return 0;

    n = __read(kbd->fd, &ev, sizeof(ev));

    /* 非阻塞无数据或部分读取都算无事件 */
    if (n != (int)sizeof(ev))
        return 0;

    /* 只返回 EV_KEY 事件 */
    if (ev.type != EV_KEY)
        return 0;

    if (type_out)
        *type_out = ev.value;

    return (int)ev.code;
}

void evdev_kbd_close(struct evdev_kbd *kbd)
{
    if (!kbd) return;
    if (kbd->fd >= 0)
        __close(kbd->fd);
    tlibc_free(kbd);
}

const char *evdev_kbd_name(struct evdev_kbd *kbd)
{
    if (!kbd) return "";
    return kbd->name;
}
