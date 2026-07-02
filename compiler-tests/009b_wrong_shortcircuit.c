/*
 * 009b_wrong_shortcircuit.c — 逻辑短路求值（已知 bug）
 *
 * 验证：&& 和 || 的短路求值
 * 已知 bug：tcc 求值 &&/|| 两侧表达式，不短路
 * 预期：此测试在 tcc 上 FAIL（短路不生效）
 */

int main(void) {
    int x = 5;

    /* 0 && side_effect — 不应求值右侧 */
    if (0 && (x = 10)) ;
    if (x != 5) return 1;  /* tcc 上 FAIL: x 被赋值为 10 */

    /* 1 || side_effect — 不应求值右侧 */
    x = 5;
    if (1 || (x = 20)) ;
    if (x != 5) return 2;  /* tcc 上可能 FAIL: x 被赋值为 20 */

    return 0;
}
