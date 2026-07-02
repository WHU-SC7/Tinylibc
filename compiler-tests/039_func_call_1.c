/*
 * 039_func_call_1.c — 单参数函数调用
 *
 * 验证：1 个参数的传递
 * 预期：退出码 0
 */

int double_it(int x) {
    return x * 2;
}

int main(void) {
    if (double_it(5) != 10) return 1;
    if (double_it(0) != 0)  return 2;
    if (double_it(-3) != -6) return 3;
    return 0;
}
