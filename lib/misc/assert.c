/* SPDX-License-Identifier: MIT
 *
 * assert.c — __assert_fail 实现（供 assert.h 使用）
 *
 * 失败时打印断言信息到 stderr 并调用 _exit(-1)。
 */

#include "core.h"
#include "string.h"

void __assert_fail(const char *expr, const char *file, int line, const char *func)
{
    /* 输出: "Assertion failed: <expr>, file <file>, line <line>, function <func>\n" */
    __write(2, "Assertion failed: ", 18);
    __write(2, expr, strlen(expr));

    __write(2, ", file ", 7);
    __write(2, file, strlen(file));
    __write(2, ", line ", 7);

    /* line 是 int，逐位转十进制输出 */
    char lbuf[16];
    int  lpos = 0;
    int  tmp  = line;
    if (tmp == 0) {
        lbuf[lpos++] = '0';
    } else {
        while (tmp > 0) {
            lbuf[lpos++] = '0' + (tmp % 10);
            tmp /= 10;
        }
        /* 反转 */
        for (int i = 0; i < lpos / 2; i++) {
            char t          = lbuf[i];
            lbuf[i]         = lbuf[lpos - 1 - i];
            lbuf[lpos - 1 - i] = t;
        }
    }
    __write(2, lbuf, lpos);

    __write(2, ", function ", 11);
    __write(2, func, strlen(func));
    __write(2, "\n", 1);

    __exit(-1);
    /* unreachable */
    for (;;)
        ;
}
