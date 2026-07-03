/*
 * 006_string.c — 测试 string.c 中的字符串操作模式
 *
 * 验证：字符遍历 / 指针自增 / 按字节拷贝
 * EXPECT: 0
 */

int main(void) {
    char buf[32];

    /* strlen 模式 */
    const char *s = "hello";
    int len = 0;
    while (s[len]) len++;
    if (len != 5) return 1;

    /* strcpy 模式 */
    int i = 0;
    while ((buf[i] = s[i])) i++;
    if (buf[0] != 'h' || buf[4] != 'o') return 2;

    /* strcmp 模式 */
    const char *a = "abc", *b = "abc";
    int diff = 0;
    for (i = 0; ; i++) {
        if (a[i] != b[i]) { diff = 1; break; }
        if (!a[i]) break;
    }
    if (diff != 0) return 3;

    /* 指针步进 */
    const char *str = "hello world";
    const char *p = str;
    while (*p && *p != ' ') p++;
    if (*p != ' ') return 4;
    if (p - str != 5) return 5;

    return 0;
}
