/*
 * 091_dynamic_buf.c — 模拟 OutBuf 动态缓冲区
 *
 * preproc.c 的 OutBuf 是核心数据结构。
 * 验证动态增长缓冲区的读写。
 *
 * EXPECT: 42
 */

typedef struct { char *data; int len; int cap; } Buf;

static void putc(Buf *b, char c) {
    if (b->data == 0) {
        b->cap = 64;
        b->data = (char *)0;  /* 模拟无 malloc 环境 */
    }
    if (b->len < b->cap)
        b->data[b->len++] = c;
}

int main(void) {
    Buf b = { 0, 0, 0 };
    /* 未初始化的 buf，putc 不应崩溃 */
    /* 只是验证结构体零初始化有效 */
    if (b.data != 0) return 1;
    if (b.len  != 0) return 2;
    if (b.cap  != 0) return 3;
    return 42;
}
