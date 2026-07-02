/*
 * 090_many_locals.c — 大量局部变量
 *
 * parse.c 和 cgen.c 有大量局部变量（labels, cases, fixups）。
 * 验证 tcc 能正确分配和访问多个局部变量。
 *
 * EXPECT: 42
 */

int main(void) {
    int a0=0,a1=1,a2=2,a3=3,a4=4,a5=5,a6=6,a7=7,a8=8,a9=9;
    int b0=0,b1=1,b2=2,b3=3,b4=4,b5=5,b6=6,b7=7,b8=8,b9=9;
    int sum = 0;
    int i;
    int vals[10] = { a0,a1,a2,a3,a4,a5,a6,a7,a8,a9 };
    for (i = 0; i < 10; i++) sum += vals[i];
    for (i = 0; i < 10; i++) sum += b0;
    if (sum != 45 + 90) return 1;
    return 42;
}
