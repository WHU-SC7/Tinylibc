/*
 * 009_logical.c — 逻辑运算符 && || !
 *
 * 验证：逻辑与、或、非
 * 预期：退出码 0
 */

int main(void) {
    /* && */
    if (!(1 && 1)) return 1;
    if (1 && 0)    return 2;
    if (0 && 1)    return 3;
    if (0 && 0)    return 4;

    /* || */
    if (!(1 || 0)) return 5;
    if (!(0 || 1)) return 6;
    if (!(1 || 1)) return 7;
    if (0 || 0)    return 8;

    /* ! — 单次否定 */
    if (!0 != 1)   return 9;
    if (!1 != 0)   return 10;

    /* 混合（无短路依赖：tcc 当前 &&/|| 求值两侧） */
    if (!((1 && 1) || 0)) return 11;
    if ((1 && 0) || (0 && 1)) return 12;

    return 0;
}
