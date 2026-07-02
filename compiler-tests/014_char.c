/*
 * 014_char.c — char 类型声明与运算
 *
 * 验证：char 类型的基本读写
 * 注意：tcc 中 char 变量用 4 字节存取，相邻 char 会栈重叠
 * 预期：退出码 0（只测试单个 char 变量）
 */

int main(void) {
    char c;

    c = 'A';
    if (c != 65) return 1;  /* 'A' == 65 */

    c = 100;
    if (c != 100) return 2;

    /* char as local variable (only one at a time, no adjacent char overlap) */
    char x = 42;
    if (x != 42) return 3;

    /* char 作为 int 运算（读入 eax，是 4 字节，但值正确） */
    int test = 'A';
    if (test != 65) return 4;

    return 0;
}
