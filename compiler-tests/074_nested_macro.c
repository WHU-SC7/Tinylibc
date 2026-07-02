/*
 * 074_nested_macro.c — 宏嵌套展开
 *
 * 验证：宏嵌套展开和递归保护
 * 预期：退出码 0
 */

#define ADD(a, b) ((a) + (b))
#define MUL(a, b) ((a) * (b))
#define IDENTITY(x) x

int main(void) {
    int x;

    x = ADD(MUL(2, 3), ADD(4, 5));
    if (x != 15) return 1;  /* (6)+(9) = 15 */

    /* 自引用宏（递归保护应防止无限展开） */
    x = IDENTITY(IDENTITY(42));
    if (x != 42) return 2;

    return 0;
}
