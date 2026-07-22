/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * evdev_kbd — TTY 图形模式下的 evdev 键盘输入抽象
 *
 * 机制：扫描 /dev/input/event[0..31]，通过 ioctl 给每个设备打分
 *       （按键数 + 总线类型 + 名称），选评分最高的键盘。
 *       然后从该设备读取 struct input_event。
 * 系统调用：openat, ioctl(EVIOCGBIT/EVIOCGID/EVIOCGNAME), read, fcntl, close
 *
 * 本模块被编译到 tlibc.a 中，应用代码链接即可使用。
 *
 * 索引：
 *   evdev_kbd_open     扫描所有 evdev 设备 → 评分选最佳键盘 → 打开
 *     keyboard_score    给 fd 打键盘分（按键数 + 总线 + 名称）
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

/* ── 给 fd 的键盘度打分（0=不是键盘，分越高越像真键盘）──
 *
 * 评分规则：
 *   必须项：EV_KEY + 按键数 ≥ 阈值（无 REL/ABS 时 20，有则 100）
 *   基础分 = 按键数
 *   总线加分：USB/Bluetooth +300（外接键盘，通常兼容最好）
 *            I2C        +50（内置键盘，一般可用）
 *            PS/2       -100（部分机器图形模式可能无 evdev 事件）
 *            ACPI       -500（电源按钮/功能键，不是真键盘）
 *   名称加分："Keyboard" 字样 +100
 *   名称减分："extra"/"Radio" -300（功能键/无线控制，不是键盘）
 *             "Mouse"       -200（游戏鼠标上的按键接口）
 */
static int keyboard_score(int fd, char *name_out, int name_sz)
{
    unsigned char evbits[8] = {0};

    /* 读事件类型能力位图 */
    if (__ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return 0;

    /* 必须有按键事件能力 */
    if (!test_bit(EV_KEY, evbits, sizeof(evbits)))
        return 0;

    int has_rel = test_bit(EV_REL, evbits, sizeof(evbits));
    int has_abs = test_bit(EV_ABS, evbits, sizeof(evbits));
    int threshold = (has_rel || has_abs) ? 100 : 20;

    /* 统计按键数 */
    unsigned char keybits[128] = {0};
    int count = 0;
    if (__ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0) {
        for (int i = 0; i < 128 * 8; i++) {
            if (test_bit(i, keybits, sizeof(keybits)))
                count++;
        }
    }
    if (count < threshold) return 0;

    /* ── 基础分 = 按键数 ── */
    int score = count;

    /* ── 总线类型 ── */
    {
        struct input_id id;
        if (__ioctl(fd, EVIOCGID, &id) >= 0) {
            switch (id.bustype) {
            case 0x03: score += 300; break;   /* USB — 外接键盘，优先 */
            case 0x04: score += 300; break;   /* Bluetooth */
            case 0x11: score += 50;  break;   /* I2C — 内置键盘 */
            case 0x10: score -= 100; break;   /* i8042 PS/2 */
            case 0x19: score -= 500; break;   /* ACPI — 按钮，非键盘 */
            }
        }
    }

    /* ── 设备名 ── */
    {
        char name[128] = {0};
        if (__ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
            /* "Keyboard" → 加分 */
            for (int i = 0; name[i]; i++) {
                if ((name[i]|32)=='k' && (name[i+1]|32)=='e' &&
                    (name[i+2]|32)=='y' && (name[i+3]|32)=='b' &&
                    (name[i+4]|32)=='o' && (name[i+5]|32)=='a' &&
                    (name[i+6]|32)=='r' && (name[i+7]|32)=='d') {
                    score += 100;
                    break;
                }
            }
            /* "extra" → 不是真键盘 */
            for (int i = 0; name[i]; i++) {
                if ((name[i]|32)=='e' && (name[i+1]|32)=='x' &&
                    (name[i+2]|32)=='t' && (name[i+3]|32)=='r' &&
                    (name[i+4]|32)=='a') {
                    score -= 300;
                    break;
                }
            }
            /* "Mouse" → 游戏鼠标上的键盘接口 */
            for (int i = 0; name[i]; i++) {
                if ((name[i]|32)=='m' && (name[i+1]|32)=='o' &&
                    (name[i+2]|32)=='u' && (name[i+3]|32)=='s' &&
                    (name[i+4]|32)=='e') {
                    score -= 200;
                    break;
                }
            }
            /* 传出名称 */
            if (name_out) {
                int n = 0;
                while (name[n] && n < name_sz - 1) { name_out[n] = name[n]; n++; }
                name_out[n] = '\0';
            }
        }
    }

    return score;
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

    /* 扫描 /dev/input/event[0..MAX_EVENT_DEV) 选评分最高的键盘 */
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

            char tmp_name[128];
            int score = keyboard_score(fd, tmp_name, sizeof(tmp_name));
            if (score > best_score) {
                best_score = score;
                best_fd = fd;
                /* 复制名称 */
                int n = 0;
                while (tmp_name[n] && n < (int)sizeof(best_name) - 1) {
                    best_name[n] = tmp_name[n];
                    n++;
                }
                best_name[n] = '\0';
            } else {
                __close(fd);
            }
        }

        if (best_fd >= 0) {
            kbd->fd = best_fd;
            /* 复制名称到 kbd 结构 */
            int n = 0;
            while (best_name[n] && n < (int)sizeof(kbd->name) - 1) {
                kbd->name[n] = best_name[n];
                n++;
            }
            kbd->name[n] = '\0';

            /* 设为非阻塞 */
            __fcntl(best_fd, F_SETFL, O_NONBLOCK);
        }
    }

    if (kbd->fd < 0) {
        tlibc_free(kbd);
        return NULL;
    }

    return kbd;
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
