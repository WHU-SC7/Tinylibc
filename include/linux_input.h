/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

#ifndef __LINUX_INPUT_H
#define __LINUX_INPUT_H

/*
 * Linux evdev 接口 — ioctl 命令 + 数据结构
 *
 * 来源：Linux 内核 UAPI <linux/input.h>，精简到本库需要的子集。
 * 结构体布局按 x86_64 自然对齐。
 *
 * 使用流程：
 *   fd = open("/dev/input/event3", O_RDONLY);
 *   struct input_id id;
 *   ioctl(fd, EVIOCGID, &id);
 *   char name[256];
 *   ioctl(fd, EVIOCGNAME(sizeof(name)), name);
 *
 * 读取事件：
 *   struct input_event ev;
 *   read(fd, &ev, sizeof(ev));
 *   if (ev.type == EV_KEY && ev.value == 1) { ... }  // 1=press, 2=repeat, 0=release
 *
 * 发现设备：遍历 /dev/input/event* 或解析 /proc/bus/input/devices。
 */

#include "tlibc_types.h"

/* ── ioctl 宏辅助 ── */

#define _IOC_NRSHIFT   0
#define _IOC_TYPESHIFT 8
#define _IOC_SIZESHIFT 16
#define _IOC_DIRSHIFT  30

#define _IOC_NONE  0U
#define _IOC_WRITE 1U
#define _IOC_READ  2U

#define _IOC(dir, type, nr, size)  \
    (((dir)  << _IOC_DIRSHIFT)  | \
     ((type) << _IOC_TYPESHIFT) | \
     ((nr)   << _IOC_NRSHIFT)  | \
     ((size) << _IOC_SIZESHIFT))

/* ── ioctl 命令 ── */

#define EVIOCGVERSION    _IOC(_IOC_READ,  'E', 0x01, 4)       /* 读驱动版本 (int) */
#define EVIOCGID         _IOC(_IOC_READ,  'E', 0x02, 8)       /* 读设备 ID (struct input_id) */
#define EVIOCGNAME(len)  _IOC(_IOC_READ,  'E', 0x06, len)     /* 读设备名 (char[]) */
#define EVIOCGPHYS(len)  _IOC(_IOC_READ,  'E', 0x07, len)     /* 读物理路径 (char[]) */
#define EVIOCGUNIQ(len)  _IOC(_IOC_READ,  'E', 0x08, len)     /* 读唯一标识 (char[]) */
#define EVIOCGBIT(ev, len) _IOC(_IOC_READ, 'E', 0x20 + (ev), len) /* 读能力位图 */

/* ── 数据结构 ── */

struct input_id {
    uint16_t bustype;
    uint16_t vendor;
    uint16_t product;
    uint16_t version;
};

/*
 * struct input_event — evdev 事件（24 字节固定大小）
 *
 * 内核用 struct timeval（秒+微秒），不考虑 timespec（秒+纳秒）变体。
 * struct timeval 在 x86_64 上：___time64 占 8 字节 + suseconds64_t 占 8 字节。
 * 事件类型/代码/值都是 int32。
 */
struct input_event {
    uint64_t tv_sec;       /* 秒 */
    uint64_t tv_usec;      /* 微秒 */
    uint16_t type;         /* EV_* 事件类型 */
    uint16_t code;         /* 按键/相对轴编码 */
    int32_t  value;        /* 1=按下, 0=松开, 2=重复 */
};

/* ── 事件类型 (ev.type) ── */

#define EV_SYN          0x00    /* 同步标记 */
#define EV_KEY          0x01    /* 按键/开关 */
#define EV_REL          0x02    /* 相对轴（鼠标） */
#define EV_ABS          0x03    /* 绝对轴（触摸板/触摸屏） */
#define EV_MSC          0x04    /* 杂项事件 */
#define EV_SW           0x05    /* 开关 */
#define EV_LED          0x11    /* LED */
#define EV_SND          0x12    /* 声音 */
#define EV_REP          0x14    /* 自动重复参数 */
#define EV_FF           0x15    /* 力反馈 */

