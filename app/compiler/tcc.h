/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * tcc.h — Tinylibc C 编译器核心类型定义
 *
 * 机制：定义 Token、AST 节点、Arena 分配器、代码生成中间结构。
 *       所有编译器源文件通过包含此头文件共享类型。
 */

#ifndef TCC_H
#define TCC_H

#include "tlibc_everything.h"
#include "elf.h"

/* ─── Arena 分配器 ─── */

#define ARENA_SIZE (1024 * 1024)

typedef struct {
    char *ptr;
    char *end;
} Arena;

static inline void *arena_alloc(Arena *a, int size) {
    /* 对齐到 8 字节 */
    size = (size + 7) & ~7;
    if (a->ptr + size > a->end) {
        __write(2, "arena oom\n", 10);
        __exit(1);
    }
    void *p = a->ptr;
    a->ptr += size;
    return p;
}

static inline void arena_reset(Arena *a) {
    a->ptr = (char *)a + sizeof(Arena);
}

/* ─── Token ─── */

typedef enum {
    /* 关键字 (按序排列便于关键字查找) */
    TOK_INT = 256,
    TOK_VOID,
    TOK_CHAR,
    TOK_SHORT,
    TOK_LONG,
    TOK_UNSIGNED,
    TOK_SIGNED,
    TOK_RETURN,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_DO,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_SWITCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_GOTO,
    TOK_SIZEOF,
    TOK_STRUCT,
    TOK_UNION,
    TOK_ENUM,
    TOK_TYPEDEF,
    TOK_CONST,
    TOK_VOLATILE,
    TOK_RESTRICT,
    TOK_STATIC,
    TOK_EXTERN,
    TOK_INLINE,
    TOK__ATTRIBUTE__,
    TOK__ASM__,
    TOK__BUILTIN_VA_LIST,
    TOK__BUILTIN_VA_START,
    TOK__BUILTIN_VA_ARG,
    TOK__BUILTIN_VA_END,

    /* 标识符和字面量 */
    TOK_IDENT,
    TOK_NUMBER,
    TOK_STRING,

    /* 标点符号 */
    TOK_SEMI, TOK_LBRACE, TOK_RBRACE,
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_DOT, TOK_ARROW,
    TOK_AMPERSAND, TOK_STAR, TOK_PLUS, TOK_MINUS,
    TOK_TILDE, TOK_EXCLAM,
    TOK_SLASH, TOK_PERCENT,
    TOK_LESS, TOK_GREATER,
    TOK_LESS_EQ, TOK_GREATER_EQ,
    TOK_EQ_EQ, TOK_NOT_EQ,
    TOK_AND_AND, TOK_OR_OR,
    TOK_PIPE, TOK_CARET,
    TOK_LESS_LESS, TOK_GREATER_GREATER,
    TOK_EQ, TOK_PLUS_EQ, TOK_MINUS_EQ,
    TOK_STAR_EQ, TOK_SLASH_EQ, TOK_PERCENT_EQ,
    TOK_AND_EQ, TOK_OR_EQ, TOK_CARET_EQ,
    TOK_LESS_LESS_EQ, TOK_GREATER_GREATER_EQ,
    TOK_PLUS_PLUS, TOK_MINUS_MINUS,
    TOK_QUESTION, TOK_COLON,
    TOK_ELLIPSIS,

    TOK_EOF = 0,
    TOK_ERROR = -1,
} TokenKind;

typedef struct {
    TokenKind kind;
    const char *start;   /* 指向源文件中的起始位置 */
    int len;             /* 词素的字节长度 */
    int ival;            /* TOK_NUMBER 的整数值 */
    const char *sval;    /* TOK_IDENT 的名称指针（arena 分配） */
} Token;

/* ─── AST 节点类型 ─── */

