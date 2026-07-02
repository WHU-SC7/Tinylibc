/*
 * 087_block_shadow.c — 嵌套块中的同名变量
 *
 * preproc.c 的 preprocess() 函数中有：
 *   { int fnl = ...; }
 *   int fnl = ...;
 * 两个 fnl 在不同作用域，验证 tcc 能正确处理。
 *
 * EXPECT: 42
 */

int main(void) {
    int x = 10;
    {
        int x = 20;
        if (x != 20) return 1;
    }
    if (x != 10) return 2;
    return 42;
}