/* ── 同步事件 (EV_SYN) code ── */

#define SYN_REPORT      0       /* 事件包结束，应用可处理 */

/* ── 键盘按键 (EV_KEY) 常用编码 ── */

#define KEY_RESERVED    0
#define KEY_ESC         1
#define KEY_1           2
#define KEY_2           3
#define KEY_3           4
#define KEY_4           5
#define KEY_5           6
#define KEY_6           7
#define KEY_7           8
#define KEY_8           9
#define KEY_9           10
#define KEY_0           11
#define KEY_MINUS       12
#define KEY_EQUAL       13
#define KEY_BACKSPACE   14
#define KEY_TAB         15
#define KEY_Q           16
#define KEY_W           17
#define KEY_E           18
#define KEY_R           19
#define KEY_T           20
#define KEY_Y           21
#define KEY_U           22
#define KEY_I           23
#define KEY_O           24
#define KEY_P           25
#define KEY_LEFTBRACE   26      /* [ */
#define KEY_RIGHTBRACE  27      /* ] */
#define KEY_ENTER       28
#define KEY_LEFTCTRL    29
#define KEY_A           30
#define KEY_S           31
#define KEY_D           32
#define KEY_F           33
#define KEY_G           34
#define KEY_H           35
#define KEY_J           36
#define KEY_K           37
#define KEY_L           38
#define KEY_SEMICOLON   39
#define KEY_APOSTROPHE  40
#define KEY_GRAVE       41      /* ` */
#define KEY_LEFTSHIFT   42
#define KEY_BACKSLASH   43
#define KEY_Z           44
#define KEY_X           45
#define KEY_C           46
#define KEY_V           47
#define KEY_B           48
#define KEY_N           49
#define KEY_M           50
#define KEY_COMMA       51
#define KEY_DOT         52
#define KEY_SLASH       53
#define KEY_RIGHTSHIFT  54
#define KEY_KPASTERISK  55      /* * 小键盘 */
#define KEY_LEFTALT     56
#define KEY_SPACE       57
#define KEY_CAPSLOCK    58
#define KEY_F1          59
#define KEY_F2          60
#define KEY_F3          61
#define KEY_F4          62
#define KEY_F5          63
#define KEY_F6          64
#define KEY_F7          65
#define KEY_F8          66
#define KEY_F9          67
#define KEY_F10         68
#define KEY_F11         87
#define KEY_F12         88

#define KEY_RIGHTCTRL   97
#define KEY_RIGHTALT    100

#define KEY_UP          103
#define KEY_DOWN        108
#define KEY_LEFT        105
#define KEY_RIGHT       106

#define KEY_HOME        102
#define KEY_END         107
#define KEY_PAGEUP      104
#define KEY_PAGEDOWN    109
#define KEY_INSERT      110
#define KEY_DELETE      111

/* ── 鼠标按键 (EV_KEY) 编码 (BTN_*) ── */

#define BTN_LEFT        0x110   /* 左键 */
#define BTN_RIGHT       0x111   /* 右键 */
#define BTN_MIDDLE      0x112   /* 中键 */

/* ── 鼠标相对轴 (EV_REL) code ── */

#define REL_X           0
#define REL_Y           1
#define REL_WHEEL       8       /* 垂直滚轮 */

/* ── 工具宏：测试能力位图 ── */

/* 检查设备是否支持某类事件（bitmask 来自 EVIOCGBIT） */
static inline int test_bit(int bit, const unsigned char *bits, int nbytes)
{
    int byte = bit / 8;
    int b = bit % 8;
    if (byte >= nbytes) return 0;
    return (bits[byte] >> b) & 1;
}

#endif /* __LINUX_INPUT_H */
