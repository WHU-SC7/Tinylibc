/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * evdev_kbd — TTY 图形模式下的 evdev 键盘输入抽象
 *
 * 在 KD_GRAPHICS 模式下，标准 TTY 输入通道（stdin）可能不可靠或完全
 * 不可用。本模块通过直接读取 /dev/input/event* 设备来获取键盘事件。
 *
 * 使用流程：
 *   struct evdev_kbd *kbd = evdev_kbd_open();
 *   ...
 *   int type;
 *   int key = evdev_kbd_read(kbd, &type);   // 非阻塞
 *   if (key == KEY_ESC) ...
 *   ...
 *   evdev_kbd_close(kbd);
 *
 * 配合 poll/select：
 *   int fd = evdev_kbd_fd(kbd);
 *   poll(&(struct pollfd){fd, POLLIN}, 1, -1);
 *   key = evdev_kbd_read(kbd, &type);
 */

#ifndef EVDEV_KBD_H
#define EVDEV_KBD_H

#include "tlibc_types.h"

/* ── 不透明句柄 ── */
struct evdev_kbd;

/* ── 事件类型（同 input_event.value） ── */
#define EVDEV_PRESS   1
#define EVDEV_RELEASE 0
#define EVDEV_REPEAT  2

/*
 * 自动发现并打开系统中第一个键盘类 evdev 设备。
 * 扫描 /dev/input/event[0..31]，选取纯按键设备（有 EV_KEY、无 EV_REL/ABS）。
 * 以 O_RDONLY | O_NONBLOCK 模式打开。
 * 返回句柄（失败返回 NULL）。
 */
struct evdev_kbd *evdev_kbd_open(void);

/*
 * 获取底层 fd，用于 poll/select/epoll 等待可读事件。
 * 返回 -1 表示无效句柄。
 */
int evdev_kbd_fd(struct evdev_kbd *kbd);

/*
 * 读取一个键盘事件（非阻塞）。
 * 返回 KEY_* 编码（定义见 linux_input.h），
 * type_out 接收事件类型（EVDEV_PRESS/RELEASE/REPEAT，可传 NULL）。
 * 无可用事件时返回 0（EAGAIN）。
 */
int evdev_kbd_read(struct evdev_kbd *kbd, int *type_out);

/* 关闭设备并释放句柄 */
void evdev_kbd_close(struct evdev_kbd *kbd);

/* 获取设备名称字符串 */
const char *evdev_kbd_name(struct evdev_kbd *kbd);

#endif /* EVDEV_KBD_H */
