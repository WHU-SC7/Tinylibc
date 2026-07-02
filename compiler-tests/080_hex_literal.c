/*
 * 080_hex_literal.c — 十六进制字面量
 *
 * 验证：0x 前缀的整数常量
 * 预期：退出码 0
 */

int main(void) {
    if (0x0 != 0)       return 1;
    if (0x10 != 16)     return 2;
    if (0xFF != 255)    return 3;
    if (0x100 != 256)   return 4;
    if (0xAABB != 43707) return 5;
    if (0xDEAD != 57005) return 6;

    /* 十六进制赋值 */
    int x = 0x1A;
    if (x != 26) return 7;

    /* 十六进制运算 */
    if ((0x10 + 0x20) != 0x30) return 8;

    return 0;
}
