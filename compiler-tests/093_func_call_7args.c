/*
 * 093_func_call_7args.c — 7 参数函数调用
 *
 * __syscall6(n, a1..a6) 需要 7 个参数。
 * 现在 tcc 支持 7+ 参数的函数调用。
 *
 * EXPECT: 0
 */

/* 7 参数求和 */
static int sum7(int a1,int a2,int a3,int a4,int a5,int a6,int a7) {
    return a1+a2+a3+a4+a5+a6+a7;
}

int main(void) {
    int r = sum7(1,2,3,4,5,6,7);
    if (r == 28) return 0;   /* 正确 */
    return 1;                 /* 错误 */
}
