/*
 * 057_global_var.c — 全局变量读写
 *
 * 验证：全局变量的声明、读取和写入
 * 预期：退出码 0
 */

int g_val;
int g_arr[3];

void set_global(int x) {
    g_val = x;
}

int get_global(void) {
    return g_val;
}

int main(void) {
    /* 初始化为 0 */
    if (g_val != 0) return 1;

    /* 赋值 */
    g_val = 42;
    if (g_val != 42) return 2;

    /* 函数内读写 */
    set_global(100);
    if (g_val != 100) return 3;
    if (get_global() != 100) return 4;

    /* 全局数组 */
    g_arr[0] = 10;
    g_arr[1] = 20;
    g_arr[2] = 30;
    if (g_arr[0] != 10) return 5;
    if (g_arr[1] != 20) return 6;
    if (g_arr[2] != 30) return 7;

    return 0;
}
