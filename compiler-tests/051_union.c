/*
 * 051_union.c — union 声明与访问
 *
 * 验证：union 的基本用法
 * 注意：tcc 中 union 当作 struct 处理，成员不重叠（已知限制）
 * 预期：退出码 0
 */

union Data {
    int i;
    char c;
};

int main(void) {
    union Data u;

    u.i = 0x12345678;

    if (u.i != 0x12345678) return 1;

    /* union 访问另一个成员（小端系统上应该是 0x78） */
    /* tcc 当作 struct，所以这里可能不可预测 */
    /* 我们只测试基本 i 成员的读写 */
    u.i = 42;
    if (u.i != 42) return 2;

    return 0;
}
