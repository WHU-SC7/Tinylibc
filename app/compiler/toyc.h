/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * toyc.h — ToyC 编译器核心类型定义
 *
 * 机制：定义 Token、AST 节点、Arena 分配器、代码生成中间结构。
 *       所有编译器源文件通过包含此头文件共享类型。
 */

#ifndef TOYC_H
#define TOYC_H

#include "toyc_need.h"
#include "elf.h"

/* ─── 缓冲区容量（固定分配，溢出时安全报错退出） ─── */

#define CODE_BUF_SIZE  524288  /* 代码生成缓冲区 (512*1024) */
#define STRTAB_SIZE    524288  /* ELF 字符串表 (512*1024) */
#define STRPOOL_SIZE   524288  /* 字符串字面量池 (512*1024) */
#ifndef DATA_BUF_SIZE
#define DATA_BUF_SIZE  524288  /* .data 段原始数据缓冲区 (512*1024) */
#endif

#include "elf_write.h"

/* ─── Arena 分配器 ─── */

#define ARENA_SIZE 33554432  /* (32*1024*1024) */

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
    { char *zp = (char *)p; int zi; for (zi = 0; zi < size; zi++) zp[zi] = 0; }
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
    TOK_FLOAT,
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
    TOK__BUILTIN_HUGE_VAL,
    TOK__BUILTIN_HUGE_VALF,

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
    long ival;      /* TOK_NUMBER 的整数值（64 位）; 对 float 常量存词素长度 */
    double dval;          /* 浮点字面量的值（未使用，占位保留） */
    int is_float;         /* 0=非浮点, 4=float(32-bit), 8=double(64-bit) */
    int is_unsigned;      /* 1 表示 u/ul/ull 后缀的整数常量 */
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

/* ─── 全局变量初始化器元素 ─── */

#define INIT_TYPE_INT 0   /* 整数值 */
#define INIT_TYPE_STR 1   /* 字符串（在 .data 中占 8 字节指针，需重定位） */

typedef struct {
    int type;            /* INIT_TYPE_INT 或 INIT_TYPE_STR */
    long ival;           /* INIT_TYPE_INT 的整数值 */
    const char *str;     /* INIT_TYPE_STR 的字符串内容（arena 分配，已解码） */
} InitItem;

/* ─── 内联汇编操作数 ─── */

typedef struct {
    const char *constraint;  /* 约束字符串，如 "a", "D", "=a" 等 */
    struct AstNode *expr;    /* 约束对应的表达式 AST */
} AsmOperand;

/* ─── 前向声明（StructType 定义在 AstNode 之后） ─── */
typedef struct StructType StructType;

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
    long ival;
    int is_float;     /* 0=非浮点, 4=float(32-bit), 8=double(64-bit) */
    double dval;      /* 浮点常量值（is_float!=0 时有效） */
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
    struct AstNode *call_target;  /* 函数调用目标表达式（函数指针表达式如 ops[0]） */
    /* AST_MEMBER: member_name = 成员名字 */
    const char *member_name;
    /* AST_STRING: str_val = 解码后的字符串内容 */
    const char *str_val;
    /* AST_ASM: 内联汇编的完整信息 */
    struct {
        const char *asm_template;   /* 汇编模板字符串 */
        int is_volatile;            /* __volatile__ 标记 */
        AsmOperand *outputs;        /* 输出操作数数组 */
        int output_count;
        AsmOperand *inputs;         /* 输入操作数数组 */
        int input_count;
        const char **clobbers;           /* 破坏列表（字符串数组） */
        int clobber_count;
    } asm_;

    /* 注意：asm_ 是匿名子结构体，通过 node->asm_.inputs 等方式访问 */
    /* AST_FUNC_DEF: params = 参数声明链表 */
    struct AstNode *params;
    /* AST_UNARY / AST_BINOP 操作符标记 */
    int op;
    /* 修饰标记 */
    int is_static;       /* static 存储类 */
    int is_variadic;     /* 变参函数（...） */
    int is_postfix;      /* 1 表示 x++/x--（后缀），0 表示 ++x/--x（前缀） */
    int type_size;       /* 类型大小（字节）：4=int, 8=指针/long/double, 1=char, 2=short */
    int elem_size;       /* AST_VAR_DECL: 指针变量的元素大小（int*→4, char**→8） */
    int base_elem_size;  /* AST_VAR_DECL: 数组的基础元素大小（多维数组中内层元素大小） */
    int elem_is_ptr;     /* 1: 数组元素为指针类型（char *arr[] 或 int *arr[N]） */
    int elem_is_float;   /* 指针变量的元素是否为 float/double */
    int is_array;        /* 1: 数组变量（分配时退化为指针）0: 标量或指针变量 */
    /* 全局变量初始化器数据（AST_VAR_DECL 且 = { ... } 时有效） */
    int init_count;           /* init_items 数组长度 */
    int init_items_per_elem;  /* 每个数组元素的标量初始化器数（用于元素内边距填充） */
    InitItem *init_items;     /* arena 分配的数组 */
    int is_unsigned;     /* 1 表示 unsigned 类型（unsigned int/long/char/short） */
    int elem_is_unsigned; /* 指针变量：指向的元素类型是否为 unsigned */
    StructType *struct_type; /* 仅 struct 类型的表达式：指向解析后的 StructType，NULL=非 struct 或未知 */
} AstNode;

