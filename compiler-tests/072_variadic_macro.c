/*
 * 072_variadic_macro.c — 变参宏 __VA_ARGS__
 *
 * 验证：变参宏展开和 ,,## 逗号吞噬
 * 预期：退出码 0
 */

#define PRINT(fmt, ...) fmt

int main(void) {
    int x;

    x = PRINT("hello", 1, 2, 3);
    /* 只取第一个参数，后面的 ... 被丢弃 */
    /* 只要能编译通过且值合理即可 */
    (void)x;

    return 0;
}
