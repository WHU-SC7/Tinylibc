#ifndef __TERMINAL_ESC_H
#define __TERMINAL_ESC_H

/* ANSI / VT100 终端转义序列（来自 tlibc_ioctl.h） */

#define ESC             "\033"
#define CSI             ESC "["   /* Control Sequence Introducer */

/* ── 光标移动 ── */
#define CURSOR_HOME     CSI "H"              /* 移动到左上角 (1,1) */
#define CURSOR_UP(n)    CSI #n "A"           /* 上移 n 行 */
#define CURSOR_DOWN(n)  CSI #n "B"           /* 下移 n 行 */
#define CURSOR_RIGHT(n) CSI #n "C"           /* 右移 n 列 */
#define CURSOR_LEFT(n)  CSI #n "D"           /* 左移 n 列 */
#define CURSOR_POS(row, col) CSI #row ";" #col "H"  /* 移到指定位置 */
#define CURSOR_START_LINE CSI "1G"           /* 到当前行第 1 列 */

/* ── 光标显示 ── */
#define CURSOR_HIDE     CSI "?25l"
#define CURSOR_SHOW     CSI "?25h"

/* ── 清屏 ── */
#define CLEAR_SCREEN        CSI "2J"         /* 整个屏幕 */
#define CLEAR_FROM_CURSOR   CSI "0J"         /* 光标到末尾 */
#define CLEAR_TO_CURSOR     CSI "1J"         /* 开始到光标 */
#define CLEAR_LINE          CSI "2K"         /* 整行 */

/* ── 替代屏幕（Alternate Screen） ── */
#define ALT_SCREEN_ON   ESC "[?1049h"
#define ALT_SCREEN_OFF  ESC "[?1049l"

#endif /* __TERMINAL_ESC_H */
