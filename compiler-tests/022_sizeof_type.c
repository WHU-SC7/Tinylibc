/*
 * 022_sizeof_type.c — sizeof 类型查询
 *
 * 验证：sizeof 基本类型的返回值
 * 注意：tcc 中 sizeof(void) 返回 4（非标准），此处不测
 * 预期：退出码 0
 */

int main(void) {
    if (sizeof(int)    != 4) return 1;
    if (sizeof(char)   != 1) return 2;
    if (sizeof(short)  != 2) return 3;
    if (sizeof(long)   != 8) return 4;
    if (sizeof(double) != 8) return 5;

    /* 指针 */
    if (sizeof(int *)    != 8) return 6;
    if (sizeof(char *)   != 8) return 7;

    return 0;
}
