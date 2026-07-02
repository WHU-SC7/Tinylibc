/*
 * 053_switch_default.c — switch/case/default
 *
 * 验证：switch fall-through 和 default
 * 预期：退出码 0
 */

int main(void) {
    int x;
    int result;

    /* default */
    x = 99;
    result = 0;
    switch (x) {
        case 1:
            result = 10;
            break;
        default:
            result = 99;
            break;
    }
    if (result != 99) return 1;

    /* fall-through */
    x = 1;
    result = 0;
    switch (x) {
        case 1:
            result = 1;
            /* fall through */
        case 2:
            result = result + 2;
            break;
        case 3:
            result = result + 4;
            break;
    }
    if (result != 3) return 2;  /* 1 + 2 */

    return 0;
}
