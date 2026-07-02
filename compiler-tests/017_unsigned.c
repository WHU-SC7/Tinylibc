/*
 * 017_unsigned.c — unsigned 类型声明与运算
 *
 * 验证：unsigned 基本用法（tcc 中无符号语义无差异）
 * 预期：退出码 0
 */

int main(void) {
    unsigned int u;

    u = 100;
    if (u != 100) return 1;

    u = 0xFFFFFFFF;
    /* 按 32 位有符号存储，值应为 -1 或 0xFFFFFFFF */
    if (u != 0xFFFFFFFF) return 2;

    unsigned a = 10, b = 20;
    unsigned c = a + b;
    if (c != 30) return 3;

    return 0;
}
