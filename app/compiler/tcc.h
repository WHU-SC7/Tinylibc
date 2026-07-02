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
#include "elf_write.h"

/* ─── 缓冲区容量（固定分配，溢出时安全报错退出） ─── */

#define CODE_BUF_SIZE  (256 * 1024)  /* 代码生成缓冲区 */
#define STRTAB_SIZE    (256 * 1024)  /* ELF 字符串表 */
#define STRPOOL_SIZE   (256 * 1024)  /* 字符串字面量池 */

/* ─── Arena 分配器 ─── */

#define ARENA_SIZE (16 * 1024 * 1024)

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
    TOK_DOUBLE,
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
    TOK_REGISTER,
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
    double dval;          /* 浮点字面量的值 */
    int is_float;         /* 1 表示浮点字面量（dval 有效） */
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
    AST_SWITCH,        /* switch 语句 */
    AST_CASE,          /* case 标签 */
    AST_DEFAULT,       /* default 标签 */
    AST_BREAK,         /* break 语句 */
    AST_CONTINUE,      /* continue 语句 */
    AST_BLOCK,         /* 复合语句（{}） */
    AST_CALL,          /* 函数调用 */
    AST_EXPR_STMT,     /* 表达式语句 */
    AST_NULL_STMT,     /* 空语句 */
    AST_MEMBER,        /* s.member 或 p->member */
    AST_STRING,        /* 字符串常量 */
    AST_STRUCT_DEF,    /* struct 定义（顶层） */
    AST_ASM,           /* __asm__ 内联汇编 */
    AST_GOTO,          /* goto label */
    AST_LABEL,         /* label: 定义 */
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
    double dval;      /* 浮点常量值（is_float=1 时有效） */
    int is_float;     /* 1 表示此表达式结果为 double 类型 */
    /* AST_BINOP */
    struct AstNode *left, *right;
    /* AST_IF */
    struct AstNode *cond, *then_stmt, *else_stmt;
    /* AST_WHILE / AST_FOR */
    struct AstNode *loop_cond, *loop_body, *loop_init, *loop_step;
    /* AST_SWITCH: cond = 条件, loop_body = 跳转表代码, stmts = 体语句 (AST_CASE/AST_DEFAULT/普通语句) */
    /* AST_BLOCK */
    struct AstNode *stmts;
    /* AST_CALL */
    struct AstNode *args;
    /* AST_MEMBER: member_name = 成员名字 */
    const char *member_name;
    /* AST_STRING: str_val = 解码后的字符串内容 */
    const char *str_val;
    /* AST_ASM: asm_template = 汇编模板字符串 */
    const char *asm_template;
    /* AST_FUNC_DEF: params = 参数声明链表 */
    struct AstNode *params;
    /* AST_UNARY / AST_BINOP 操作符标记 */
    int op;
    /* 修饰标记 */
    int is_static;
    int type_size;       /* 类型大小（字节）：4=int, 8=指针/long/double, 1=char, 2=short */
} AstNode;

/* ─── 符号表（代码生成输出用） ─── */

#define MAX_SYMS 8192
#define MAX_RELS 16384

typedef struct {
    const char *name;
    int offset;       /* 在 .text 中的偏移 */
    int size;         /* 函数字节大小 */
    int is_global;
    int is_func;
    int sym_idx;      /* 在 .symtab 中的索引（由 ELF 写入时分配） */
} CgenSym;

/* 代码生成输出的全局状态 — 映射到 elf_write.c 的共享缓冲区 */
#define code_buf    elf_code_buf
#define code_size   elf_code_size
#define syms        elf_syms
#define sym_count   elf_sym_count
#define rels        elf_rels
#define rel_count   elf_rel_count
/* 注意：tcc 的 CgenSym 必须与 elf_write.h 的 ElfWriteSym 布局兼容 */
#define CgenSym ElfWriteSym

extern unsigned char elf_code_buf[CODE_BUF_SIZE];
extern int elf_code_size;
extern ElfWriteSym elf_syms[MAX_SYMS];
extern int elf_sym_count;
extern Elf64_Rela elf_rels[MAX_RELS];
extern int elf_rel_count;

extern char strtab[STRTAB_SIZE];
extern int strtab_len;

/* 字符串字面量池 — cgen_expr 追加，cgen_program 结尾刷入 code_buf */
extern unsigned char strpool_buf[STRPOOL_SIZE];
extern int strpool_size;

/* 字符串字面量引用记录（供 cgen_program 结尾修复偏移） */
#define MAX_STRINGS 1024
typedef struct {
    int pool_offset;       /* 在 strpool_buf 中的偏移 */
    int len;               /* 字符串长度（含 null） */
    int sym_index;         /* syms[] 中对应符号条目的索引 */
    char name[16];         /* 符号名如 ".LC0" */
} StrInfo;
extern StrInfo str_infos[MAX_STRINGS];
extern int str_info_count;

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

/* ─── 局部变量表 ─── */

#define MAX_LOCALS 256
typedef struct {
    const char *name;
    int offset;  /* 距 rbp 的偏移（负值） */
    int size;    /* 类型大小 */
    const char *struct_tag;  /* 如果是 struct 类型，存标签名 */
    int is_float;            /* 1 表示 double 类型变量 */
} LocalVar;

extern LocalVar locals[MAX_LOCALS];
extern int local_count;
extern int frame_size;
extern int reg_save_offset;
extern int func_nparams;

/* ─── 类型系统（Phase 3） ─── */

#define MAX_MEMBERS 128
#define MAX_TAGS 512
#define MAX_TYPEDEFS 1024

/* 结构体成员描述 */
typedef struct {
    const char *name;
    int offset;
    int size;
} Member;

/* 结构体类型（通过 struct 标签或匿名定义） */
typedef struct {
    const char *tag;        /* NULL 表示匿名 */
    Member members[MAX_MEMBERS];
    int member_count;
    int total_size;
} StructType;

/* struct/union/enum 标签表 */
extern StructType tag_table[MAX_TAGS];
extern int tag_count;

/* typedef 名字表 */
typedef struct {
    const char *name;
    int size;        /* 类型大小（字节） */
    int type_kind;   /* 0=基本, 1=struct */
    int struct_idx;
    Member members[MAX_MEMBERS];
    int member_count;
} TypedefEntry;

extern TypedefEntry typedef_table[MAX_TYPEDEFS];
extern int typedef_count;

/* 判断名字是否为 typedef */
int is_typedef_name(const char *name);

/* 查找 struct 标签 */
StructType *find_struct_tag(const char *tag);

/* ─── 预处理器 ─── */

#define MAX_INCLUDE_PATHS 16

void add_include_path(const char *path);
char *preprocess(const char *src, int len, const char *fname, int *out_len);

/* ─── 代码生成 ─── */

void cgen_init(void);
void cgen_program(AstNode *prog);
void cgen_expr(AstNode *node);
void cgen_asm(AstNode *node);

/* ─── ELF 写入 ─── */

int elf_write_object(const char *path);

/* ─── 工具函数 ─── */

static inline int align_up(int offset, int align) {
    return (offset + align - 1) & ~(align - 1);
}

/* 代码生成共享辅助（所有 cgen_*.c 文件共用） */
static inline void e1(int b) {
    if (code_size >= CODE_BUF_SIZE) {
        __write(2, "tcc: code buffer overflow\n", 26);
        __exit(1);
    }
    code_buf[code_size++] = b & 0xFF;
}
static inline void e4(int v) { e1(v); e1(v>>8); e1(v>>16); e1(v>>24); }

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