/* ─── 符号表（代码生成输出用） ─── */

#define MAX_SYMS 16384
#define MAX_RELS 32768

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
/* 注意：toyc 的 CgenSym 必须与 elf_write.h 的 ElfWriteSym 布局兼容 */
#define CgenSym ElfWriteSym

extern unsigned char elf_code_buf[CODE_BUF_SIZE];
extern int elf_code_size;
extern ElfWriteSym elf_syms[MAX_SYMS];
extern int elf_sym_count;
extern Elf64_Rela elf_rels[MAX_RELS];
extern int elf_rel_count;
extern int elf_bss_size;
extern int elf_data_size;

/* .data 段缓冲区（映射到 elf_write.c 的全局变量） */
#define data_buf        elf_data_buf
#define data_size       elf_data_size
#define data_rels       elf_data_rels
#define data_rel_count  elf_data_rel_count

extern unsigned char elf_data_buf[DATA_BUF_SIZE];
extern int elf_data_size;
extern Elf64_Rela elf_data_rels[MAX_RELS];
extern int elf_data_rel_count;

extern char strtab[STRTAB_SIZE];
extern int strtab_len;

/* 全局变量的元素大小（数组下标运算用） */
extern int global_elem_size[MAX_SYMS];
extern int global_ptr_elem_size[MAX_SYMS];  /* 全局指针变量的元素大小（int*→4, long*→8） */
extern int global_base_elem_size[MAX_SYMS];
extern int global_elem_is_ptr_arr[MAX_SYMS];
extern int global_elem_unsigned[MAX_SYMS];
extern int global_elem_float[MAX_SYMS];      /* 全局数组/指针元素的浮点类型：0=非浮点, 4=float, 8=double */
extern int global_is_array[MAX_SYMS];        /* 全局变量是否为数组 */

/* 字符串字面量池 — cgen_expr 追加，cgen_program 结尾刷入 code_buf */
extern unsigned char strpool_buf[STRPOOL_SIZE];
extern int strpool_size;

/* 字符串字面量引用记录（供 cgen_program 结尾修复偏移） */
#define MAX_STRINGS 4096
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
    const char *filename;/* 源文件名（供错误报告使用） */
    int line;
    int col;
    Token cur;           /* 当前 token */
    const char *lex_err; /* 最近一次词法错误的描述（供 parser 提取） */
} Lexer;

void lexer_init(Lexer *lx, const char *src, int len, const char *fname);
Token lexer_next(Lexer *lx);
Token lexer_peek(Lexer *lx);

/* ─── 解析器 ─── */

#define MAX_ERRORS 8  /* 单次编译最大报错数，之后静默跳过解析 */

typedef struct {
    Lexer *lexer;
    Token tok;          /* 超前查看的当前 token */
    Arena *arena;
    int had_error;
    int error_count;    /* 已报错误数（已达 MAX_ERRORS 则后续错误静默跳过） */
    int func_depth;     /* 函数定义嵌套深度（>0 在函数体内） */
    int loop_depth;     /* 循环嵌套深度（while/for/do-while） */
    int switch_depth;   /* switch 嵌套深度 */
    int block_depth;    /* 复合语句嵌套深度（预留给变量重定义检测） */
} Parser;

void parser_init(Parser *p, Lexer *lx, Arena *a);
AstNode *parse_program(Parser *p);

/* ─── 局部变量表 ─── */

