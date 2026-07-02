/*
 * 033_wrong_deref.c — 指针解引用（已知 bug）
 *
 * 验证：*ptr 读取 int 指针指向的全部 4 字节
 * 已知 bug：tcc 只读 1 字节并符号扩展
 * 预期：此测试在 tcc 上 FAIL
 */

int main(void) {
    int x = 0x12345678;
    int *p = &x;
    int v = *p;

    /* 标准 C: v == 0x12345678 */
    /* tcc bug: 只读 1 字节 → v == 0x78（小端最低字节） */
    if (v != 0x12345678) return 1;

    /* 通过指针写入 */
    *p = 0xAABBCCDD;
    if (x != 0xAABBCCDD) return 2;

    return 0;
}
