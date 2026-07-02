/*
 * 016_long.c — long 类型声明与运算
 *
 * 验证：long 64 位整数的基本运算
 * 注意：tcc 可能发生 32 位截断（已知限制）
 * 预期：退出码 0（若截断则 FAIL）
 */

int main(void) {
    long x;

    /* 32 位范围内 — 应通过 */
    x = 100000;
    if (x != 100000) return 1;

    /* 超出 32 位 — tcc 可能截断高位 */
    long y = 0x100000001L;  /* 略大于 2^32 */
    /* 正确的行为：y == 0x100000001 */
    /* tcc 已知 bug：可能 y == 1（高位丢失） */
    if (y != 0x100000001L) return 2;

    /* long 乘法 */
    long a = 100000L, b = 200000L;
    long c = a * b;
    if (c != 20000000000L) return 3;  /* 2e10 */

    return 0;
}
