/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * fb_evdev_info — 列出 evdev 输入设备信息
 *
 * 机制：遍历 /dev/input/event[0..31]，open → ioctl 查询 → 打印名称/ID/能力
 * 系统调用：openat, ioctl, readlink, close
 *
 * 用法：
 *   fb_evdev_info                # 列出所有 evdev 设备
 *   fb_evdev_info 3              # 只查看 event3
 *   fb_evdev_info -v             # 显示详细能力位图
 *   fb_evdev_info 3 -v           # event3 + 详细
 *
 * 注意：
 *   - 需要 /dev/input/event* 的读权限（input 组或 root）
 *   - Wayland 桌面直接运行也可用，但设备可能被合成器占用
 *
 * 退出状态：
 *   0  成功
 *   1  /dev/input 不可访问
 */

/*
 * 索引：
 *   main                 入口：解析参数 → 扫描设备 → 打印信息
 *     print_device_info  打开单设备 → ioctl 查询 → 格式化输出
 *     classify_device    根据能力位图判断设备类型
 *     print_key_names    列出键盘设备支持的常用键
 */

#include "core.h"
#include "fcntl.h"
#include "linux_input.h"

/* snprintf 由 lib/stdio/snprintf.c 提供 */
extern int snprintf(char *str, unsigned long size, const char *fmt, ...);

/* ── 常量 ── */

#define MAX_EVENT_DEV  32

/* ── 全局参数 ── */

static int g_verbose = 0;
static int g_single   = -1;  /* <0 表示扫描全部 */

/* ── 工具函数 ── */

static const char *bus_name(uint16_t bustype)
{
    switch (bustype) {
    case 0x00: return "PCI";
    case 0x01: return "ISA";
    case 0x02: return "USB";
    case 0x03: return "HIL";
    case 0x04: return "Bluetooth";
    case 0x05: return "Virtual";
    case 0x06: return "ISA (PnP)";
    case 0x10: return "i8042 (PS/2)";
    case 0x11: return "I2C";
    case 0x19: return "ACPI";
    default:   return "Unknown";
    }
}

/* 简写能力类型名 */
static const char *ev_type_name(uint16_t type)
{
    switch (type) {
    case EV_KEY: return "KEY";
    case EV_REL: return "REL";
    case EV_ABS: return "ABS";
    case EV_MSC: return "MSC";
    case EV_SW:  return "SW";
    case EV_LED: return "LED";
    case EV_SND: return "SND";
    case EV_REP: return "REP";
    case EV_FF:  return "FF";
    default:     return "?";
    }
}

static const char *classify_device(const unsigned char *evbits, int nbytes,
                                   int key_count)
{
    int has_key = test_bit(EV_KEY, evbits, nbytes);
    int has_rel = test_bit(EV_REL, evbits, nbytes);
    int has_abs = test_bit(EV_ABS, evbits, nbytes);

    /* 按键数超过 80 的设备一定是键盘，即使它有 REL/ABS（游戏键盘等） */
    if (has_key && key_count >= 80) return "Keyboard";
    /* 鼠标 */
    if (has_key && has_rel && !has_abs) return "Mouse";
    /* 触摸板（同时有 REL + ABS + KEY） */
    if (has_key && has_abs && has_rel)  return "Touchpad";
    /* 触摸屏 */
    if (has_key && has_abs && !has_rel) return "Touchscreen";
    /* 纯按键设备（键盘/电源按钮等） */
    if (has_key && !has_rel && !has_abs) return "Keyboard";
    /* 开关 */
    if (test_bit(EV_SW, evbits, nbytes)) return "Switch";
    return "Other";
}

/* 简化键名：字母/特殊键用短名 */
static const char *key_short_name(int code)
{
    if (code >= KEY_A && code <= KEY_Z)
        return (const char[]){'A' + (code - KEY_A), 0};
    if (code >= KEY_F1 && code <= KEY_F12) {
        static char buf[4];
        int n = code - KEY_F1 + 1;
        buf[0] = 'F';
        if (n >= 10) { buf[1] = '0' + n / 10; buf[2] = '0' + n % 10; buf[3] = 0; }
        else         { buf[1] = '0' + n; buf[2] = 0; }
        return buf;
    }
    switch (code) {
    case KEY_ESC:       return "Esc";
    case KEY_TAB:       return "Tab";
    case KEY_ENTER:     return "Enter";
    case KEY_BACKSPACE: return "BS";
    case KEY_SPACE:     return "Spc";
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
    default:            return 0;
    }
}

