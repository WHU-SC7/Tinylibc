/*
 * 001_mem_compile.c — 测试 tcc 编译 mem.c 中的代码模式
 *
 * 验证：sizeof / 指针偏移 / 类型转换。
 * 只使用基本的赋值和比较，不依赖复杂的指针运算。
 * EXPECT: 0
 */

int main(void) {
    /* sizeof 常量 */
    unsigned long total = 100 + sizeof(unsigned long);
    if (total != 108) return 1;

    /* 简单指针读写 */
    char buf[32];
    unsigned long *p = (unsigned long*)buf;
    *p = 42;
    if (*p != 42) return 2;

    /* 地址偏移 (char*) + N */
    char *cp = buf;
    cp = cp + 8;
    unsigned long *q = (unsigned long*)cp;
    q = q - 1;
    if (*q != 42) return 3;

    /* 大常数移位 */
    unsigned long x = 0xFFFF000000000000UL;
    if ((x >> 48) != 0xFFFFUL) return 4;

    return 0;
}