#define MAX_LOCALS 4096
typedef struct {
    const char *name;
    int offset;  /* 距 rbp 的偏移（负值） */
    int size;    /* 类型大小 */
    const char *struct_tag;  /* 如果是 struct 类型，存标签名 */
    int is_float;            /* 0=非浮点, 4=float(32-bit), 8=double(64-bit) */
    int element_size;        /* 指针变量的元素大小（用于指针运算：int*→4, char**→8） */
    int base_elem_size;      /* 数组变量：单个元素的类型尺寸（多维数组的内层 elem_size） */
    int elem_is_ptr;         /* 1: 数组元素的基类型是指针（char *arr[]） */
    int scope_depth;         /* 声明时的作用域深度（用于块作用域变量阴影） */
    int scope_id;            /* 声明时所在块的唯一 scope ID（由 scope_chain 分配） */
    int is_array;            /* 1 表示数组类型（访问时退化为指针） */
    int is_unsigned;         /* 1 表示 unsigned 类型 */
    int is_param;            /* 1 表示函数参数（大结构体参数保存指针而非数据） */
    int elem_is_unsigned;    /* 指针变量的元素是否为 unsigned（用于 *ptr 解引用） */
    int elem_is_float;       /* 指针变量的元素是否为 float/double（用于 *ptr 解引用） */
} LocalVar;

extern LocalVar locals[MAX_LOCALS];
extern int local_count;
extern int frame_size;
extern int reg_save_offset;
extern int func_nparams;
extern int scope_depth;
#define MAX_SCOPE_IDS 256
extern int scope_chain[MAX_SCOPE_IDS];
extern int scope_chain_count;
static inline int is_scope_visible(int id) {
    int i;
    for (i = 0; i < scope_chain_count; i++)
        if (scope_chain[i] == id) return 1;
    return 0;
}

/* 局部变量搜索：先 scope_id 精确过滤，回退到任意同名+同深度变量。 */
#define SEARCH_LOCAL(i, name_expr) \
    for (i = local_count - 1; i >= 0; i--) \
        if (strcmp(locals[i].name, (name_expr)) == 0 && \
            locals[i].scope_depth <= scope_depth && \
            (locals[i].scope_id == 0 || \
             (scope_chain_count > 0 && locals[i].scope_id <= scope_chain[scope_chain_count - 1])))\
            break; \
    if (i < 0) \
        for (i = local_count - 1; i >= 0; i--) \
            if (strcmp(locals[i].name, (name_expr)) == 0 && \
                locals[i].scope_depth <= scope_depth) \
                break

/* ─── 函数返回类型表（供 struct 按值返回的 caller 侧使用） ─── */
#define MAX_FUNC_RET_TYPES 4096
extern const char *func_ret_names[MAX_FUNC_RET_TYPES];
extern int func_ret_sizes[MAX_FUNC_RET_TYPES];
extern int func_ret_count;
static inline int get_func_ret_size(const char *name) {
    int i;
    if (!name) return 0;
    for (i = 0; i < func_ret_count; i++)
        if (func_ret_names[i] && strcmp(func_ret_names[i], name) == 0)
            return func_ret_sizes[i];
    return 0;
}

/* 解析期记录的函数返回类型表（原型 + 定义的超集） */
extern const char *parsed_func_ret_names[MAX_FUNC_RET_TYPES];
extern int parsed_func_ret_sizes[MAX_FUNC_RET_TYPES];
extern int parsed_func_ret_float[MAX_FUNC_RET_TYPES];
extern int parsed_func_ret_unsigned[MAX_FUNC_RET_TYPES];
extern int parsed_func_ret_count;

/* ─── 类型系统（Phase 3） ─── */

#define MAX_MEMBERS 256
#define MAX_TAGS 2048
#define MAX_TYPEDEFS 4096

/* 结构体成员描述 */
typedef struct {
    const char *name;
    int offset;
    int size;
    int elem_size;      /* 指针成员指向的元素大小（long*→8, int*→4），数组成员的元素大小 */
    int is_unsigned;    /* 成员是否为 unsigned 类型 */
    int is_float;       /* 0=非浮点, 4=float, 8=double */
    int memb_is_array;  /* 1=数组成员, 0=指针或标量（用于 init 数据发射宽度计算） */
    const char *member_struct_tag;  /* 如果此成员本身是 struct 类型（非指针），存 struct 标签名；否则 NULL */
} Member;

