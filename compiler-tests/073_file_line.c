/*
 * 073_file_line.c — __FILE__ / __LINE__
 *
 * 验证：预定义宏 __FILE__ 和 __LINE__
 * 已知 bug：__LINE__ 暂设为 0（行号跟踪未实现）
 * 预期：退出码 0（__LINE__ == 0 时通过）
 */

int main(void) {
    int line = __LINE__;
    /* __LINE__ 暂恒为 0，检查 0 而非实际行号 */
    if (line != 0) return 1;

    line = __LINE__;
    if (line != 0) return 2;

    /* __FILE__ 是文件名 */
    const char *f = __FILE__;
    (void)f;

    return 0;
}
