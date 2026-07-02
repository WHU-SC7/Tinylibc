/*
 * 086_struct_brace_init.c — 结构体花括号初始化 {0,0,0}
 *
 * tcc 自身 preproc.c 大量使用 OutBuf x = { 0, 0, 0 } 模式。
 * 验证 tcc 能正确处理结构化零初始化。
 *
 * EXPECT: 42
 */

typedef struct { char *data; int len; int cap; } Buf;

int main(void) {
    Buf b = { 0, 0, 0 };
    /* 全部初始化为 0 */
    if (b.data != 0) return 1;
    if (b.len  != 0) return 2;
    if (b.cap  != 0) return 3;
    return 42;
}
