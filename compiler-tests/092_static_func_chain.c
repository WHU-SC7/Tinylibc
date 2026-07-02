/*
 * 092_static_func_chain.c — 静态函数调用链
 *
 * preproc.c 中有 preprocess → pp_buf → pp_buf_impl 等静态函数调用链。
 * 验证静态函数间互相调用且传递结构体指针的正确性。
 *
 * EXPECT: 42
 */

typedef struct { char *data; int len; int cap; } Buf;
static Buf shared;

static void step2(Buf *b, const char *s) {
    while (*s) {
        if (b->len < b->cap - 1)
            b->data[b->len++] = *s;
        s++;
    }
}

static void step1(Buf *b, const char *s) {
    step2(b, s);
    b->data[b->len] = '\0';
}

int main(void) {
    char buf[64];
    shared.data = buf;
    shared.cap  = 64;
    shared.len  = 0;
    step1(&shared, "hello");
    /* 验证结果 */
    const char *p = shared.data;
    const char *q = "hello";
    while (*p && *q && *p == *q) { p++; q++; }
    if (*p != *q) return 1;
    return 42;
}
