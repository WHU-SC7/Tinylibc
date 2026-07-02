/*
 * 011_bitwise.c — 位运算符 & | ^ ~ << >>
 *
 * 验证：6 种位运算
 * 预期：退出码 0
 */

int main(void) {
    int a = 0x0F;  /* 0000 1111 */
    int b = 0x33;  /* 0011 0011 */

    /* & */
    if ((a & b) != 0x03) return 1;  /* 0000 0011 */

    /* | */
    if ((a | b) != 0x3F) return 2;  /* 0011 1111 */

    /* ^ */
    if ((a ^ b) != 0x3C) return 3;  /* 0011 1100 */

    /* ~ */
    if ((~a & 0xFF) != 0xF0) return 4;  /* 1111 0000 (mask to 8 bits) */

    /* << */
    if ((a << 2) != 0x3C) return 5;  /* 0011 1100 */
    if ((1 << 4) != 16)   return 6;

    /* >> */
    int c = 0xF0;
    if ((c >> 2) != 0x3C) return 7;  /* 0011 1100 */
    if ((16 >> 2) != 4)   return 8;

    /* 复合 */
    if (((a & b) | (c >> 4)) != 0x0F) return 9;

    return 0;
}
