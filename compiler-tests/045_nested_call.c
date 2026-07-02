/*
 * 045_nested_call.c — 嵌套函数调用
 *
 * 验证：多层嵌套函数调用
 * 预期：退出码 0
 */

int inc(int x) { return x + 1; }
int dec(int x) { return x - 1; }
int twice(int x) { return x * 2; }

int main(void) {
    int x;

    x = inc(dec(5));
    if (x != 5) return 1;  /* 5-1+1 = 5 */

    x = twice(inc(twice(3)));
    if (x != 14) return 2; /* ((3*2)+1)*2 = 14 */

    x = inc(inc(inc(inc(0))));
    if (x != 4) return 3;

    return 0;
}