typedef enum {
    AST_PROGRAM,       /* 翻译单元：函数定义链表 */
    AST_FUNC_DEF,      /* 函数定义 */
    AST_RETURN,        /* return 语句 */
    AST_CONSTANT,      /* 整数常量 */
    AST_BINOP,         /* 二元运算（+ - * / 等） */
    AST_UNARY,         /* 一元运算（! ~ - & *） */
    AST_ASSIGN,        /* 赋值 */
    AST_VAR,           /* 变量引用（左值或右值） */
    AST_VAR_DECL,      /* 变量声明 */
    AST_IF,            /* if 语句 */
    AST_WHILE,         /* while 语句 */
    AST_FOR,           /* for 语句 */
    AST_DO_WHILE,      /* do-while 语句 */
    AST_BREAK,         /* break 语句 */
    AST_CONTINUE,      /* continue 语句 */
    AST_BLOCK,         /* 复合语句（{}） */
    AST_CALL,          /* 函数调用 */
    AST_EXPR_STMT,     /* 表达式语句 */
    AST_NULL_STMT,     /* 空语句 */
} AstKind;

/* ─── AST 节点 ─── */

typedef struct AstNode {
    AstKind kind;
    struct AstNode *next;    /* 链表链接（函数列表、语句列表、参数列表） */
    /* AST_FUNC_DEF / AST_VAR / AST_CALL / AST_VAR_DECL */
    const char *name;
    /* AST_FUNC_DEF */
    struct AstNode *body;
    /* AST_RETURN / AST_EXPR_STMT */
    struct AstNode *expr;
    /* AST_CONSTANT */
    int ival;
    /* AST_BINOP */
    struct AstNode *left, *right;
    /* AST_IF */
    struct AstNode *cond, *then_stmt, *else_stmt;
    /* AST_WHILE / AST_FOR */
    struct AstNode *loop_cond, *loop_body, *loop_init, *loop_step;
    /* AST_BLOCK */
    struct AstNode *stmts;
    /* AST_CALL */
    struct AstNode *args;
    /* AST_UNARY / AST_BINOP 操作符标记 */
    int op;
    /* 修饰标记 */
    int is_static;
} AstNode;

/* ─── 符号表（代码生成输出用） ─── */

#define MAX_SYMS 4096
#define MAX_RELS 8192

typedef struct {
    const char *name;
    int offset;       /* 在 .text 中的偏移 */
    int size;         /* 函数字节大小 */
    int is_global;
    int is_func;
    int sym_idx;      /* 在 .symtab 中的索引（由 ELF 写入时分配） */
} CgenSym;

/* 代码生成输出的全局状态 */
extern unsigned char code_buf[65536];
extern int code_size;

extern CgenSym syms[MAX_SYMS];
extern int sym_count;

extern Elf64_Rela rels[MAX_RELS];
extern int rel_count;

extern char strtab[65536];
extern int strtab_len;

/* ─── 词法分析器 ─── */

typedef struct {
    const char *start;   /* 当前 token 起始 */
    const char *pos;     /* 当前位置 */
    const char *end;     /* 源文件结束 */
    int line;
    int col;
    Token cur;           /* 当前 token */
} Lexer;

void lexer_init(Lexer *lx, const char *src, int len);
Token lexer_next(Lexer *lx);
Token lexer_peek(Lexer *lx);

/* ─── 解析器 ─── */

typedef struct {
    Lexer *lexer;
    Token tok;          /* 超前查看的当前 token */
    Arena *arena;
    int had_error;
} Parser;

void parser_init(Parser *p, Lexer *lx, Arena *a);
AstNode *parse_program(Parser *p);

/* ─── 代码生成 ─── */

void cgen_init(void);
void cgen_program(AstNode *prog);

/* ─── ELF 写入 ─── */

int elf_write_object(const char *path);

/* ─── 工具函数 ─── */

static inline int align_up(int offset, int align) {
    return (offset + align - 1) & ~(align - 1);
}

static inline const char *arena_strdup(Arena *a, const char *start, int len) {
    char *p = arena_alloc(a, len + 1);
    int i;
    for (i = 0; i < len; i++)
        p[i] = start[i];
    p[len] = '\0';
    return p;
}

void error_at(Parser *p, const char *msg);

#endif /* TCC_H */