static void print_key_names(int fd, int verbose)
{
    unsigned char keybits[128] = {0};
    int len = sizeof(keybits);

    if (__ioctl(fd, EVIOCGBIT(EV_KEY, len), keybits) < 0)
        return;

    /* 统计支持的键数 */
    int count = 0;
    for (int i = 0; i < len * 8; i++) {
        if (test_bit(i, keybits, len))
            count++;
    }

    __printf("    Key count: %d\n", count);

    if (!verbose) return;

    /* 列出已知键名 */
    __printf("    Keys: ");
    int n = 0;
    for (int i = 0; i < len * 8; i++) {
        if (!test_bit(i, keybits, len)) continue;
        const char *name = key_short_name(i);
        if (name) {
            if (n++ > 0) __printf(", ");
            __printf("%s", name);
            if (n % 12 == 0) __printf("\n           ");
        }
    }
    if (n > 0) __printf("\n");
}

/* ── 打印单设备信息 ── */

static int print_device_info(int num)
{
    char path[32];
    snprintf(path, sizeof(path), "/dev/input/event%d", num);

    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0)
        return 0;  /* 设备不存在或无权限 */

    /* ── 基本信息 ── */
    __printf("── event%d ──\n", num);

    /* 设备名 */
    char name[128] = {0};
    if (__ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 && name[0])
        __printf("  Name:   %s\n", name);
    else
        __printf("  Name:   (unknown)\n");

    /* 物理路径 */
    char phys[64] = {0};
    if (__ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys) >= 0 && phys[0])
        __printf("  Phys:   %s\n", phys);

    /* 设备 ID */
    struct input_id id;
    if (__ioctl(fd, EVIOCGID, &id) >= 0) {
        __printf("  Bus:    %s (0x%04x)\n", bus_name(id.bustype), id.bustype);
        __printf("  Vendor: 0x%04x  Product: 0x%04x  Version: 0x%04x\n",
                 id.vendor, id.product, id.version);
    }

    /* ── 能力位图（事件类型） ── */
    unsigned char evbits[8] = {0};
    if (__ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) >= 0) {
        __printf("  Types:  ");
        int first = 1;
        for (int i = 0; i < 8 * 8; i++) {
            if (test_bit(i, evbits, sizeof(evbits))) {
                if (!first) __printf(", ");
                __printf("%s", ev_type_name(i));
                first = 0;
            }
        }
        __printf("\n");

        /* 统计按键数（用于辅助分类） */
        int key_count = 0;
        if (test_bit(EV_KEY, evbits, sizeof(evbits))) {
            unsigned char keybits[128] = {0};
            if (__ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0) {
                for (int i = 0; i < 128 * 8; i++) {
                    if (test_bit(i, keybits, sizeof(keybits)))
                        key_count++;
                }
            }
        }

        /* 分类 */
        __printf("  Class:  %s\n", classify_device(evbits, sizeof(evbits), key_count));
    }

    /* 如果是键盘/触摸板等有 EV_KEY 的设备，列出键位 */
    if (test_bit(EV_KEY, evbits, sizeof(evbits)))
        print_key_names(fd, g_verbose);

    __close(fd);
    return 1;
}

/* ── 入口 ── */

int main(int argc, char **argv)
{
    int start = 0, end = MAX_EVENT_DEV - 1;

    /* 解析参数 */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'v' && argv[i][2] == 0) {
                g_verbose = 1;
            } else {
                __fprintf(2, "Usage: %s [-v] [eventN]\n", argv[0]);
                return 1;
            }
        } else {
            /* 数字参数：设备号 */
            int n = 0;
            char *p = argv[i];
            /* 跳过 "event" 前缀 */
            if (p[0] == 'e' && p[1] == 'v' && p[2] == 'e' &&
                p[3] == 'n' && p[4] == 't')
                p += 5;
            while (*p >= '0' && *p <= '9')
                n = n * 10 + (*p++ - '0');
            if (*p == 0) {
                g_single = n;
                start = n;
                end = n;
            }
        }
    }

    /* 检查 /dev/input 是否能访问 */
    int dir_fd = __openat(AT_FDCWD, "/dev/input", O_RDONLY, 0);
    if (dir_fd < 0) {
        __fprintf(2, "fb_evdev_info: cannot open /dev/input "
                     "(need read permission / input group)\n");
        __fprintf(2, "  Try: sudo adduser $USER input\n");
        return 1;
    }
    __close(dir_fd);

    __printf("Input devices found on this system:\n\n");

    int any = 0;
    for (int i = start; i <= end; i++) {
        if (print_device_info(i))
            any = 1;
    }

    __printf("\nTotal: %d device(s)\n"
             "Tip: use fb_evdev_info -v for extended key listing\n"
             "Tip: fb_evdev_info <N> to show only eventN\n",
             any);
    return 0;
}
