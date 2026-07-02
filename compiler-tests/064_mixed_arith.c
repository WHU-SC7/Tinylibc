/*
 * 064_mixed_arith.c — int + double 混合运算
 *
 * 验证：int 和 double 在表达式中的混合
 * 注意：tcc 可能不支持自动类型提升（已知限制）
 * 预期：退出码 0（若提升失败则 FAIL）
 */

int main(void) {
    double d;
    int i = 5;

    /* int + double */
    d = i + 3.0;
    if (d != 8.0) return 1;

    /* double + int */
    d = 3.0 + i;
    if (d != 8.0) return 2;

    /* double * int */
    d = 2.5 * 2;
    if (d != 5.0) return 3;

    return 0;
}