/* 结构体类型（通过 struct 标签或匿名定义） */
typedef struct StructType {
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
    int type_kind;   /* 0=基本, 1=struct, 2=pointer */
    int ptr_level;   /* 指针层数（0=非指针） */
    int points_to;   /* 指针指向的类型大小（ptr_level>0 时有效） */
    int is_unsigned; /* 1 表示 unsigned 基本类型 */
    int struct_idx;
    Member members[MAX_MEMBERS];
    int member_count;
} TypedefEntry;

extern TypedefEntry typedef_table[MAX_TYPEDEFS];
extern int typedef_count;

/* ─── enum 值表 ─── */

#define MAX_ENUM_VALS 8192

typedef struct {
    const char *name;
    int value;
} EnumEntry;

extern EnumEntry enum_vals[MAX_ENUM_VALS];
extern int enum_val_count;

void register_enum_val(const char *name, int value);
int find_enum_val(const char *name);
int find_enum_val_ex(const char *name, int *val);

/* 判断名字是否为 typedef */
int is_typedef_name(const char *name);

/* 从解析期的 pvar 表查找变量大小和数组元素大小（供 cgen 全局数组衰减使用） */
int pvar_lookup_size(const char *name);
int pvar_lookup_elem_size(const char *name);

/* 查找 struct 标签 */
StructType *find_struct_tag(const char *tag);

/* ─── 预处理器 ─── */

#define MAX_INCLUDE_PATHS 64

void add_include_path(const char *path);
char *preprocess(const char *src, int len, const char *fname, int *out_len);

/* ─── 代码生成 ─── */

void cgen_init(void);
void cgen_program(AstNode *prog);
void cgen_expr(AstNode *node);
void cgen_addr(AstNode *node);
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
        __write(2, "toyc: code buffer overflow\n", 26);
        __exit(1);
    }
    code_buf[code_size++] = b & 0xFF;
}
static inline void e4(int v) { e1(v); e1(v>>8); e1(v>>16); e1(v>>24); }

/* ─── [rbp+off] 加载/存储（自动选择 disp8/disp32，修复大帧偏移截断 BUG） ─── */

static inline int disp8_fits(int offset) {
    return offset >= -128 && offset <= 127;
}

/* mov [rbp+off], r64 (REX.W + 0x89, reg = 源寄存器) */
static inline void emit_store_rbp64(int reg, int off) {
    e1(0x48);  e1(0x89);
    if (disp8_fits(off)) { e1(0x45 | (reg << 3)); e1(off & 0xFF); }
    else { e1(0x85 | (reg << 3)); e4(off); }
}

/* mov r64, [rbp+off] (REX.W + 0x8B) */
static inline void emit_load_rbp64(int reg, int off) {
    e1(0x48);  e1(0x8B);
    if (disp8_fits(off)) { e1(0x45 | (reg << 3)); e1(off & 0xFF); }
    else { e1(0x85 | (reg << 3)); e4(off); }
}

/* mov [rbp+off], r32 */
static inline void emit_store_rbp32(int reg, int off) {
    e1(0x89);
    if (disp8_fits(off)) { e1(0x45 | (reg << 3)); e1(off & 0xFF); }
    else { e1(0x85 | (reg << 3)); e4(off); }
}

/* mov r32, [rbp+off] */
static inline void emit_load_rbp32(int reg, int off) {
    e1(0x8B);
    if (disp8_fits(off)) { e1(0x45 | (reg << 3)); e1(off & 0xFF); }
    else { e1(0x85 | (reg << 3)); e4(off); }
}

/* 便捷函数 */
static inline void load_rax_from_rbp(int off) { emit_load_rbp64(0, off); }
static inline void store_rax_to_rbp(int off) { emit_store_rbp64(0, off); }
static inline void load_eax_from_rbp(int off) { emit_load_rbp32(0, off); }
static inline void store_eax_to_rbp(int off) { emit_store_rbp32(0, off); }

/* 8-bit/16-bit 便捷函数 */
static inline void emit_store_rbp16(int off) { e1(0x66); emit_store_rbp32(0, off); }
static inline void emit_store_rbp8(int off) {
    e1(0x88);
    if (disp8_fits(off)) { e1(0x45); e1(off & 0xFF); }
    else { e1(0x85); e4(off); }
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

#endif /* TOYC_H */
