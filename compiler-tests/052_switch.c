/*
 * 052_switch.c — switch/case
 *
 * 验证：switch 基本跳转
 * 注意：tcc 使用线性 cmp 链（非跳转表）
 * 预期：退出码 0
 */

int main(void) {
    int x = 2;
    int result = 0;

    switch (x) {
        case 1:
            result = 10;
            break;
        case 2:
            result = 20;
            break;
        case 3:
            result = 30;
            break;
    }

    if (result != 20) return 1;

    /* 未匹配的 case — 不执行任何分支 */
    x = 99;
    result = 0;
    switch (x) {
        case 1:
            result = 10;
            break;
        case 2:
            result = 20;
            break;
    }
    if (result != 0) return 2;

    return 0;
}
