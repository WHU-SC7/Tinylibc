/*
 * 004_printf_fmt.c — 测试 printf.c 中的格式解析模式
 *
 * 验证：位标志 |= / while 数字解析
 * EXPECT: 0
 */

int main(void) {
    int flags = 0;
    flags = flags | 1;
    flags = flags | 2;
    if (!(flags & 1) || !(flags & 2)) return 1;
    if ((flags & 4)) return 2;

    int width = 0;
    const char *p = "123";
    while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
    if (width != 123) return 3;

    return 0;
}
