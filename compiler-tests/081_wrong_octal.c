/*
 * 081_wrong_octal.c — 八进制字面量（已知 bug）
 *
 * 验证：以 0 开头的八进制常量
 * 已知 bug：tcc 不支持八进制，0777 被解析为 0 然后 777
 * 预期：此测试在 tcc 上 FAIL
 */

int main(void) {
    /* 八进制 0777 = 十进制 511 */
    /* tcc bug: 0777 解析为 0 和 777 → 语法错误或错误值 */
    int x = 0777;

    if (x != 511) return 1;  /* tcc 上 FAIL */

    int y = 010;
    if (y != 8) return 2;    /* tcc 上 FAIL */

    return 0;
}
