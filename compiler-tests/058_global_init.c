/*
 * 058_global_init.c — 全局变量初始化
 *
 * 验证：带初始值的全局变量
 * 注意：tcc 使用运行时赋值而非 .data 节区
 * 预期：退出码 0
 */

int g_a = 10;
int g_b = 20;
int g_c = 30;

int main(void) {
    if (g_a != 10) return 1;
    if (g_b != 20) return 2;
    if (g_c != 30) return 3;

    /* 修改后验证 */
    g_a = 100;
    if (g_a != 100) return 4;

    return 0;
}
