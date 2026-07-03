/*
 * 036_string_literal.c — 字符串字面量
 *
 * 验证：字符串字面量声明和池分配
 * 预期：退出码 0
 */

int main(void) {
    char *s = "Hello, World!";

    if (s[0] != 'H')  return 1;
    if (s[4] != 'o')  return 2;
    if (s[7] != 'W')  return 3;
    if (s[11] != 'd') return 4;
    if (s[12] != '!') return 5;
    if (s[13] != '\0') return 6;

    /* 字符串长度（不含 null）*/
    int len = 0;
    while (s[len] != '\0')
        len = len + 1;
    if (len != 13) return 7;

    /* 相邻字符串拼接 */
    char *t = "Hello" ", " "World!";
    if (t[0]  != 'H') return 8;
    if (t[7]  != 'W') return 9;
    if (t[12] != '!') return 10;

    return 0;
}
