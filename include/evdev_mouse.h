/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * evdev_mouse — TTY 图形模式下的 evdev 鼠标输入抽象
 *
 * 在 KD_GRAPHICS 模式下，标准 TTY 输入通道（stdin）不可用。
 * 本模块通过直接读取 /dev/input/event* 设备来获取鼠标事件。
 *
 * 使用流程：
 *   struct evdev_mouse *mouse = evdev_mouse_open(800, 600);
 *   ...
 *   struct evdev_mouse_state st;
 *   evdev_mouse_read(mouse, &st);
 *   if (st.updated) { draw_cursor(st.x, st.y, st.buttons); }
 *   ...
 *   evdev_mouse_close(mouse);
 *
 * 配合 poll/select：
 *   int fd = evdev_mouse_fd(mouse);
 *   poll(&(struct pollfd){fd, POLLIN}, 1, -1);
 *   evdev_mouse_read(mouse, &state);
 */

#ifndef EVDEV_MOUSE_H
#define EVDEV_MOUSE_H

#include "tlibc_types.h"

/* ── 不透明句柄 ── */
struct evdev_mouse;

/* ── 鼠标状态（每次 read 填充此结构） ── */

struct evdev_mouse_state {
    int x;           /* 绝对 X 位置（像素，被 clamp 到 0～max_x-1） */
    int y;           /* 绝对 Y 位置（像素，被 clamp 到 0～max_y-1） */
    int buttons;     /* 位掩码：bit0=左键，bit1=右键，bit2=中键 */
    int wheel;       /* 本次 poll 周期内的滚轮累计值（正=上滚） */
    int updated;     /* 非 0 表示状态有变化 */
};

/*
 * 自动发现并打开系统中第一个鼠标类 evdev 设备。
 * max_x / max_y 用于将鼠标相对移动转换为屏幕绝对坐标并 clamp。
 * 扫描 /dev/input/event[0..31]，选取有 EV_REL + REL_X/REL_Y 能力的设备。
 * 以 O_RDONLY | O_NONBLOCK 模式打开。
 * 返回句柄（失败返回 NULL）。
 */
struct evdev_mouse *evdev_mouse_open(int max_x, int max_y);

/*
 * 获取底层 fd，用于 poll/select/epoll 等待可读事件。
 * 返回 -1 表示无效句柄。
 */
int evdev_mouse_fd(struct evdev_mouse *mouse);

/*
 * 读取所有待处理的鼠标事件，累积到内部状态。
 * 用当前累积值填充 state（坐标已经 clamp 到 max_x/max_y）。
 * state.updated 指示自上次调用以来是否有任何变化。
 * 返回 1 如果有更新，0 如果无新事件。
 */
int evdev_mouse_read(struct evdev_mouse *mouse, struct evdev_mouse_state *state);

/* 关闭设备并释放句柄 */
void evdev_mouse_close(struct evdev_mouse *mouse);

/* 获取设备名称字符串 */
const char *evdev_mouse_name(struct evdev_mouse *mouse);

/* 重置鼠标位置到屏幕中心 */
void evdev_mouse_recenter(struct evdev_mouse *mouse);

#endif /* EVDEV_MOUSE_H */
