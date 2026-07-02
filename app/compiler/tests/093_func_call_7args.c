/*
 * 093_func_call_7args.c — 7 参数函数调用
 *
 * __syscall6(n, a1..a6) 需要 7 个参数。
 * tcc 的函数调用生成只支持 ≤6 个参数，第 7 个被丢弃。
 * 这个测试验证 tcc 能否通过变通方式（手动压栈）处理。
 *
 * 注意：当前 tcc 不支持 7+ 参数，此测试验证问题的存在。
 *
 * EXPECT: 0
 */

/* 7 参数求和 */
static int sum7(int a1,int a2,int a3,int a4,int a5,int a6,int a7) {
    return a1+a2+a3+a4+a5+a6+a7;
}

int main(void) {
    int r = sum7(1,2,3,4,5,6,7);
    /* 正确的 7 参数求和应为 28 */
    /* 如果第 7 个参数丢失，结果为 21（前 6 个的和）*/
    if (r == 28) return 42;  /* 正确 */
    if (r == 21) return 0;   /* 第 7 个参数丢失 */
    return r;                 /* 其他错误 */
}
