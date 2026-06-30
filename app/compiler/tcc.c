/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * tcc — Tinylibc C 编译器（统一入口）
 *
 * 此文件是 tmake 识别的单一应用入口。它 #include 所有模块文件，
 * 使得编译器能通过项目现有构建系统直接编译。
 *
 * 开发时各模块文件（lex.c / parse.c / cgen.c / elf_write.c）保持独立，
 * 支持单独修改和增量重编。
 *
 * 用法：
 *   tcc input.c                  # 输出 input.o
 *   tcc input.c -o output.o      # 指定输出文件
 *   tcc -d input.c               # 调试：打印 Token 序列
 *
 * 索引：
 *   main            主入口：参数解析 → 读取 → 编译 → 输出
 *     read_file     读取整个源文件到堆内存
 */

/* 统一编译：包含所有模块。
 * 模块中的 #include "tcc.h" 按 #include "..." 规则从 app/compiler/ 目录查找 */
#include "tcc.h"
#include "elf.h"
#include "lex.c"
#include "parse.c"
#include "cgen_asm.c"
#include "cgen_expr.c"
#include "cgen.c"
#include "elf_write.c"

/* ─── 读取文件 ─── */

static char *read_file(const char *path, int *out_len) {
    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) {
        __printf("tcc: cannot open '%s'\n", path);
        return NULL;
    }

    off_t size = __lseek(fd, 0, SEEK_END);
    __lseek(fd, 0, SEEK_SET);

    if (size <= 0 || size > 1024 * 1024) {
        __printf("tcc: invalid file size for '%s'\n", path);
        __close(fd);
        return NULL;
    }

    char *buf = (char *)tlibc_malloc(size + 2);
    if (!buf) {
        __printf("tcc: out of memory\n");
        __close(fd);
        return NULL;
    }

    int n = __read(fd, buf, size);
    __close(fd);

    if (n != size) {
        __printf("tcc: read error on '%s'\n", path);
        tlibc_free(buf);
        return NULL;
    }

    buf[size] = '\0';
    buf[size + 1] = '\0';
    *out_len = size;
    return buf;
}

/* ─── 输出文件路径 ─── */

static void make_output_path(const char *input, const char *output,
                             char *out_buf, int buf_size) {
    if (output) {
        int i;
        for (i = 0; output[i] && i < buf_size - 1; i++)
            out_buf[i] = output[i];
        out_buf[i] = '\0';
        return;
    }

    int i;
    for (i = 0; input[i] && i < buf_size - 3; i++)
        out_buf[i] = input[i];
    out_buf[i] = '\0';

    if (i >= 2 && out_buf[i - 2] == '.' && out_buf[i - 1] == 'c')
        out_buf[i - 2] = '\0';

    int j;
    for (j = 0; out_buf[j]; j++)
        ;
    out_buf[j] = '.';
    out_buf[j + 1] = 'o';
    out_buf[j + 2] = '\0';
}

/* ─── 打印 Token 序列（调试用） ─── */

static void debug_tokens(const char *src, int len) {
    Lexer lx;
    lexer_init(&lx, src, len);
    while (1) {
        Token t = lexer_next(&lx);
        switch (t.kind) {
        case TOK_EOF:   __printf("EOF\n"); return;
        case TOK_INT:   __printf("int\n"); break;
        case TOK_VOID:  __printf("void\n"); break;
        case TOK_RETURN: __printf("return\n"); break;
        case TOK_IDENT:
            __printf("ident:%.*s\n", t.len, t.start);
            break;
        case TOK_NUMBER: __printf("number:%d\n", t.ival); break;
        case TOK_LBRACE: __printf("{\n"); break;
        case TOK_RBRACE: __printf("}\n"); break;
        case TOK_LPAREN: __printf("(\n"); break;
        case TOK_RPAREN: __printf(")\n"); break;
        case TOK_SEMI:   __printf(";\n"); break;
        default:         __printf("token:%d\n", t.kind); break;
        }
    }
}

/* ─── 主入口 ─── */

int main(int argc, char *argv[]) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    int debug = 0;

    int i;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'o' && argv[i][2] == '\0' && i + 1 < argc)
                output_path = argv[++i];
            else if (argv[i][1] == '-' && argv[i][2] == '\0')
                ;
            else if (argv[i][1] == '-' && argv[i][2] == 'd' &&
                     argv[i][3] == 'e' && argv[i][4] == 'b' &&
                     argv[i][5] == 'u' && argv[i][6] == 'g')
                debug = 1;
            else if (argv[i][1] == 'd')
                debug = 1;
        } else {
            input_path = argv[i];
        }
    }

    if (!input_path) {
        __printf("usage: tcc input.c [-o output.o]\n");
        return 1;
    }

    int src_len;
    char *src = read_file(input_path, &src_len);
    if (!src)
        return 1;

    if (debug) {
        debug_tokens(src, src_len);
        tlibc_free(src);
        return 0;
    }

    Arena *arena = (Arena *)tlibc_malloc(sizeof(Arena) + ARENA_SIZE);
    if (!arena) {
        __printf("tcc: out of memory\n");
        tlibc_free(src);
        return 1;
    }
    arena->ptr = (char *)arena + sizeof(Arena);
    arena->end = arena->ptr + ARENA_SIZE;

    Lexer lexer;
    lexer_init(&lexer, src, src_len);

    Parser parser;
    parser_init(&parser, &lexer, arena);
    AstNode *prog = parse_program(&parser);

    if (parser.had_error) {
        __printf("tcc: parse error\n");
        tlibc_free(src);
        tlibc_free(arena);
        return 1;
    }

    cgen_init();
    cgen_program(prog);

    char out_path[512];
    make_output_path(input_path, output_path, out_path, sizeof(out_path));

    if (elf_write_object(out_path) != 0) {
        __printf("tcc: cannot write '%s'\n", out_path);
        tlibc_free(src);
        tlibc_free(arena);
        return 1;
    }

    __printf("tcc: wrote %s (%d bytes code, %d symbols)\n",
             out_path, code_size, sym_count);

    tlibc_free(src);
    tlibc_free(arena);
    return 0;
}
