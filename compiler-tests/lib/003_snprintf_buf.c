/*
 * 003_snprintf_buf.c — 测试 snprintf.c 中的结构体/缓冲区模式
 *
 * 验证：struct 成员访问 / size_t 比较 / 指针写入
 * EXPECT: 0
 */

typedef struct { char *buffer; unsigned long size; unsigned long pos; int truncated; } strbuf_t;

static void sb_write(strbuf_t *sb, const char *data, unsigned long len) {
    if (!sb->buffer || sb->size == 0) { sb->pos += len; sb->truncated = 1; return; }
    if (sb->pos >= sb->size) { sb->pos += len; sb->truncated = 1; return; }
    unsigned long remain = sb->size - sb->pos;
    if (len < remain) { int i; for (i = 0; i < (int)len; i++) sb->buffer[sb->pos + i] = data[i]; sb->pos += len; }
    else { if (remain > 1) { int i; for (i = 0; i < (int)(remain-1); i++) sb->buffer[sb->pos + i] = data[i]; } sb->pos += len; sb->truncated = 1; }
}

int main(void) {
    char buf[16];
    strbuf_t sb;
    sb.buffer = buf; sb.size = 16; sb.pos = 0; sb.truncated = 0;
    sb_write(&sb, "hi", 2);
    if (sb.pos != 2) return 1;
    if (buf[0] != 'h' || buf[1] != 'i') return 2;

    /* struct 指针写回 */
    sb.pos = 0;
    strbuf_t *psb = &sb;
    psb->pos = 10;
    if (sb.pos != 10) return 3;

    /* null 缓冲区 */
    sb_write(&sb, "test", 4);
    if (sb.pos != 14) return 4;

    return 0;
}
