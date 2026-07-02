/*
 * 054_goto.c — goto label
 *
 * 验证：goto 跳转和标签
 * 预期：退出码 0
 */

int main(void) {
    int x = 0;

    if (x != 0)
        goto fail;

    x = 10;
    if (x == 10)
        goto ok;

fail:
    return 1;  /* 不应到达 */

ok:
    if (x != 10) return 2;

    /* forward goto */
    goto skip;
    return 3;  /* 不应到达 */

skip:
    return 0;
}
