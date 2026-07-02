/*
 * 038_func_call_0.c — 无参函数调用
 *
 * 验证：无参数函数的调用和返回值
 * 预期：退出码 0
 */

int get_value(void) {
    return 42;
}

int main(void) {
    int x = get_value();
    if (x != 42) return 1;
    return 0;
}
