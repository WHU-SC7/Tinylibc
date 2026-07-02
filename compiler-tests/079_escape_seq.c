/*
 * 079_escape_seq.c — 转义字符序列
 *
 * 验证：字符和字符串中的转义序列
 * 预期：退出码 0
 */

int main(void) {
    /* 字符转义 */
    if ('\0' != 0)  return 1;
    if ('\n' != 10) return 2;
    if ('\t' != 9)  return 3;
    if ('\\' != 92) return 4;
    if ('\'' != 39) return 5;
    if ('\"' != 34) return 6;

    /* 十六进制转义 */
    if ('\x41' != 65) return 7;  /* 'A' */
    if ('\x1B' != 27) return 8;  /* ESC */

    /* 八进制转义 */
    if ('\101' != 65) return 9;  /* 'A' */

    return 0;
}
