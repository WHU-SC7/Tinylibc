/*
 * 077_nested_if.c — 多层 if-else 嵌套
 *
 * 验证：深度嵌套的条件分支
 * 预期：退出码 0
 */

int main(void) {
    int x;
    int result;

    /* 3 层嵌套 */
    x = 1;
    if (x == 1) {
        if (x == 1) {
            if (x == 1) {
                result = 100;
            } else {
                result = 0;
            }
        }
    }
    if (result != 100) return 1;

    /* else 匹配 */
    x = 0;
    result = 0;
    if (x) {
        if (x) result = 1;
        else   result = 2;
    } else {
        result = 3;
    }
    if (result != 3) return 2;

    return 0;
}
