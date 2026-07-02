/*
 * 035_addr_of_global.c — & 取全局变量地址
 *
 * 验证：取全局变量的地址
 * 注意：tcc 使用 R_X86_64_32 重定位，>4GB 地址空间可能链接失败
 * 预期：退出码 0（32 位地址空间可工作）
 */

int global_x = 42;
int global_arr[4] = {10, 20, 30, 40};

int main(void) {
    int *p;

    p = &global_x;
    if (*p != 42) return 1;

    *p = 100;
    if (global_x != 100) return 2;

    /* 全局数组地址 */
    p = &global_arr[0];
    if (*p != 10) return 3;

    p = &global_arr[2];
    if (*p != 30) return 4;

    return 0;
}
