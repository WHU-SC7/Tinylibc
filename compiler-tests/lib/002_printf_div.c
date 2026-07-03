/*
 * 002_printf_div.c — 测试 printf.c 中的除法/取模模式
 *
 * 验证：取模 / 复合除法 / long long 截断
 * 注意：负数取模结果依赖平台（C99 结果与除号一致），这里只测试正数
 * EXPECT: 0
 */

static int do_mod(long num) {
    return num % 10;
}

static long do_div(long num) {
    num /= 10;
    return num;
}

int main(void) {
    if (do_mod(123) != 3) return 1;
    if (do_mod(0) != 0) return 2;

    if (do_div(100) != 10) return 4;
    if (do_div(5) != 0) return 5;

    /* long long → long 截断 */
    long long big = 1234567890123LL;
    long trunc = (long)big;
    if (trunc != 1234567890123L) return 7;

    return 0;
}
