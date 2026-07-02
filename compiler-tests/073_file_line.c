/*
 * 073_file_line.c — __FILE__ / __LINE__
 *
 * 验证：预定义宏 __FILE__ 和 __LINE__
 * 预期：退出码 0
 */

int main(void) {
    /* __LINE__ 应该是本行行号 */
    int line = __LINE__;
    if (line != 12) return 1;  /* 这行是第 12 行 */

    line = __LINE__;
    if (line != 14) return 2;  /* 这行是第 14 行 */

    /* __FILE__ 是文件名 */
    const char *f = __FILE__;
    (void)f;

    return 0;
}
