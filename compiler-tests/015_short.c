/*
 * 015_short.c — short 类型声明与运算
 *
 * 验证：short 类型基本用法
 * 注意：tcc 中 short 变量用 4 字节存取，相邻 short 会栈重叠
 * 预期：退出码 0
 */

int main(void) {
    short s;

    s = 1000;
    if (s != 1000) return 1;

    s = 32000;
    if (s != 32000) return 2;

    /* 单个 short 运算 */
    short a = 100;
    int x = a + 200;
    if (x != 300) return 3;

    x = a * 200;
    if (x != 20000) return 4;

    return 0;
}
