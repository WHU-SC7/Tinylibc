/*
 * 082_block_scope.c — 块作用域变量
 *
 * 验证：复合语句中的局部变量覆盖外层同名变量
 * 预期：退出码 0
 */

int main(void) {
    int x = 10;

    {
        int x = 20;
        if (x != 20) return 1;  /* 内层 x */
    }

    if (x != 10) return 2;  /* 外层 x 不受影响 */

    /* 嵌套块 */
    {
        int a = 1;
        {
            int a = 2;
            if (a != 2) return 3;
        }
        if (a != 1) return 4;
    }

    return 0;
}
