/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * parse.c — C 递归下降解析器（Phase 2）
 *
 * 机制：将 Token 流解析为 AST。每个运算符优先级对应一个解析函数。
 *
 * 表达式优先级（从低到高）：
 *   赋值     = += -= *= /= %= <<= >>= &= ^= |=
 *   三元     ?:
 *   逻辑或   ||
 *   逻辑与   &&
 *   按位或   |
 *   按位异或 ^
 *   按位与   &
 *   相等     == !=
 *   关系     < > <= >=
 *   移位     << >>
 *   加减     + -
 *   乘除     * / %
 *   一元     ! ~ + - ++ -- * & sizeof
 *   后缀     [] () . ->
 *   基本     id num string (expr)
 */

#include "toyc.h"

/* lex.c 中的浮点字面量解析函数：将十进制浮点字符串解析为 IEEE 754 64 位位模式 */
extern void parse_float_literal(const char *s, int len,
                                unsigned int *out_lo, unsigned int *out_hi);

/* 将 64 位 double IEEE 754 位模式转换为 32 位 float IEEE 754 位模式。
 * 纯 32 位整数运算，避免浮点硬件或 tcc 的 double 算术 bug。
 * hi = 双精度高 32 位，lo = 低 32 位。返回 32 位 float 位模式。 */
static unsigned int double_bits_to_float(unsigned int hi, unsigned int lo) {
    unsigned int sign = hi >> 31;
    unsigned int exp_d = (hi >> 20) & 0x7FF;  /* 11-bit biased exponent */

    /* 零值（含 -0.0） */
    if ((hi & 0x7FFFFFFF) == 0 && lo == 0)
        return sign << 31;

    /* 无穷大和 NaN */
    if (exp_d == 0x7FF) {
        int is_nan = (hi & 0xFFFFF) || lo;
        if (is_nan)
            return (sign << 31) | 0x7FC00000;  /* quiet NaN */
        return (sign << 31) | 0x7F800000;      /* Infinity */
    }

    /* 指数钳位：double 偏置 1023 → float 偏置 127 */
    int exp_new = (int)exp_d - 1023 + 127;

    if (exp_new >= 255)
        return (sign << 31) | 0x7F800000;  /* 上溢到无穷大 */

    if (exp_new <= 0)
        return sign << 31;                 /* 下溢到零（简化处理） */

    /* 取 52 位尾数的高 23 位：
     * 双精度尾数布局：hi[19:0] (20 bits) || lo[31:0] (32 bits)
     * 取位 51→29: hi[19:0] << 3 + lo[31:29] */
    unsigned int mant = ((hi & 0xFFFFF) << 3) | (lo >> 29);

    return (sign << 31) | ((unsigned)exp_new << 23) | (mant & 0x7FFFFF);
}

/* 解析 struct 类型时捕获的信息（供变量声明和成员访问追踪） */
static const char *last_struct_tag = NULL;
static Member last_struct_members[MAX_MEMBERS];
static int last_struct_member_count = 0;
static int last_type_is_float = 0;     /* 最近 parse_type_specifier 结果是否为 float/double：0=否, 4=float, 8=double */

/* parse_declarator 在处理 (*name[N])(params) 时捕获的数组维度（供外层声明使用） */
static int pdecl_fptr_array_dim = 0;

/* ─── 局部变量类型追踪（解析阶段用，供 .m 成员访问计算偏移） ─── */

#define MAX_PVARS 4096
static const char *pvar_name[MAX_PVARS];       /* 变量名 */
static const char *pvar_tag[MAX_PVARS];        /* struct 标签 */
static int pvar_is_float_arr[MAX_PVARS];      /* 是否为 double 类型 */
static int pvar_is_unsigned_arr[MAX_PVARS];   /* 是否为 unsigned 类型 */
static int pvar_size_arr[MAX_PVARS];          /* 变量大小（用于 sizeof） */
static int pvar_elem_size_arr[MAX_PVARS];     /* 数组元素大小（0=非数组） */
static int pvar_elem_float_arr[MAX_PVARS];    /* 指针/数组元素的浮点类型 */
static StructType *pvar_struct_type_arr[MAX_PVARS]; /* 解析后的 StructType*（NULL=非 struct 或未知） */
static int pvar_count;

static void pvar_add_ex(const char *name, const char *tag, int is_float, int is_unsigned, int size) {
    if (name && *name) {
        if (pvar_count >= MAX_PVARS) {
            __write(2, "toyc: too many local variables (parser)\n", 39);
            __exit(1); }
        /* 暂不实现变量重定义检测 */
        pvar_name[pvar_count] = name;
        pvar_tag[pvar_count] = tag;
        pvar_is_float_arr[pvar_count] = is_float;
        pvar_is_unsigned_arr[pvar_count] = is_unsigned;
        pvar_size_arr[pvar_count] = size;
        pvar_elem_size_arr[pvar_count] = 0;
        pvar_elem_float_arr[pvar_count] = 0;
        pvar_struct_type_arr[pvar_count] = NULL;
        pvar_count++;
    }
}
static void pvar_set_elem_size(const char *name, int elem_size) {
    int i;
    for (i = 0; i < pvar_count; i++)
        if (strcmp(pvar_name[i], name) == 0)
            { pvar_elem_size_arr[i] = elem_size; return; }
}
static int pvar_find_elem_size(const char *name) {
    int i;
    for (i = 0; i < pvar_count; i++)
        if (strcmp(pvar_name[i], name) == 0) return pvar_elem_size_arr[i];
    return 0;
}
static void pvar_set_elem_float(const char *name, int elem_float) {
    int i;
    for (i = 0; i < pvar_count; i++)
        if (strcmp(pvar_name[i], name) == 0)
            { pvar_elem_float_arr[i] = elem_float; return; }
}
static int pvar_find_elem_float(const char *name) {
    int i;
    for (i = 0; i < pvar_count; i++)
        if (strcmp(pvar_name[i], name) == 0) return pvar_elem_float_arr[i];
    return 0;
}
static void pvar_update_size(const char *name, int new_size) {
    int i;
    for (i = 0; i < pvar_count; i++)
        if (strcmp(pvar_name[i], name) == 0)
            { pvar_size_arr[i] = new_size; return; }
}
#define pvar_add(name, tag, is_float) pvar_add_ex(name, tag, is_float, 0, 0)
static const char *pvar_find_tag(const char *name) {
    int i;
    for (i = pvar_count - 1; i >= 0; i--)
        if (strcmp(pvar_name[i], name) == 0) return pvar_tag[i];
    return NULL;
}
static int pvar_find_size(const char *name) {
    int i;
    for (i = pvar_count - 1; i >= 0; i--) {
        int match = strcmp(pvar_name[i], name);
        if (match == 0) return pvar_size_arr[i];
    }
    return 0;
}
int pvar_lookup_size(const char *name) { return pvar_find_size(name); }
int pvar_lookup_elem_size(const char *name) { return pvar_find_elem_size(name); }
static int pvar_find_float(const char *name) {
    int i;
    for (i = pvar_count - 1; i >= 0; i--)
        if (strcmp(pvar_name[i], name) == 0) return pvar_is_float_arr[i];
    return 0;
}
static int pvar_find_unsigned(const char *name) {
    int i;
    for (i = pvar_count - 1; i >= 0; i--)
        if (strcmp(pvar_name[i], name) == 0) return pvar_is_unsigned_arr[i];
    return 0;
}
static StructType *pvar_find_struct_type(const char *name) {
    int i;
    for (i = pvar_count - 1; i >= 0; i--)
        if (strcmp(pvar_name[i], name) == 0) return pvar_struct_type_arr[i];
    return NULL;
}
static void pvar_set_struct_type(const char *name, StructType *st) {
    int i;
    for (i = pvar_count - 1; i >= 0; i--)
        if (strcmp(pvar_name[i], name) == 0) { pvar_struct_type_arr[i] = st; return; }
}
/* 从 struct 标签名或 typedef 名解析出 StructType* */
static StructType *resolve_struct_type(const char *tag) {
    if (!tag || !*tag) return NULL;
    StructType *st = find_struct_tag(tag);
    if (st) return st;
    /* 回退：搜 typedef 表（匿名 struct typedef 的 tag 就是 typedef 名） */
    int ti;
    for (ti = 0; ti < typedef_count; ti++) {
        if (strcmp(typedef_table[ti].name, tag) == 0 && typedef_table[ti].member_count > 0) {
            return find_struct_tag(tag);  /* 匿名 struct 注册时已用 typedef 名作 tag */
        }
    }
    return NULL;
}
/* 在 struct 中查找成员偏移 */
static int find_member_offset(const char *struct_tag, const char *member) {
    if (struct_tag) {
        /* 查 struct 标签表 */
        StructType *st = find_struct_tag(struct_tag);
        if (st) {
            int i;
            for (i = 0; i < st->member_count; i++)
                if (strcmp(st->members[i].name, member) == 0)
                    return st->members[i].offset;
        }
    }
    return 0;
}

/* 在 struct 中查找成员大小 */
static int find_member_size(const char *struct_tag, const char *member) {
    if (struct_tag) {
        StructType *st = find_struct_tag(struct_tag);
        if (st) {
            int i;
            for (i = 0; i < st->member_count; i++)
                if (strcmp(st->members[i].name, member) == 0)
                    return st->members[i].size;
        }
    }
    return 0;
}

/* ─── 当前类型说明符的有符号性（供变量声明和类型转换使用） ─── */
static int last_type_is_unsigned = 0;

/* ─── 解析期函数返回类型表（供 func().member 成员访问使用） ─── */
#define MAX_PARSE_FUNC_RET 512
static const char *parse_func_ret_name[MAX_PARSE_FUNC_RET];
static StructType *parse_func_ret_type[MAX_PARSE_FUNC_RET];
static int parse_func_ret_count;

/* ─── 类型系统全局表 ─── */

StructType tag_table[MAX_TAGS];
int tag_count;

TypedefEntry typedef_table[MAX_TYPEDEFS];
int typedef_count;

EnumEntry enum_vals[MAX_ENUM_VALS];
int enum_val_count;

void register_enum_val(const char *name, int value) {
    if (enum_val_count >= MAX_ENUM_VALS) {
        __write(2, "toyc: too many enum values\n", 26);
        __exit(1); }
    enum_vals[enum_val_count].name = name;
    enum_vals[enum_val_count].value = value;
    enum_val_count++;
}

int find_enum_val(const char *name) {
    int i;
    for (i = 0; i < enum_val_count; i++)
        if (strcmp(enum_vals[i].name, name) == 0)
            return enum_vals[i].value;
    return -1;
}
int find_enum_val_ex(const char *name, int *val) {
    int i;
    for (i = 0; i < enum_val_count; i++)
        if (strcmp(enum_vals[i].name, name) == 0) { if (val) *val = enum_vals[i].value; return 1; }
    return 0;
}

int is_typedef_name(const char *name) {
    int i;
    for (i = 0; i < typedef_count; i++)
        if (strcmp(typedef_table[i].name, name) == 0) return 1;
    return 0;
}

StructType *find_struct_tag(const char *tag) {
    int i;
    for (i = 0; i < tag_count; i++)
        if (tag_table[i].tag && strcmp(tag_table[i].tag, tag) == 0)
            return &tag_table[i];
    return NULL;
}

static int add_struct_tag(const char *tag, StructType *st) {
    if (tag_count >= MAX_TAGS) {
        __write(2, "toyc: too many struct/union tags\n", 32);
        __exit(1); }
    StructType *s = &tag_table[tag_count++];
    s->tag = tag;
    int i;
    for (i = 0; i < st->member_count; i++)
        s->members[i] = st->members[i];
    s->member_count = st->member_count;
    s->total_size = st->total_size;
    return tag_count - 1;
}

/* ─── Token 名称表（供错误报告使用） ─── */

static const char *token_name(TokenKind kind) {
    switch (kind) {
    case TOK_EOF:             return "end-of-file";
    case TOK_ERROR:           return "error";

    /* 关键字 */
    case TOK_INT:             return "'int'";
    case TOK_VOID:            return "'void'";
    case TOK_CHAR:            return "'char'";
    case TOK_SHORT:           return "'short'";
    case TOK_LONG:            return "'long'";
    case TOK_UNSIGNED:        return "'unsigned'";
    case TOK_SIGNED:          return "'signed'";
    case TOK_RETURN:          return "'return'";
    case TOK_IF:              return "'if'";
    case TOK_ELSE:            return "'else'";
    case TOK_WHILE:           return "'while'";
    case TOK_FOR:             return "'for'";
    case TOK_DO:              return "'do'";
    case TOK_DOUBLE:          return "'double'";
    case TOK_FLOAT:           return "'float'";
    case TOK_BREAK:           return "'break'";
    case TOK_CONTINUE:        return "'continue'";
    case TOK_SWITCH:          return "'switch'";
    case TOK_CASE:            return "'case'";
    case TOK_DEFAULT:         return "'default'";
    case TOK_GOTO:            return "'goto'";
    case TOK_SIZEOF:          return "'sizeof'";
    case TOK_STRUCT:          return "'struct'";
    case TOK_UNION:           return "'union'";
    case TOK_ENUM:            return "'enum'";
    case TOK_TYPEDEF:         return "'typedef'";
    case TOK_CONST:           return "'const'";
    case TOK_VOLATILE:        return "'volatile'";
    case TOK_RESTRICT:        return "'restrict'";
    case TOK_REGISTER:        return "'register'";
    case TOK_STATIC:          return "'static'";
    case TOK_EXTERN:          return "'extern'";
    case TOK_INLINE:          return "'inline'";
    case TOK__ATTRIBUTE__:    return "'__attribute__'";
    case TOK__ASM__:          return "'__asm__'";
    case TOK__BUILTIN_VA_LIST: return "'__builtin_va_list'";
    case TOK__BUILTIN_VA_START: return "'__builtin_va_start'";
    case TOK__BUILTIN_VA_ARG: return "'__builtin_va_arg'";
    case TOK__BUILTIN_VA_END: return "'__builtin_va_end'";

    /* 标识符和字面量 */
    case TOK_IDENT:           return "identifier";
    case TOK_NUMBER:          return "number";
    case TOK_STRING:          return "string literal";

    /* 标点符号 */
    case TOK_SEMI:            return "';'";
    case TOK_LBRACE:          return "'{'";
    case TOK_RBRACE:          return "'}'";
    case TOK_LPAREN:          return "'('";
    case TOK_RPAREN:          return "')'";
    case TOK_LBRACKET:        return "'['";
    case TOK_RBRACKET:        return "']'";
    case TOK_COMMA:           return "','";
    case TOK_DOT:             return "'.'";
    case TOK_ARROW:           return "'->'";
    case TOK_AMPERSAND:       return "'&'";
    case TOK_STAR:            return "'*'";
    case TOK_PLUS:            return "'+'";
    case TOK_MINUS:           return "'-'";
    case TOK_TILDE:           return "'~'";
    case TOK_EXCLAM:          return "'!'";
    case TOK_SLASH:           return "'/'";
    case TOK_PERCENT:         return "'%'";
    case TOK_LESS:            return "'<'";
    case TOK_GREATER:         return "'>'";
    case TOK_LESS_EQ:         return "'<='";
    case TOK_GREATER_EQ:      return "'>='";
    case TOK_EQ_EQ:           return "'=='";
    case TOK_NOT_EQ:          return "'!='";
    case TOK_AND_AND:         return "'&&'";
    case TOK_OR_OR:           return "'||'";
    case TOK_PIPE:            return "'|'";
    case TOK_CARET:           return "'^'";
    case TOK_LESS_LESS:       return "'<<'";
    case TOK_GREATER_GREATER: return "'>>'";
    case TOK_EQ:              return "'='";
    case TOK_PLUS_EQ:         return "'+='";
    case TOK_MINUS_EQ:        return "'-='";
    case TOK_STAR_EQ:         return "'*='";
    case TOK_SLASH_EQ:        return "'/='";
    case TOK_PERCENT_EQ:      return "'%='";
    case TOK_AND_EQ:          return "'&='";
    case TOK_OR_EQ:           return "'|='";
    case TOK_CARET_EQ:        return "'^='";
    case TOK_LESS_LESS_EQ:    return "'<<='";
    case TOK_GREATER_GREATER_EQ: return "'>>='";
    case TOK_PLUS_PLUS:       return "'++'";
    case TOK_MINUS_MINUS:     return "'--'";
    case TOK_QUESTION:        return "'?'";
    case TOK_COLON:           return "':'";
    case TOK_ELLIPSIS:        return "'...'";
    }
    return "unknown token";
}

/* 从 token 中提取简短的可读词素（用于显示实际出现的符号） */
static void token_lexeme(Parser *p, char *buf, int bufsz) {
    Token t = p->tok;
    if (t.kind == TOK_IDENT || t.kind == TOK_NUMBER || t.kind == TOK_STRING) {
        int i;
        int max = t.len < bufsz - 1 ? t.len : bufsz - 1;
        for (i = 0; i < max; i++) buf[i] = t.start[i];
        buf[max] = '\0';
    } else if (t.kind == TOK_EOF) {
        buf[0] = '<'; buf[1] = 'E'; buf[2] = 'O'; buf[3] = 'F'; buf[4] = '>'; buf[5] = '\0';
    } else if (t.kind == TOK_ERROR) {
        buf[0] = '<'; buf[1] = 'l'; buf[2] = 'e'; buf[3] = 'x'; buf[4] = '-'; buf[5] = 'e'; buf[6] = 'r'; buf[7] = 'r'; buf[8] = '>'; buf[9] = '\0';
    } else {
        const char *tn = token_name(t.kind);
        int i; for (i = 0; tn[i] && i < bufsz - 1; i++) buf[i] = tn[i];
        buf[i] = '\0';
    }
}

/* ─── 错误报告 ─── */

/* 写 ANSI 转义序列：ESC + s（避免 \033 被 bootstrap/tcc 误译成 \0+33） */
static void esc(const char *s, int len) {
    char e = 27;
    __write(2, &e, 1);
    __write(2, s, len);
}

/* 格式化行号：右对齐 width 宽度 + " | "，写入 fd 2 */
static void lineno_prefix(int line, int width) {
    char rev[16];
    int ri = 0, pd, i;
    if (line == 0) rev[ri++] = '0';
    else { int n = line; while (n > 0) { rev[ri++] = '0' + (n % 10); n /= 10; } }
    pd = width - ri;  /* 前导空格数 */
    if (pd < 0) pd = 0;
    for (i = 0; i < pd; i++) __write(2, " ", 1);
    while (ri > 0) __write(2, rev + --ri, 1);
    __write(2, " | ", 3);
}

void error_at(Parser *p, const char *msg) {
    if (p->error_count >= MAX_ERRORS) { p->had_error = 1; return; }
    p->error_count++;
    char esc = 27;
    int err_line = p->lexer->line;
    const char *fn = p->lexer->filename ? p->lexer->filename : "<unknown>";
    __write(2, &esc, 1); __write(2, "[1;31merror:", 12);
    __write(2, &esc, 1); __write(2, "[0m", 3);
    __eprintf(" %s:%d:%d: %s\n", fn, err_line, p->lexer->col, msg);

    if (p->tok.start) {
        const char *tokstart = p->tok.start;
        const char *bol = tokstart;
        while (bol > tokstart - 200 && *(bol - 1) != '\n' && *(bol - 1) != '\0') bol--;
        const char *eol = tokstart;
        while (eol < p->lexer->end && *eol != '\n') eol++;

        if (bol < tokstart && *bol) {
            const char *pb = bol - 1;
            while (pb > bol - 200 && *(pb - 1) != '\n' && *(pb - 1) != '\0') pb--;
            if (pb < bol && *pb != '\0') {
                __write(2, &esc, 1); __write(2, "[2m", 3);
                lineno_prefix(err_line - 1, 3);
                { const char *cp; for (cp = pb; cp < bol; cp++) __write(2, cp, 1); }
                __write(2, &esc, 1); __write(2, "[0m\n", 4);
            }
        }
        lineno_prefix(err_line, 3);
        { const char *cp; for (cp = bol; cp < eol; cp++) __write(2, cp, 1); }
        __write(2, "\n", 1);
        __write(2, "    ", 4);
        { int _si; int _sp = (int)(tokstart - bol); if (_sp > 1024) _sp = 0;
          for (_si = 0; _si < _sp; _si++) __write(2, " ", 1); }
        __write(2, &esc, 1); __write(2, "[1;31m^", 7);
        if (p->tok.len > 1 && (p->tok.kind == TOK_IDENT || p->tok.kind == TOK_NUMBER)) {
            int ti;
            for (ti = 1; ti < p->tok.len && ti < 40; ti++) __write(2, "~", 1);
        }
        __write(2, &esc, 1); __write(2, "[0m\n", 4);
    }
    p->had_error = 1;
}

/* ─── Token 辅助 ─── */

static Token peek(Parser *p) { return p->tok; }
static Token consume(Parser *p) { Token t = p->tok; p->tok = lexer_next(p->lexer); return t; }

static int match(Parser *p, TokenKind kind) {
    if (p->tok.kind == kind) { consume(p); return 1; }
    return 0;
}

static int expect(Parser *p, TokenKind kind) {
    if (p->tok.kind == kind) { consume(p); return 1; }
    if (p->error_count >= MAX_ERRORS) { p->had_error = 1; return 0; }
    p->error_count++;
    char lexeme[64];
    token_lexeme(p, lexeme, sizeof(lexeme));
    const char *fn = p->lexer->filename ? p->lexer->filename : "<unknown>";
    __eprintf("error: %s:%d:%d: expected %s but got %s\n",
             fn, p->lexer->line, p->lexer->col,
             token_name(kind), lexeme);
    p->had_error = 1;
    return 0;
}

/* 恐慌模式错误恢复：跳过 token 到下一个 ; 或 }，减少级联报错 */
static void error_recover(Parser *p) {
    int depth = 0;
    while (p->tok.kind != TOK_EOF && p->tok.kind != TOK_SEMI
           && p->tok.kind != TOK_RBRACE) {
        if (p->tok.kind == TOK_LBRACE) depth++;
        if (p->tok.kind == TOK_RBRACE) { if (depth == 0) break; depth--; }
        consume(p);
    }
}

/* ─── 初始化 ─── */

void parser_init(Parser *p, Lexer *lx, Arena *a) {
    p->lexer = lx;
    p->arena = a;
    p->had_error = 0;
    p->error_count = 0;
    p->func_depth = 0;
    p->loop_depth = 0;
    p->switch_depth = 0;
    p->block_depth = 0;
    p->tok = lexer_next(lx);
    enum_val_count = 0;  /* 重置 enum 表 */
}

/* 前向声明 */
static AstNode *parse_expr(Parser *p);
static AstNode *parse_expr_comma(Parser *p);
static AstNode *parse_statement(Parser *p);
static AstNode *parse_compound_statement(Parser *p);
int parse_type_specifier(Parser *p);

/* ─── 一元表达式的解析 ─── */
/* 按优先级从低到高定义 */

static AstNode *new_ast(Parser *p, AstKind kind) {
    AstNode *n = arena_alloc(p->arena, sizeof(AstNode));
    n->kind = kind;
    n->next = NULL;
    n->type_size = 4;  /* 默认 int 大小 */
    n->elem_size = 0;  /* 必须初始化（arena_alloc 不归零） */
    n->is_float = 0;
    n->is_static = 0;
    n->is_variadic = 0;
    n->ival = 0;
    n->dval = 0.0;
    n->op = 0;
    n->base_elem_size = 0;
    n->is_array = 0;
    n->call_target = NULL;
    n->struct_type = NULL;
    return n;
}

/* 解码字符串字面量（去除引号，处理转义序列） */
static const char *decode_string_literal(Parser *p, const Token *t) {
    const char *src = t->start + 1;      /* 跳过开头的 " */
    int src_len = t->len - 2;            /* 去掉两端引号 */
    char *dst = arena_alloc(p->arena, src_len + 1);
    char *out = dst;
    int i = 0;
    while (i < src_len) {
        char c = src[i++];
        if (c == '\\' && i < src_len) {
            char esc = src[i++];
            switch (esc) {
            case 'n': *out++ = '\n'; break;
            case 't': *out++ = '\t'; break;
            case 'r': *out++ = '\r'; break;
            case '0': *out++ = '\0'; break;
            case '\\': *out++ = '\\'; break;
            case '"': *out++ = '"'; break;
            default:
                /* \NNN octal escape */
                if (esc >= '0' && esc <= '7') {
                    int oct_val = esc - '0';
                    int od = 1;
                    while (od < 3 && i < src_len) {
                        char oc = src[i];
                        if (oc >= '0' && oc <= '7') { oct_val = oct_val * 8 + (oc - '0'); i++; od++; }
                        else break;
                    }
                    *out++ = (char)oct_val;
                } else {
                    *out++ = esc;
                }
                break;
            }
        } else {
            *out++ = c;
        }
    }
    *out = '\0';
    return dst;
}

/* 基本表达式: identifier, number, string, (expr) */
static AstNode *parse_primary(Parser *p) {
    Token t = peek(p);

    if (t.kind == TOK_NUMBER) {
        consume(p);
        AstNode *n = new_ast(p, AST_CONSTANT);
        n->is_float = t.is_float;
        if (t.is_float) {
            /* 浮点常量：在 name/ival 中存源字符串指针和长度，
             * 在代码生成时用 32 位整数运算解析 IEEE 754 位模式。 */
            n->name = t.start;
            n->ival = t.len;
        } else {
            n->ival = t.ival;
        }
        /* 设置 type_size：浮点类型用 is_float 值（4=float, 8=double）；
         * 整数在 32 位范围内用 4 字节，否则 8 字节 */
        if (n->is_float)
            n->type_size = n->is_float;
        else if (t.is_unsigned)
            n->type_size = (t.ival >= 0 && t.ival <= 4294967295L) ? 4 : 8;
        else if (t.ival >= -2147483648L && t.ival <= 2147483647L)
            n->type_size = 4;
        else
            n->type_size = 8;
        /* unsigned 判定：优先用 Token 记录的 U/UL 后缀，无后缀时用启发式 */
        if (t.is_unsigned)
            n->is_unsigned = 1;
        else if ((unsigned long)t.ival > 2147483647UL)
            n->is_unsigned = 1;
        else
            n->is_unsigned = 0;
        return n;
    }

    /* __builtin_huge_val() / __builtin_huge_valf() → double/float 正无穷常量 */
    if (t.kind == TOK__BUILTIN_HUGE_VAL || t.kind == TOK__BUILTIN_HUGE_VALF) {
        consume(p);
        expect(p, TOK_LPAREN);
        expect(p, TOK_RPAREN);
        AstNode *n = new_ast(p, AST_CONSTANT);
        n->is_float = (t.kind == TOK__BUILTIN_HUGE_VAL) ? 8 : 4;
        n->type_size = n->is_float;
        n->name = NULL;  /* NULL → 代码生成时使用预计算位模式 */
        n->ival = 0x7FF0000000000000LL;  /* double 正无穷 IEEE 754 位模式 */
        return n;
    }

    if (t.kind == TOK_IDENT ||
        t.kind == TOK__BUILTIN_VA_START || t.kind == TOK__BUILTIN_VA_ARG ||
        t.kind == TOK__BUILTIN_VA_END || t.kind == TOK__BUILTIN_VA_LIST ||
        t.kind == TOK__ASM__ || t.kind == TOK__ATTRIBUTE__) {
        consume(p);
        /* 检查是否为 enum 常量 */
        if (t.kind == TOK_IDENT) {
            int eval = 0;
            if (find_enum_val_ex(arena_strdup(p->arena, t.start, t.len), &eval)) {
                AstNode *n = new_ast(p, AST_CONSTANT);
                n->ival = eval;
                if (eval >= -2147483648L && eval <= 2147483647L)
                    n->type_size = 4;
                else
                    n->type_size = 8;
                if (eval > 2147483647L)
                    n->is_unsigned = 1;
                return n;
            }
        }
        AstNode *n = new_ast(p, AST_VAR);
        n->name = arena_strdup(p->arena, t.start, t.len);
        n->is_float = pvar_find_float(n->name);
        n->is_unsigned = pvar_find_unsigned(n->name);
        n->struct_type = pvar_find_struct_type(n->name);
        return n;
    }

    if (t.kind == TOK_STRING) {
        consume(p);
        /* 收集所有相邻的字符串字面量（C 标准字符串连接） */
        Token str_tokens[64];
        int str_count = 0;
        str_tokens[str_count++] = t;
        while (peek(p).kind == TOK_STRING && str_count < 64)
            str_tokens[str_count++] = consume(p);
        /* 计算总长度 */
        int total = 0;
        int si;
        for (si = 0; si < str_count; si++) {
            const char *src = str_tokens[si].start + 1;
            int src_len = str_tokens[si].len - 2;
            int i = 0;
            while (i < src_len) {
                if (src[i] == '\\' && i + 1 < src_len) { i += 2; total++; }
                else { i++; total++; }
            }
        }
        char *dst = arena_alloc(p->arena, total + 1);
        int pos = 0;
        for (si = 0; si < str_count; si++) {
            const char *src = str_tokens[si].start + 1;
            int src_len = str_tokens[si].len - 2;
            int i = 0;
            while (i < src_len) {
                char c = src[i++];
                if (c == '\\' && i < src_len) {
                    char esc = src[i++];
                    switch (esc) {
                    case 'n': dst[pos++] = '\n'; break;
                    case 't': dst[pos++] = '\t'; break;
                    case 'r': dst[pos++] = '\r'; break;
                    case 'e': dst[pos++] = 27; break;   /* \e → ESC */
                    case '\\': dst[pos++] = '\\'; break;
                    case '"': dst[pos++] = '"'; break;
                    case 'x': {
                        /* \xAB hex escape in strings */
                        int hex_val = 0, hd = 0;
                        while (hd < 2 && i < src_len) {
                            char hc = src[i];
                            if (hc >= '0' && hc <= '9') { hex_val = hex_val * 16 + (hc - '0'); i++; hd++; }
                            else if (hc >= 'a' && hc <= 'f') { hex_val = hex_val * 16 + (hc - 'a' + 10); i++; hd++; }
                            else if (hc >= 'A' && hc <= 'F') { hex_val = hex_val * 16 + (hc - 'A' + 10); i++; hd++; }
                            else break;
                        }
                        dst[pos++] = (char)(hd > 0 ? hex_val : 'x');
                        break;
                    }
                    default:
                        /* \NNN octal escape (also handles \0, \00, \033, etc.) */
                        if (esc >= '0' && esc <= '7') {
                            int oct_val = esc - '0';
                            int od = 1;
                            while (od < 3 && i < src_len) {
                                char oc = src[i];
                                if (oc >= '0' && oc <= '7') { oct_val = oct_val * 8 + (oc - '0'); i++; od++; }
                                else break;
                            }
                            dst[pos++] = (char)oct_val;
                        } else {
                            dst[pos++] = esc;
                        }
                        break;
                    }
                } else {
                    dst[pos++] = c;
                }
            }
        }
        dst[pos] = '\0';
        AstNode *n = new_ast(p, AST_STRING);
        n->str_val = dst;
        return n;
    }

    if (t.kind == TOK_LPAREN) {
        consume(p);
        AstNode *n = parse_expr_comma(p);
        expect(p, TOK_RPAREN);
        return n;
    }

    if (t.kind == TOK_ERROR) {
        error_at(p, p->lexer->lex_err ? p->lexer->lex_err : "unrecognized character or invalid token");
        p->lexer->lex_err = 0;
        consume(p);
        return NULL;
    }

    error_at(p, "expected expression");
    return NULL;
}

/* 后缀表达式: f()  a[i]  s.m  p->m  x++  x-- */
static AstNode *parse_postfix(Parser *p) {
    AstNode *left = parse_primary(p);
    if (!left) return NULL;

    while (1) {
        Token t = peek(p);

        if (t.kind == TOK_LPAREN) {
            /* 函数调用 */
            AstNode *call = new_ast(p, AST_CALL);
            call->name = left->name;
            call->call_target = left;
            call->args = NULL;
            consume(p);
            AstNode **tail = &call->args;
            while (peek(p).kind != TOK_RPAREN && peek(p).kind != TOK_EOF) {
                /* 先检查是否类型关键字（__builtin_va_arg 的类型参数） */
                if (call->name && (peek(p).kind == TOK_INT || peek(p).kind == TOK_CHAR ||
                    peek(p).kind == TOK_SHORT || peek(p).kind == TOK_LONG ||
                    peek(p).kind == TOK_VOID || peek(p).kind == TOK_DOUBLE ||
                    peek(p).kind == TOK_FLOAT ||
                    peek(p).kind == TOK_UNSIGNED || peek(p).kind == TOK_SIGNED ||
                    peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE)) {
                    int first_kind = peek(p).kind;
                    int tsz = parse_type_specifier(p);
                    /* 处理指针类型：char*, const void* 等 */
                    while (peek(p).kind == TOK_STAR || peek(p).kind == TOK_CONST ||
                           peek(p).kind == TOK_VOLATILE || peek(p).kind == TOK_RESTRICT) {
                        if (peek(p).kind == TOK_STAR) tsz = 8;
                        consume(p);
                    }
                    if (tsz > 0) {
                        *tail = new_ast(p, AST_CONSTANT);
                        (*tail)->ival = tsz;
                        if (first_kind == TOK_FLOAT || first_kind == TOK_DOUBLE)
                            (*tail)->is_float = tsz;
                        (*tail)->is_unsigned = last_type_is_unsigned;
                    }
                } else {
                    *tail = parse_expr(p);
                }
                /* 回退路径：parse_expr 返回 NULL 时尝试类型 */
                if (*tail == NULL && call->name) {
                    int first_kind = peek(p).kind;
                    int tsz = parse_type_specifier(p);
                    /* 处理 const 后的指针星号 */
                    while (peek(p).kind == TOK_STAR || peek(p).kind == TOK_CONST)
                        { if (peek(p).kind == TOK_STAR) tsz = 8; consume(p); }
                    if (tsz > 0) {
                        *tail = new_ast(p, AST_CONSTANT);
                        (*tail)->ival = tsz;
                        if (first_kind == TOK_FLOAT || first_kind == TOK_DOUBLE)
                            (*tail)->is_float = tsz;
                        (*tail)->is_unsigned = last_type_is_unsigned;
                    }
                }
                if (*tail) {
                    tail = &(*tail)->next;
                }
                if (peek(p).kind == TOK_COMMA) consume(p);
                else break;
            }
            expect(p, TOK_RPAREN);
            /* 启发式：若任一实参为 float 且函数返回类型未知，则假定函数返回 float/double */
            {
                AstNode *a;
                for (a = call->args; a; a = a->next)
                    if (a->is_float) {
                        int _known = 0;
                        /* 优先使用解析期记录的函数返回类型（函数原型或定义提供了准确信息） */
                        if (call->name) {
                            int _fi;
                            for (_fi = 0; _fi < parsed_func_ret_count; _fi++) {
                                if (parsed_func_ret_names[_fi] &&
                                    strcmp(parsed_func_ret_names[_fi], call->name) == 0) {
                                    if (parsed_func_ret_float[_fi])
                                        call->is_float = parsed_func_ret_float[_fi];
                                    _known = 1;
                                    break;
                                }
                            }
                        }
                        /* 回退：函数未记录（隐式声明），用参数类型猜测 */
                        if (!_known)
                            call->is_float = a->is_float;
                        break;
                    }
            }
            /* 查找解析期记录的 struct 返回类型（供 func().member 使用） */
            if (call->name) {
                int fi;
                for (fi = 0; fi < parse_func_ret_count; fi++) {
                    if (strcmp(parse_func_ret_name[fi], call->name) == 0) {
                        call->struct_type = parse_func_ret_type[fi];
                        break;
                    }
                }
            }
            left = call;

        } else if (t.kind == TOK_LBRACKET) {
            /* 数组下标 a[i] */
            consume(p);
            AstNode *idx = parse_expr(p);
            expect(p, TOK_RBRACKET);
            AstNode *n = new_ast(p, AST_BINOP);
            n->op = TOK_LBRACKET;  /* 用 op 标记下标操作 */
            n->left = left;
            n->right = idx;
            /* a[i] 继承 left（数组/指针）的 struct 类型和浮点类型 */
            n->struct_type = left ? left->struct_type : NULL;
            if (left && left->is_float)
                n->is_float = left->is_float;
            left = n;

        } else if (t.kind == TOK_DOT) {
            consume(p);
            Token m = consume(p);
            AstNode *n = new_ast(p, AST_MEMBER);
            n->left = left;
            n->member_name = arena_strdup(p->arena, m.start, m.len);
            n->op = TOK_DOT;
            n->ival = -1;  /* sentinel: not found yet */
            /* 解析 struct 类型：优先通过 left->struct_type 传播链 */
            StructType *st = NULL;
            if (left && left->struct_type) {
                st = left->struct_type;
            } else if (left) {
                /* 回退 1：从 pvar 表查找（AST_VAR 或 a[i] 中的变量） */
                const char *pvar_tag_name = NULL;
                if (left->kind == AST_VAR) {
                    pvar_tag_name = pvar_find_tag(left->name);
                } else if (left->kind == AST_BINOP && left->op == TOK_LBRACKET &&
                           left->left && left->left->kind == AST_VAR) {
                    pvar_tag_name = pvar_find_tag(left->left->name);
                }
                if (pvar_tag_name)
                    st = find_struct_tag(pvar_tag_name);
                if (!st && pvar_tag_name) {
                    /* 回退 1b：搜 typedef 表（匿名 struct typedef） */
                    int ti;
                    for (ti = 0; ti < typedef_count; ti++) {
                        if (strcmp(typedef_table[ti].name, pvar_tag_name) == 0 && typedef_table[ti].member_count > 0) {
                            st = find_struct_tag(pvar_tag_name);
                            break;
                        }
                    }
                }
            }
            if (st) {
                int fi;
                for (fi = 0; fi < st->member_count; fi++) {
                    if (strcmp(st->members[fi].name, n->member_name) == 0) {
                        n->ival = st->members[fi].offset;
                        n->type_size = st->members[fi].size;
                        n->is_unsigned = st->members[fi].is_unsigned;
                        n->is_float = st->members[fi].is_float;
                        n->elem_size = st->members[fi].elem_size;
                        n->is_array = st->members[fi].memb_is_array;
                        /* 如果此成员是 struct 类型，传播 struct_type 供链式访问 */
                        if (st->members[fi].member_struct_tag)
                            n->struct_type = find_struct_tag(st->members[fi].member_struct_tag);
                        break;
                    }
                }
            }
            /* 回退 2：全局按名搜索（带歧义检测），仅对非 AST_CALL 执行 */
            if (n->ival == -1 && left && left->struct_type) {
                StructType *found_st = NULL;
                int ambiguous = 0;
                int ti, mi;
                for (ti = 0; ti < tag_count && !ambiguous; ti++) {
                    for (mi = 0; mi < tag_table[ti].member_count; mi++) {
                        if (strcmp(tag_table[ti].members[mi].name, n->member_name) == 0) {
                            if (found_st) {
                                /* 第二个 struct 也有同名成员 → 歧义，放弃 */
                                ambiguous = 1;
                                n->ival = -1;
                                break;
                            }
                            found_st = &tag_table[ti];
                            n->ival = tag_table[ti].members[mi].offset;
                            n->type_size = tag_table[ti].members[mi].size;
                            n->is_unsigned = tag_table[ti].members[mi].is_unsigned;
                            n->is_float = tag_table[ti].members[mi].is_float;
                            n->elem_size = tag_table[ti].members[mi].elem_size;
                            n->is_array = tag_table[ti].members[mi].memb_is_array;
                            if (tag_table[ti].members[mi].member_struct_tag)
                                n->struct_type = find_struct_tag(tag_table[ti].members[mi].member_struct_tag);
                        }
                    }
                }
                if (ambiguous)
                    n->ival = -1;  /* 歧义：保持 -1，由下文错误处理 */
            }
            /* 仅在左值的 struct_type 已知且成员未找到时报错。
             * 若 struct_type 为 NULL（如函数返回值、未追踪类型），
             * 保持向后兼容，由代码生成阶段处理。 */
            if (n->ival == -1 && left && left->struct_type) {
                if (p->error_count < MAX_ERRORS) {
                    p->error_count++;
                    __eprintf("error: %s:%d:%d: no member named '%s' in struct/union\n",
                             p->lexer->filename ? p->lexer->filename : "<unknown>",
                             p->lexer->line, p->lexer->col,
                             n->member_name ? n->member_name : "");
                    p->had_error = 1;
                }
            }
            left = n;

        } else if (t.kind == TOK_ARROW) {
            consume(p);
            Token m = consume(p);
            AstNode *n = new_ast(p, AST_MEMBER);
            n->left = left;
            n->member_name = arena_strdup(p->arena, m.start, m.len);
            n->op = TOK_ARROW;
            n->ival = -1;  /* sentinel: not found yet */
            /* 解析 struct 类型（-> 的左操作数是指针，其 struct_type 指被指向的 struct） */
            StructType *st = NULL;
            if (left && left->struct_type) {
                st = left->struct_type;
            } else if (left) {
                /* 回退 1：从 pvar 表查找 */
                const char *pvar_tag_name = NULL;
                if (left->kind == AST_VAR) {
                    pvar_tag_name = pvar_find_tag(left->name);
                } else if (left->kind == AST_BINOP && left->op == TOK_LBRACKET &&
                           left->left && left->left->kind == AST_VAR) {
                    pvar_tag_name = pvar_find_tag(left->left->name);
                }
                if (pvar_tag_name)
                    st = find_struct_tag(pvar_tag_name);
                if (!st && pvar_tag_name) {
                    int ti;
                    for (ti = 0; ti < typedef_count; ti++) {
                        if (strcmp(typedef_table[ti].name, pvar_tag_name) == 0 && typedef_table[ti].member_count > 0) {
                            st = find_struct_tag(pvar_tag_name);
                            break;
                        }
                    }
                }
            }
            if (st) {
                int fi;
                for (fi = 0; fi < st->member_count; fi++) {
                    if (strcmp(st->members[fi].name, n->member_name) == 0) {
                        n->ival = st->members[fi].offset;
                        n->type_size = st->members[fi].size;
                        n->is_unsigned = st->members[fi].is_unsigned;
                        n->is_float = st->members[fi].is_float;
                        n->elem_size = st->members[fi].elem_size;
                        n->is_array = st->members[fi].memb_is_array;
                        if (st->members[fi].member_struct_tag)
                            n->struct_type = find_struct_tag(st->members[fi].member_struct_tag);
                        break;
                    }
                }
            }
            /* 回退 2：全局按名搜索（带歧义检测） */
            if (n->ival == -1) {
                StructType *found_st = NULL;
                int ambiguous = 0;
                int ti, mi;
                for (ti = 0; ti < tag_count && !ambiguous; ti++) {
                    for (mi = 0; mi < tag_table[ti].member_count; mi++) {
                        if (strcmp(tag_table[ti].members[mi].name, n->member_name) == 0) {
                            if (found_st) {
                                ambiguous = 1;
                                n->ival = -1;
                                break;
                            }
                            found_st = &tag_table[ti];
                            n->ival = tag_table[ti].members[mi].offset;
                            n->type_size = tag_table[ti].members[mi].size;
                            n->is_unsigned = tag_table[ti].members[mi].is_unsigned;
                            n->is_float = tag_table[ti].members[mi].is_float;
                            n->elem_size = tag_table[ti].members[mi].elem_size;
                            n->is_array = tag_table[ti].members[mi].memb_is_array;
                            if (tag_table[ti].members[mi].member_struct_tag)
                                n->struct_type = find_struct_tag(tag_table[ti].members[mi].member_struct_tag);
                        }
                    }
                }
                if (ambiguous)
                    n->ival = -1;
            }
            if (n->ival == -1 && left && left->struct_type) {
                if (p->error_count < MAX_ERRORS) {
                    p->error_count++;
                    __eprintf("error: %s:%d:%d: no member named '%s' in struct/union\n",
                             p->lexer->filename ? p->lexer->filename : "<unknown>",
                             p->lexer->line, p->lexer->col,
                             n->member_name ? n->member_name : "");
                    p->had_error = 1;
                }
            }
            left = n;

        } else if (t.kind == TOK_PLUS_PLUS) {
            consume(p);
            AstNode *n = new_ast(p, AST_UNARY);
            n->op = TOK_PLUS_PLUS;
            n->is_postfix = 1;
            n->expr = left;
            left = n;

        } else if (t.kind == TOK_MINUS_MINUS) {
            consume(p);
            AstNode *n = new_ast(p, AST_UNARY);
            n->op = TOK_MINUS_MINUS;
            n->is_postfix = 1;
            n->expr = left;
            left = n;

        } else {
            break;
        }
    }
    return left;
}

/* 一元表达式: ! ~ + - * & ++ -- sizeof */
static AstNode *parse_unary(Parser *p) {
    Token t = peek(p);

    if (t.kind == TOK_PLUS_PLUS || t.kind == TOK_MINUS_MINUS ||
        t.kind == TOK_AMPERSAND || t.kind == TOK_STAR ||
        t.kind == TOK_PLUS || t.kind == TOK_MINUS ||
        t.kind == TOK_TILDE || t.kind == TOK_EXCLAM) {
        consume(p);
        AstNode *n = new_ast(p, AST_UNARY);
        n->op = t.kind;
        n->expr = parse_unary(p);
        /* +/-, ++/--, * 保留操作数的浮点类型 */
        if ((t.kind == TOK_PLUS || t.kind == TOK_MINUS ||
             t.kind == TOK_PLUS_PLUS || t.kind == TOK_MINUS_MINUS) &&
            n->expr && n->expr->is_float)
            n->is_float = n->expr->is_float;
        /* *ptr：检查指针指向的浮点类型 */
        if (t.kind == TOK_STAR && n->expr && n->expr->kind == AST_VAR && n->expr->name) {
            int ef = pvar_find_elem_float(n->expr->name);
            if (ef) n->is_float = ef;
        }
        return n;
    }

    if (t.kind == TOK_SIZEOF) {
        consume(p);
        AstNode *n = new_ast(p, AST_CONSTANT);
        if (peek(p).kind == TOK_LPAREN) {
            const char *sp = p->lexer->pos;
            int sl = p->lexer->line, sc = p->lexer->col;
            Token st = p->tok;
            consume(p);
            int sz = parse_type_specifier(p);
            if (sz >= 0) {
                /* 跳过指针星号和限定符：sizeof(char*), sizeof(const int*) 等 */
                int ptr_stars = 0;
                while (peek(p).kind == TOK_STAR) { consume(p); ptr_stars++; }
                while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
                       peek(p).kind == TOK_RESTRICT) { consume(p); }
                if (peek(p).kind == TOK_RPAREN) {
                    consume(p);
                    n->ival = (ptr_stars > 0) ? 8 : (sz > 0 ? sz : 4);
                    return n;
                }
            }
            /* sizeof(expr) — 回退：尝试推断变量的大小 */
            p->lexer->pos = sp; p->lexer->line = sl;
            p->lexer->col = sc; p->tok = st;
            consume(p);
            AstNode *sexpr = parse_expr(p);
            expect(p, TOK_RPAREN);
            n->ival = 8;
            if (sexpr && sexpr->kind == AST_VAR && sexpr->name) {
                int vsize = pvar_find_size(sexpr->name);
                if (vsize > 0)
                    n->ival = vsize;
            }
            /* sizeof(arr[i]) — 数组下标表达式：返回元素类型大小 */
            if (sexpr && sexpr->kind == AST_BINOP && sexpr->op == TOK_LBRACKET &&
                sexpr->left && sexpr->left->kind == AST_VAR && sexpr->left->name) {
                int es = pvar_find_elem_size(sexpr->left->name);
                if (es > 0) n->ival = es;
            }
        } else {
            n->ival = 4;
        }
        return n;
    }

    /* (type)expr — 类型转换 */
    if (t.kind == TOK_LPAREN) {
        const char *save_pos = p->lexer->pos;
        int save_line = p->lexer->line;
        int save_col = p->lexer->col;
        Token save_tok = p->tok;

        consume(p);
        /* 跳过限定符 const/volatile/restrict */
        while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
               peek(p).kind == TOK_RESTRICT) consume(p);
        int cast_to_double = (peek(p).kind == TOK_DOUBLE);
        int cast_to_float = (peek(p).kind == TOK_FLOAT);
        int csz = parse_type_specifier(p);
        int cast_ptr_stars = 0;
        if (csz >= 0) {
            /* 跳过星号和更多限定符（处理 char *, const unsigned char * 等） */
            while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
                   peek(p).kind == TOK_RESTRICT) consume(p);
            while (peek(p).kind == TOK_STAR) {
                consume(p);
                cast_ptr_stars++;
                while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
                       peek(p).kind == TOK_RESTRICT) consume(p);
            }
            /* 处理复杂指针类型：(*name), (*name)[N], (*name)(params) 等 */
            while (peek(p).kind == TOK_LPAREN) {
                int depth = 1; consume(p);
                while (depth > 0 && peek(p).kind != TOK_EOF) {
                    if (peek(p).kind == TOK_LPAREN) depth++;
                    if (peek(p).kind == TOK_RPAREN) depth--;
                    if (depth) consume(p);
                }
                if (peek(p).kind == TOK_RPAREN) consume(p);
                /* 数组后缀：(*)[N] */
                while (peek(p).kind == TOK_LBRACKET) {
                    int d2 = 1; consume(p);
                    while (d2 > 0 && peek(p).kind != TOK_EOF) {
                        if (peek(p).kind == TOK_LBRACKET) d2++;
                        if (peek(p).kind == TOK_RBRACKET) d2--;
                        if (d2) consume(p);
                    }
                    if (peek(p).kind == TOK_RBRACKET) consume(p);
                }
            }
            if (peek(p).kind == TOK_RPAREN) {
                consume(p);
                AstNode *inner = parse_unary(p);
                if (inner) {
                    if (cast_ptr_stars == 0) {
                        inner->type_size = csz;
                        inner->is_unsigned = last_type_is_unsigned;
                        /* 对 signed char/short 常量值做截断符号扩展 */
                        if (inner->kind == AST_CONSTANT) {
                            if (csz == 1 && !last_type_is_unsigned)
                                inner->ival = (long)(signed char)(int)inner->ival;
                            else if (csz == 2 && !last_type_is_unsigned)
                                inner->ival = (long)(signed short)(int)inner->ival;
                        }
                        /* 浮点相关的转型创建包装节点走 default: 转换路径。
                         * 注意：不修改 inner->is_float，让 cgen_expr 的包装节点
                         * 在 default: 路径中处理 int↔float 转换。 */
                        if (inner->is_float || cast_to_double || cast_to_float) {
                            AstNode *w = new_ast(p, AST_UNARY);
                            w->expr = inner;
                            w->type_size = csz;
                            w->is_unsigned = last_type_is_unsigned;
                            if (cast_to_double) w->is_float = 8;
                            if (cast_to_float) w->is_float = 4;
                            return w;
                        }
                    } else {
                        inner->elem_size = csz;
                        inner->is_unsigned = last_type_is_unsigned;
                        if (cast_to_double) inner->elem_is_float = 8;
                        else if (cast_to_float) inner->elem_is_float = 4;
                    }
                }
                return inner;
            }
        }
        /* 不是类型转换，回溯 */
        p->lexer->pos = save_pos;
        p->lexer->line = save_line;
        p->lexer->col = save_col;
        p->tok = save_tok;
    }

    return parse_postfix(p);
}

/* 乘除: * / % */
static AstNode *parse_mul(Parser *p) {
    AstNode *left = parse_unary(p);
    while (peek(p).kind == TOK_STAR || peek(p).kind == TOK_SLASH || peek(p).kind == TOK_PERCENT) {
        Token op = consume(p);
        AstNode *right = parse_unary(p);
        AstNode *n = new_ast(p, AST_BINOP);
        n->left = left; n->right = right; n->op = op.kind;
        /* 浮点传播：* 和 / 保留浮点类型，较大的类型优先级更高 */
        if (op.kind != TOK_PERCENT &&
            ((left && left->is_float) || (right && right->is_float))) {
            if ((left && left->is_float == 8) || (right && right->is_float == 8))
                n->is_float = 8;
            else
                n->is_float = 4;
        }
        left = n;
    }
    return left;
}

/* 加减: + - */
static AstNode *parse_add(Parser *p) {
    AstNode *left = parse_mul(p);
    while (peek(p).kind == TOK_PLUS || peek(p).kind == TOK_MINUS) {
        Token op = consume(p);
        AstNode *right = parse_mul(p);
        AstNode *n = new_ast(p, AST_BINOP);
        n->left = left; n->right = right; n->op = op.kind;
        if ((left && left->is_float) || (right && right->is_float)) {
            if ((left && left->is_float == 8) || (right && right->is_float == 8))
                n->is_float = 8;
            else
                n->is_float = 4;
        }
        left = n;
    }
    return left;
}

/* 移位: << >> */
static AstNode *parse_shift(Parser *p) {
    AstNode *left = parse_add(p);
    while (peek(p).kind == TOK_LESS_LESS || peek(p).kind == TOK_GREATER_GREATER) {
        Token op = consume(p);
        AstNode *right = parse_add(p);
        AstNode *n = new_ast(p, AST_BINOP);
        n->left = left; n->right = right; n->op = op.kind;
        left = n;
    }
    return left;
}

/* 关系: < > <= >= */
static AstNode *parse_rel(Parser *p) {
    AstNode *left = parse_shift(p);
    while (peek(p).kind == TOK_LESS || peek(p).kind == TOK_GREATER ||
           peek(p).kind == TOK_LESS_EQ || peek(p).kind == TOK_GREATER_EQ) {
        Token op = consume(p);
        AstNode *right = parse_shift(p);
        AstNode *n = new_ast(p, AST_BINOP);
        n->left = left; n->right = right; n->op = op.kind;
        left = n;
    }
    return left;
}

/* 相等: == != */
static AstNode *parse_eq(Parser *p) {
    AstNode *left = parse_rel(p);
    while (peek(p).kind == TOK_EQ_EQ || peek(p).kind == TOK_NOT_EQ) {
        Token op = consume(p);
        AstNode *right = parse_rel(p);
        AstNode *n = new_ast(p, AST_BINOP);
        n->left = left; n->right = right; n->op = op.kind;
        left = n;
    }
    return left;
}

/* 按位与: & */
static AstNode *parse_bitand(Parser *p) {
    AstNode *left = parse_eq(p);
    while (peek(p).kind == TOK_AMPERSAND) {
        consume(p);
        AstNode *right = parse_eq(p);
        AstNode *n = new_ast(p, AST_BINOP);
        n->left = left; n->right = right; n->op = TOK_AMPERSAND;
        left = n;
    }
    return left;
}

/* 按位异或: ^ */
static AstNode *parse_bitxor(Parser *p) {
    AstNode *left = parse_bitand(p);
    while (peek(p).kind == TOK_CARET) {
        consume(p);
        AstNode *right = parse_bitand(p);
        AstNode *n = new_ast(p, AST_BINOP);
        n->left = left; n->right = right; n->op = TOK_CARET;
        left = n;
    }
    return left;
}

/* 按位或: | */
static AstNode *parse_bitor(Parser *p) {
    AstNode *left = parse_bitxor(p);
    while (peek(p).kind == TOK_PIPE) {
        consume(p);
        AstNode *right = parse_bitxor(p);
        AstNode *n = new_ast(p, AST_BINOP);
        n->left = left; n->right = right; n->op = TOK_PIPE;
        left = n;
    }
    return left;
}

/* 逻辑与: && */
static AstNode *parse_and(Parser *p) {
    AstNode *left = parse_bitor(p);
    while (peek(p).kind == TOK_AND_AND) {
        consume(p);
        AstNode *right = parse_bitor(p);
        AstNode *n = new_ast(p, AST_BINOP);
        n->left = left; n->right = right; n->op = TOK_AND_AND;
        left = n;
    }
    return left;
}

/* 逻辑或: || */
static AstNode *parse_or(Parser *p) {
    AstNode *left = parse_and(p);
    while (peek(p).kind == TOK_OR_OR) {
        consume(p);
        AstNode *right = parse_and(p);
        AstNode *n = new_ast(p, AST_BINOP);
        n->left = left; n->right = right; n->op = TOK_OR_OR;
        left = n;
    }
    return left;
}

/* 三元: ?: */
static AstNode *parse_ternary(Parser *p) {
    AstNode *cond = parse_or(p);
    if (peek(p).kind == TOK_QUESTION) {
        consume(p);
        AstNode *then_expr = parse_expr(p);
        expect(p, TOK_COLON);
        AstNode *else_expr = parse_ternary(p);
        AstNode *n = new_ast(p, AST_IF);
        n->cond = cond;
        n->then_stmt = then_expr;
        n->else_stmt = else_expr;
        if ((then_expr && then_expr->is_float) ||
            (else_expr && else_expr->is_float)) {
            if ((then_expr && then_expr->is_float == 8) ||
                (else_expr && else_expr->is_float == 8))
                n->is_float = 8;
            else
                n->is_float = 4;
        }
        return n;
    }
    return cond;
}

/* 赋值: = += -= *= /= %= <<= >>= &= ^= |= */
static AstNode *parse_assign(Parser *p) {
    AstNode *left = parse_ternary(p);
    if (peek(p).kind == TOK_EQ) {
        consume(p);
        AstNode *right = parse_assign(p);
        AstNode *n = new_ast(p, AST_ASSIGN);
        n->left = left;
        n->right = right;
        n->op = TOK_EQ;
        if (right && right->is_float) n->is_float = right->is_float;
        return n;
    }
    /* 复合赋值 += -= *= /= %= <<= >>= &= |= ^= → 展开为 left = left OP right */
    if (peek(p).kind == TOK_PLUS_EQ || peek(p).kind == TOK_MINUS_EQ ||
        peek(p).kind == TOK_STAR_EQ || peek(p).kind == TOK_SLASH_EQ ||
        peek(p).kind == TOK_PERCENT_EQ ||
        peek(p).kind == TOK_LESS_LESS_EQ || peek(p).kind == TOK_GREATER_GREATER_EQ ||
        peek(p).kind == TOK_AND_EQ || peek(p).kind == TOK_OR_EQ ||
        peek(p).kind == TOK_CARET_EQ) {
        Token op = consume(p);
        AstNode *right = parse_assign(p);
        /* 将 op 转换为对应的二元运算符 */
        int binop;
        switch (op.kind) {
        case TOK_PLUS_EQ:  binop = TOK_PLUS; break;
        case TOK_MINUS_EQ: binop = TOK_MINUS; break;
        case TOK_STAR_EQ:  binop = TOK_STAR; break;
        case TOK_SLASH_EQ: binop = TOK_SLASH; break;
        case TOK_PERCENT_EQ: binop = TOK_PERCENT; break;
        case TOK_LESS_LESS_EQ: binop = TOK_LESS_LESS; break;
        case TOK_GREATER_GREATER_EQ: binop = TOK_GREATER_GREATER; break;
        case TOK_AND_EQ:   binop = TOK_AMPERSAND; break;
        case TOK_OR_EQ:    binop = TOK_PIPE; break;
        case TOK_CARET_EQ: binop = TOK_CARET; break;
        default: binop = TOK_PLUS; break;
        }
        AstNode *n = new_ast(p, AST_ASSIGN);
        n->left = left;          /* left 需要是左值（AST 共享，代码生成时会被多次访问） */
        n->op = TOK_EQ;
        /* 构建 left = left OP right */
        AstNode *bin = new_ast(p, AST_BINOP);
        bin->op = binop;
        /* 复制 left 作为二元运算的左操作数 */
        if (left && left->kind == AST_VAR) {
            AstNode *cpy = new_ast(p, AST_VAR);
            cpy->name = left->name;
            cpy->type_size = left->type_size;
            cpy->is_float = left->is_float;
            bin->left = cpy;
        } else {
            bin->left = left;  /* 对非 AST_VAR 的左值，共享原节点 */
        }
        bin->right = right;
        if (right && right->is_float) {
            if (right->is_float == 8 || (left && left->is_float == 8))
                bin->is_float = 8;
            else
                bin->is_float = 4;
        }
        n->right = bin;
        if (bin->is_float) n->is_float = bin->is_float;
        return n;
    }
    return left;
}

/* 顶层表达式（不含逗号运算符） */
static AstNode *parse_expr(Parser *p) {
    return parse_assign(p);
}

/* 逗号表达式（最低优先级）：expr1, expr2 */
static AstNode *parse_expr_comma(Parser *p) {
    AstNode *n = parse_assign(p);
    while (peek(p).kind == TOK_COMMA) {
        consume(p);
        AstNode *right = parse_assign(p);
        AstNode *cn = new_ast(p, AST_BINOP);
        cn->op = TOK_COMMA;
        cn->left = n;
        cn->right = right;
        /* 逗号表达式的结果类型 = 右操作数 */
        if (right && right->is_float) cn->is_float = right->is_float;
        n = cn;
    }
    return n;
}

/* ─── 解析 struct 体（返回成员列表和总大小） ─── */

static int parse_struct_body(Parser *p, Member *members, int *out_count, int is_union) {
    expect(p, TOK_LBRACE);
    int count = 0;
    int offset = 0;
    int max_offset = 0;  /* union: total_size = max member size */
    int max_align = 1;
    /* 保存当前正在解析的 struct 的标签。parse_type_specifier 每调用都会清
     * last_struct_tag（line ~1430），导致自引用成员丢失 member_struct_tag。
     * 这是一个外层不变值（由 parse_struct_type 在 line 1254 设置），不会
     * 被内部的 parse_type_specifier 影响。 */
    const char *outer_struct_tag = last_struct_tag;

    while (peek(p).kind != TOK_RBRACE && peek(p).kind != TOK_EOF) {
        /* 跳过限定符 */
        while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
               peek(p).kind == TOK_RESTRICT) consume(p);
        int sz = parse_type_specifier(p);
        int base_sz = sz;
        int member_is_unsigned = last_type_is_unsigned;
        /* member_tag_for_chain：若 parse_type_specifier 正确设置了新的
         * last_struct_tag（如非自引用的已注册 struct），使用新值；
         * 否则回退到 outer_struct_tag（包含自引用成员等情况）。 */
        const char *member_tag_for_chain = last_struct_tag ? last_struct_tag : outer_struct_tag;
        if (sz < 0) { error_at(p, "invalid struct member type"); break; }

        /* 指针 */
        int ptr_count = 0;
        while (peek(p).kind == TOK_STAR) { consume(p); ptr_count++; sz = 8; }

        /* 函数指针 (*name)(params) */
        if (peek(p).kind == TOK_LPAREN) {
            consume(p);
            if (peek(p).kind == TOK_STAR) {
                consume(p); sz = 8; ptr_count++;
            } else {
                /* 不是 (*name)，无法处理 */
            }
        }

        Token id = peek(p);
        if (id.kind == TOK_IDENT) {
            consume(p);
            /* 处理数组后缀 [N]（在注册成员前计算完整大小） */
            int member_sz = sz;
            int is_array = 0;
            if (peek(p).kind == TOK_LBRACKET) {
                is_array = 1;
                consume(p);
                if (peek(p).kind == TOK_NUMBER && peek(p).ival > 0) {
                    member_sz *= peek(p).ival;
                    consume(p);
                }
                int d = 1;
                while (d > 0 && peek(p).kind != TOK_EOF) {
                    if (peek(p).kind == TOK_LBRACKET) d++;
                    if (peek(p).kind == TOK_RBRACKET) d--;
                    if (d) consume(p);
                }
                if (peek(p).kind == TOK_RBRACKET) consume(p);
            }
            if (count >= MAX_MEMBERS) {
                error_at(p, "too many struct/union members");
                break; }
            if (1) {
                /* union: 所有成员从偏移 0 开始 */
                int member_offset = is_union ? 0 : offset;
                /* 按类型自然对齐（指针/long long/double=8 字节对齐。数组按其元素类型对齐） */
                int align_sz = is_array ? sz : member_sz;
                int member_align = (align_sz >= 8) ? 8 : (align_sz >= 4) ? 4 : (align_sz >= 2) ? 2 : 1;
                member_offset = (member_offset + member_align - 1) & ~(member_align - 1);
                if (member_align > max_align) max_align = member_align;
                members[count].name = arena_strdup(p->arena, id.start, id.len);
                members[count].offset = member_offset;
                members[count].size = member_sz;
                members[count].is_unsigned = member_is_unsigned;
                members[count].is_float = last_type_is_float;
                /* elem_size: 指针→指向的类型大小，数组→元素大小 */
                if (is_array)
                    members[count].elem_size = sz;  /* 数组元素大小（乘 [N] 之前的 sz） */
                else if (ptr_count == 1)
                    members[count].elem_size = base_sz;
                else if (ptr_count > 1)
                    members[count].elem_size = 8;   /* 多级指针 → 指针大小 */
                else
                    members[count].elem_size = 0;
                members[count].memb_is_array = is_array;
                /* 记录 struct 标签，无论成员是否为指针（这样 p->member->sub
                 * 链式访问的 struct_type 传播能正确工作）。非 struct 类型时
                 * member_tag_for_chain 为 NULL，不影响。 */
                members[count].member_struct_tag = member_tag_for_chain;
                count++;
                if (is_union) {
                    if (member_sz > max_offset) max_offset = member_sz;
                    /* 指针解引用后，使用指针元素大小计算联合体成员偏移 */
                } else {
                    offset = member_offset + member_sz;
                }
            }
            sz = member_sz;  /* 逗号后成员使用相同的完整大小 */
        }
        /* 关闭函数指针的 ) 和参数列表 */
        if (sz == 8 && peek(p).kind == TOK_RPAREN) {
            consume(p);
            if (peek(p).kind == TOK_LPAREN) {
                int depth = 1; consume(p);
                while (depth > 0 && peek(p).kind != TOK_EOF) {
                    if (peek(p).kind == TOK_LPAREN) depth++;
                    if (peek(p).kind == TOK_RPAREN) depth--;
                    if (depth) consume(p);
                }
                if (peek(p).kind == TOK_RPAREN) consume(p);
            }
        }

        /* 跳过位域 :N */
        if (peek(p).kind == TOK_COLON) { consume(p);
            while (peek(p).kind != TOK_SEMI && peek(p).kind != TOK_EOF) consume(p); }

        /* 逗号分隔的成员：int a, b, c; */
        while (peek(p).kind == TOK_COMMA) {
            consume(p);
            int comma_ptr_count = 0;
            while (peek(p).kind == TOK_STAR) { consume(p); comma_ptr_count++; }
            Token cid = peek(p);
            if (cid.kind == TOK_IDENT) {
                consume(p);
                /* 处理逗号后成员的数组后缀 [N] */
                int member_sz = sz;
                int comma_is_array = 0;
                if (peek(p).kind == TOK_LBRACKET) {
                    comma_is_array = 1;
                    consume(p);
                    if (peek(p).kind == TOK_NUMBER && peek(p).ival > 0) {
                        member_sz *= peek(p).ival;
                        consume(p);
                    }
                    int d2 = 1;
                    while (d2 > 0 && peek(p).kind != TOK_EOF) {
                        if (peek(p).kind == TOK_LBRACKET) d2++;
                        if (peek(p).kind == TOK_RBRACKET) d2--;
                        if (d2) consume(p);
                    }
                    if (peek(p).kind == TOK_RBRACKET) consume(p);
                }
                if (count >= MAX_MEMBERS) {
                    error_at(p, "too many struct/union members");
                    break; }
                if (1) {
                    int comma_offset = is_union ? 0 : offset;
                    int align_sz = comma_is_array ? base_sz : member_sz;
                    int member_align = (align_sz >= 8) ? 8 : (align_sz >= 4) ? 4 : (align_sz >= 2) ? 2 : 1;
                    comma_offset = (comma_offset + member_align - 1) & ~(member_align - 1);
                    if (member_align > max_align) max_align = member_align;
                    members[count].name = arena_strdup(p->arena, cid.start, cid.len);
                    members[count].offset = comma_offset;
                    members[count].size = member_sz;
                    members[count].is_unsigned = member_is_unsigned;
                    members[count].is_float = last_type_is_float;
                    if (comma_is_array)
                        members[count].elem_size = sz;
                    else if (comma_ptr_count == 1)
                        members[count].elem_size = base_sz;
                    else if (comma_ptr_count > 1)
                        members[count].elem_size = 8;
                    else
                        members[count].elem_size = 0;
                    members[count].memb_is_array = comma_is_array;
                    members[count].member_struct_tag = member_tag_for_chain;
                    count++;
                    if (is_union) {
                        if (member_sz > max_offset) max_offset = member_sz;
                    } else {
                        offset = comma_offset + member_sz;
                    }
                }
                sz = member_sz;
            }
        }

        expect(p, TOK_SEMI);
    }
    expect(p, TOK_RBRACE);

    *out_count = count;
    if (is_union) {
        /* union 大小 = 最大成员大小（向上对齐到 max_align） */
        if (max_align > 1)
            max_offset = (max_offset + max_align - 1) & ~(max_align - 1);
        return max_offset > 0 ? max_offset : 1;
    }
    /* struct: 对齐到最大成员对齐（x86_64 ABI：struct 尾部填充对齐到 max_align） */
    if (max_align > 1)
        offset = (offset + max_align - 1) & ~(max_align - 1);
    return offset > 0 ? offset : 1;
}

/* 解析 struct 类型：struct [tag] { ... }  或 struct tag
 * 返回 0 表示失败或 forward-declaration */
static int parse_struct_type(Parser *p, StructType *out, int is_union) {
    out->tag = NULL;
    out->member_count = 0;
    out->total_size = 0;

    /* 可选的标签 */
    const char *tag = NULL;
    if (peek(p).kind == TOK_IDENT) {
        Token t = consume(p);
        tag = arena_strdup(p->arena, t.start, t.len);
    }

    if (peek(p).kind == TOK_LBRACE) {
        /* struct tag { ... } — 在 body 解析前设置 last_struct_tag，
         * 使得 self-referencing 成员（如 struct Node *next）能获得
         * 正确的 member_struct_tag，用于链式访问的 struct_type 传播。 */
        last_struct_tag = tag;
        out->total_size = parse_struct_body(p, out->members, &out->member_count, is_union);
        while (peek(p).kind == TOK__ATTRIBUTE__) {
            consume(p); expect(p, TOK_LPAREN); expect(p, TOK_LPAREN);
            int d = 2;
            while (d > 0 && peek(p).kind != TOK_EOF) {
                if (peek(p).kind == TOK_LPAREN) d++;
                if (peek(p).kind == TOK_RPAREN) d--;
                consume(p);
            }
        }
        /* 匿名 struct 使用唯一合成 tag 避免多个匿名 struct 的 tag 冲突 */
        if (!tag) {
            static int anon_id;
            char anon_buf[32];
            int anon_len = 0;
            anon_buf[anon_len++] = '@';
            { int v = anon_id++; int r; do { r = v % 36; anon_buf[anon_len++] = (r < 10) ? ('0'+r) : ('a'+r-10); v /= 36; } while (v); }
            char *anon_tag = arena_alloc(p->arena, anon_len + 1);
            int ai; for (ai = 0; ai < anon_len; ai++) anon_tag[ai] = anon_buf[ai];
            anon_tag[anon_len] = '\0';
            out->tag = anon_tag;
        } else {
            out->tag = tag;
        }
        /* 保存到 last_struct_* 供变量声明和成员访问使用 */
        last_struct_tag = out->tag;
        last_struct_member_count = out->member_count;
        int mi;
        for (mi = 0; mi < out->member_count && mi < MAX_MEMBERS; mi++)
            last_struct_members[mi] = out->members[mi];

        /* 始终注册到 tag_table */
        add_struct_tag(out->tag, out);
    } else if (tag) {
        /* struct tag (前置声明或引用) */
        StructType *existing = find_struct_tag(tag);
        if (existing) {
            *out = *existing;
            /* 设置 last_struct_* 供后续变量声明和 sizeof 使用 */
            last_struct_tag = tag;
            last_struct_member_count = existing->member_count;
            int mi;
            for (mi = 0; mi < existing->member_count && mi < MAX_MEMBERS; mi++)
                last_struct_members[mi] = existing->members[mi];
        } else {
            /* 不完全类型，暂不支持 */
        }
    }
    return out->total_size;
}

/* ─── 类型说明符 ─── */

/* evaluate compile-time constant expression */
static long long eval_const_expr(AstNode *n) {
    if (!n) return 0;
    switch (n->kind) {
    case AST_CONSTANT: return n->ival;
    case AST_UNARY:
        switch (n->op) {
        case TOK_MINUS: return -eval_const_expr(n->expr);
        case TOK_TILDE: return ~eval_const_expr(n->expr);
        case TOK_EXCLAM: return !eval_const_expr(n->expr);
        default: return eval_const_expr(n->expr);
        }
    case AST_BINOP:
        switch (n->op) {
        case TOK_PLUS:  return eval_const_expr(n->left) + eval_const_expr(n->right);
        case TOK_MINUS: return eval_const_expr(n->left) - eval_const_expr(n->right);
        case TOK_STAR:  return eval_const_expr(n->left) * eval_const_expr(n->right);
        case TOK_SLASH: return eval_const_expr(n->left) / eval_const_expr(n->right);
        case TOK_PERCENT: return eval_const_expr(n->left) % eval_const_expr(n->right);
        case TOK_AMPERSAND: return eval_const_expr(n->left) & eval_const_expr(n->right);
        case TOK_PIPE:  return eval_const_expr(n->left) | eval_const_expr(n->right);
        case TOK_CARET: return eval_const_expr(n->left) ^ eval_const_expr(n->right);
        case TOK_LESS_LESS: return eval_const_expr(n->left) << eval_const_expr(n->right);
        case TOK_GREATER_GREATER: return eval_const_expr(n->left) >> eval_const_expr(n->right);
        default: return 0;
        }
    default: return 0;
    }
}

/* ─── 全局变量初始化器解析辅助函数 ─── */

/* 解析一个标量初始器值（字符串字面量或常量表达式）存入 items[idx] */
/* 返回增加的项目数（总是 1）。idx >= max 时不存储但仍消耗输入 */
static int parse_one_init(Parser *p, InitItem *items, int idx, int max) {
    if (peek(p).kind == TOK_STRING) {
        Token t = consume(p);
        if (idx >= max) return 1;
        int sl = t.len - 2;
        char *dst = arena_alloc(p->arena, sl + 1);
        int di = 0;
        const char *src = t.start + 1;
        int i = 0;
        while (i < sl) {
            char c = src[i++];
            if (c == '\\' && i < sl) {
                char e = src[i++];
                switch (e) {
                case 'n': dst[di++] = '\n'; break;
                case 't': dst[di++] = '\t'; break;
                case 'r': dst[di++] = '\r'; break;
                case '0': dst[di++] = '\0'; break;
                case '\\': dst[di++] = '\\'; break;
                case '"': dst[di++] = '"'; break;
                default:
                    if (e >= '0' && e <= '7') {
                        int ov = e - '0'; int od = 1;
                        while (od < 3 && i < sl) {
                            char oc = src[i];
                            if (oc >= '0' && oc <= '7') { ov = ov*8+(oc-'0'); i++; od++; }
                            else break;
                        }
                        dst[di++] = (char)ov;
                    } else { dst[di++] = e; }
                }
            } else { dst[di++] = c; }
        }
        dst[di] = '\0';
        items[idx].type = INIT_TYPE_STR;
        items[idx].str = dst;
        items[idx].ival = 0;
        return 1;
    }
    if (idx >= max) { (void)parse_expr(p); return 1; }
    AstNode *expr = parse_expr(p);
    long val;
    if (expr && expr->kind == AST_CONSTANT && expr->is_float) {
        /* 浮点常量：使用 parse_float_literal 提取 IEEE 754 位模式，
         * 避免 eval_const_expr 将浮点当做整数处理。
         * 用字节数组组合 hi/lo 以避开 tcc 自举时移位和联合体的 bug。 */
        unsigned int lo, hi;
        parse_float_literal(expr->name, (int)expr->ival, &lo, &hi);
        if (expr->is_float == 4) {
            val = (long)double_bits_to_float(hi, lo);
        } else {
            unsigned char _b[8];
            _b[0] = (unsigned char)( lo        & 0xFF);
            _b[1] = (unsigned char)((lo >>  8) & 0xFF);
            _b[2] = (unsigned char)((lo >> 16) & 0xFF);
            _b[3] = (unsigned char)((lo >> 24) & 0xFF);
            _b[4] = (unsigned char)( hi        & 0xFF);
            _b[5] = (unsigned char)((hi >>  8) & 0xFF);
            _b[6] = (unsigned char)((hi >> 16) & 0xFF);
            _b[7] = (unsigned char)((hi >> 24) & 0xFF);
            val = *(long *)_b;
        }
    } else if (expr && expr->kind == AST_UNARY && expr->op == TOK_MINUS &&
               expr->expr && expr->expr->kind == AST_CONSTANT && expr->expr->is_float) {
        /* -浮点常量：对内部常量取负（翻转符号位） */
        unsigned int lo, hi;
        parse_float_literal(expr->expr->name, (int)expr->expr->ival, &lo, &hi);
        if (expr->expr->is_float == 4) {
            unsigned int f = double_bits_to_float(hi, lo) ^ 0x80000000U;
            val = (long)f;
        } else {
            unsigned char _b[8];
            _b[0] = (unsigned char)( lo        & 0xFF);
            _b[1] = (unsigned char)((lo >>  8) & 0xFF);
            _b[2] = (unsigned char)((lo >> 16) & 0xFF);
            _b[3] = (unsigned char)((lo >> 24) & 0xFF);
            _b[4] = (unsigned char)((hi ^ 0x80000000U)        & 0xFF);
            _b[5] = (unsigned char)(((hi ^ 0x80000000U) >>  8) & 0xFF);
            _b[6] = (unsigned char)(((hi ^ 0x80000000U) >> 16) & 0xFF);
            _b[7] = (unsigned char)(((hi ^ 0x80000000U) >> 24) & 0xFF);
            val = *(long *)_b;
        }
    } else {
        val = eval_const_expr(expr);
    }
    items[idx].type = INIT_TYPE_INT;
    items[idx].ival = val;
    items[idx].str = NULL;
    return 1;
}

/* 解析 { } 内的逗号分隔初始化列表，递归处理嵌套 { } */
/* 返回解析的标量总数。items 可为 NULL 则只计数不存储 */
/* 若 elem_count 非 NULL，返回时 *elem_count 设为顶层元素数（用于 items_per_elem） */
static int parse_init_list(Parser *p, InitItem *items, int max, int *elem_count) {
    int count = 0;
    int elems = 0;
    consume(p); /* skip { */
    while (peek(p).kind != TOK_RBRACE && peek(p).kind != TOK_EOF) {
        elems++;
        if (peek(p).kind == TOK_LBRACE) {
            /* 嵌套 { } — 结构体/嵌套数组元素初始化器 */
            int sub = parse_init_list(p, items ? items + count : NULL,
                                       max > count ? max - count : 0, NULL);
            count += sub;
        } else {
            count += parse_one_init(p, items, count, max);
        }
        if (peek(p).kind == TOK_COMMA) consume(p);
    }
    if (peek(p).kind == TOK_RBRACE) consume(p);
    if (elem_count) *elem_count = elems;
    return count;
}

int parse_type_specifier(Parser *p) {
    Token t = peek(p);
    /* 每次调用时清除 struct tag（非 struct 类型不会设置它） */
    last_struct_tag = NULL;
    last_type_is_float = 0;
    /* 检查 typedef 名（先提取词素，t.start 不是 null 终止的） */
    if (t.kind == TOK_IDENT) {
        char tname[128];
        int nl = t.len < 127 ? t.len : 127;
        int ci; for (ci = 0; ci < nl; ci++) tname[ci] = t.start[ci]; tname[nl] = '\0';
        int ti;
        for (ti = 0; ti < typedef_count; ti++) {
            if (strcmp(typedef_table[ti].name, tname) == 0) {
                consume(p);
                last_type_is_unsigned = typedef_table[ti].is_unsigned;
                /* 记录 struct 标签（匿名 struct typedef 的 tag 就是 typedef 名） */
                if (typedef_table[ti].member_count > 0)
                    last_struct_tag = typedef_table[ti].name;
                return typedef_table[ti].size;
            }
        }
    }
    switch (t.kind) {
    case TOK_INT:      last_type_is_unsigned = 0; consume(p); return 4;
    case TOK_CHAR:     last_type_is_unsigned = 0; consume(p); return 1;
    case TOK_SHORT:    last_type_is_unsigned = 0; consume(p); return 2;
    case TOK_FLOAT:    last_type_is_unsigned = 0; last_type_is_float = 4; consume(p); return 4;
    case TOK_DOUBLE:   last_type_is_unsigned = 0; last_type_is_float = 8; consume(p); return 8;
    case TOK__BUILTIN_VA_LIST: last_type_is_unsigned = 0; consume(p); return 24;
    case TOK_LONG:
        last_type_is_unsigned = 0; consume(p);
        if (peek(p).kind == TOK_LONG) { consume(p); if (peek(p).kind == TOK_INT) { consume(p); } return 8; }
        if (peek(p).kind == TOK_INT) { consume(p); } return 8;
    case TOK_STRUCT:
    case TOK_UNION: {
        last_type_is_unsigned = 0; consume(p);
        int _is_union = (t.kind == TOK_UNION) ? 1 : 0;
        /* 提前获取 struct 标签名（用于 total_size 回退，绕过 stage-2
         * 中 *out = *existing 大结构体拷贝导致 total_size 丢失的 bug） */
        const char *_tag_fb = NULL;
        {   Token _nt = peek(p);
            if (_nt.kind == TOK_IDENT) {
                int _nl = _nt.len < 127 ? _nt.len : 127;
                char *_tn = arena_alloc(p->arena, _nl + 1);
                int _ci; for (_ci = 0; _ci < _nl; _ci++) _tn[_ci] = _nt.start[_ci]; _tn[_nl] = '\0';
                _tag_fb = _tn;
            }
        }
        StructType st;
        int sz = parse_struct_type(p, &st, _is_union);
        if (st.member_count > 0)
            last_struct_tag = st.tag ? st.tag : "";
        /* 回退：struct 拷贝丢失了 total_size，直接从 tag_table 取 */
        if (sz <= 0 && _tag_fb) {
            StructType *_ex = find_struct_tag(_tag_fb);
            if (_ex && _ex->total_size > 0) sz = _ex->total_size;
        }
        return sz > 0 ? sz : 4;
    }
    case TOK_ENUM: {
        last_type_is_unsigned = 0; consume(p);
        if (peek(p).kind == TOK_IDENT) consume(p);
        if (peek(p).kind == TOK_LBRACE) {
            consume(p);
            int enum_val = 0;
            while (peek(p).kind != TOK_RBRACE && peek(p).kind != TOK_EOF) {
                if (peek(p).kind == TOK_IDENT) {
                    Token en = consume(p);
                    const char *ename = arena_strdup(p->arena, en.start, en.len);
                    if (match(p, TOK_EQ)) {
                        AstNode *init = parse_expr(p);
                        if (init) enum_val = eval_const_expr(init);
                    }
                    register_enum_val(ename, enum_val);
                    enum_val++;
                }
                if (peek(p).kind == TOK_COMMA) { consume(p); continue; }
                break;
            }
            if (peek(p).kind == TOK_RBRACE) consume(p);
        }
        return 4;
    }
    case TOK_VOID:     last_type_is_unsigned = 0; consume(p); return 0;
    case TOK_UNSIGNED:
        consume(p);
        last_type_is_unsigned = 1;
        if (peek(p).kind == TOK_CHAR) { consume(p); return 1; }
        if (peek(p).kind == TOK_SHORT) { consume(p); if (peek(p).kind == TOK_INT) { consume(p); } return 2; }
        if (peek(p).kind == TOK_LONG) {
            consume(p);
            if (peek(p).kind == TOK_LONG) { consume(p); if (peek(p).kind == TOK_INT) { consume(p); } return 8; }
            if (peek(p).kind == TOK_INT) { consume(p); } return 8;
        }
        if (peek(p).kind == TOK_INT) { consume(p); return 4; }
        return 4;
    case TOK_SIGNED:
        consume(p);
        last_type_is_unsigned = 0;
        if (peek(p).kind == TOK_CHAR) { consume(p); return 1; }
        if (peek(p).kind == TOK_SHORT) { consume(p); if (peek(p).kind == TOK_INT) { consume(p); } return 2; }
        if (peek(p).kind == TOK_LONG) {
            consume(p);
            if (peek(p).kind == TOK_LONG) { consume(p); if (peek(p).kind == TOK_INT) { consume(p); } return 8; }
            if (peek(p).kind == TOK_INT) { consume(p); } return 8;
        }
        if (peek(p).kind == TOK_INT) { consume(p); return 4; }
        return 4;
    default:
        return -1;
    }
}

/* ─── 声明符 ─── */

static const char *parse_declarator(Parser *p, int *ptr_level) {
    int ptrs = 0;
    while (match(p, TOK_STAR)) ptrs++;
    if (ptr_level) *ptr_level = ptrs;
    /* 跳过星号后的限定符（如 *const name） */
    while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
           peek(p).kind == TOK_RESTRICT) consume(p);
    Token t = peek(p);
    if (t.kind == TOK_IDENT) {
        consume(p);
        return arena_strdup(p->arena, t.start, t.len);
    }
    /* 处理 (*name)(params) 或 (*name[...])(params) — 函数指针/函数指针数组 */
    if (t.kind == TOK_LPAREN) {
        consume(p);
        if (peek(p).kind == TOK_STAR) {
            consume(p);
            if (ptr_level) (*ptr_level)++;  /* (*name)(params) 是函数指针 → extra ptr level */
            while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
                   peek(p).kind == TOK_RESTRICT) consume(p);
            Token nt = peek(p);
            const char *name = "";
            if (nt.kind == TOK_IDENT) { consume(p); name = arena_strdup(p->arena, nt.start, nt.len); }
            /* 跳过数组后缀 [...]（函数指针数组）*/
            pdecl_fptr_array_dim = 0;
            while (peek(p).kind == TOK_LBRACKET) {
                consume(p);
                if (pdecl_fptr_array_dim == 0 && peek(p).kind == TOK_NUMBER)
                    pdecl_fptr_array_dim = peek(p).ival;
                int d = 1;
                while (d > 0 && peek(p).kind != TOK_EOF) {
                    if (peek(p).kind == TOK_LBRACKET) d++;
                    if (peek(p).kind == TOK_RBRACKET) d--;
                    if (d) consume(p);
                }
                if (peek(p).kind == TOK_RBRACKET) consume(p);
            }
            expect(p, TOK_RPAREN);
            if (peek(p).kind == TOK_LPAREN) {
                int d = 1; consume(p);
                while (d > 0 && peek(p).kind != TOK_EOF) {
                    if (peek(p).kind == TOK_LPAREN) d++;
                    if (peek(p).kind == TOK_RPAREN) d--;
                    if (d) consume(p);
                }
                if (peek(p).kind == TOK_RPAREN) { consume(p); }
            }
            return name;
        }
        /* 不是 (*name) — 回退 */
        error_at(p, "expected identifier");
        return "";
    }
    error_at(p, "expected identifier");
    return "";
}

/* 解析变量声明（支持 register __asm__ 扩展） */
static void skip_register_asm(Parser *p) {
    /* register int x __asm__("reg") = val; */
    if (peek(p).kind == TOK_REGISTER) {
        consume(p);
        int ts = parse_type_specifier(p);
        (void)ts;
        if (peek(p).kind == TOK_IDENT) consume(p);
        if (peek(p).kind == TOK__ASM__) {
            consume(p);
            expect(p, TOK_LPAREN);
            if (peek(p).kind == TOK_STRING) consume(p);
            expect(p, TOK_RPAREN);
        }
        if (match(p, TOK_EQ)) parse_expr(p);
        expect(p, TOK_SEMI);
    }
}

/* ─── 语句 ─── */

static AstNode *parse_return_statement(Parser *p) {
    if (p->func_depth == 0) {
        error_at(p, "'return' outside function");
        consume(p); /* skip return */
        error_recover(p);
        return NULL;
    }
    consume(p);
    AstNode *n = new_ast(p, AST_RETURN);
    if (peek(p).kind != TOK_SEMI)
        n->expr = parse_expr_comma(p);
    expect(p, TOK_SEMI);
    return n;
}

static AstNode *parse_if_statement(Parser *p) {
    consume(p);  /* if */
    if (p->tok.kind != TOK_LPAREN) {
        error_at(p, "expected '(' after 'if'");
        error_recover(p);
        return NULL;
    }
    consume(p);
    AstNode *n = new_ast(p, AST_IF);
    n->cond = parse_expr(p);
    expect(p, TOK_RPAREN);
    n->then_stmt = parse_statement(p);
    if (peek(p).kind == TOK_ELSE) {
        consume(p);
        n->else_stmt = parse_statement(p);
    }
    return n;
}

static AstNode *parse_while_statement(Parser *p) {
    consume(p);  /* while */
    if (p->tok.kind != TOK_LPAREN) {
        error_at(p, "expected '(' after 'while'");
        error_recover(p);
        return NULL;
    }
    consume(p);
    AstNode *n = new_ast(p, AST_WHILE);
    n->loop_cond = parse_expr(p);
    expect(p, TOK_RPAREN);
    p->loop_depth++;
    n->loop_body = parse_statement(p);
    p->loop_depth--;
    return n;
}

static AstNode *parse_for_statement(Parser *p) {
    consume(p);  /* for */
    if (p->tok.kind != TOK_LPAREN) {
        error_at(p, "expected '(' after 'for'");
        error_recover(p);
        return NULL;
    }
    consume(p);
    AstNode *n = new_ast(p, AST_FOR);

    /* init */
    if (peek(p).kind != TOK_SEMI) {
        /* 跳过限定符：for (const char *p = ...) */
        while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
               peek(p).kind == TOK_RESTRICT) consume(p);
        int ts = parse_type_specifier(p);
        if (ts >= 0) {
            int loop_ptrs = 0;
            n->loop_init = new_ast(p, AST_VAR_DECL);
            n->loop_init->name = parse_declarator(p, &loop_ptrs);
            n->loop_init->ival = loop_ptrs > 0 ? 8 : (ts > 0 ? ts : 4);
            n->loop_init->type_size = n->loop_init->ival;
            n->loop_init->is_float = (loop_ptrs == 0) ? last_type_is_float : 0;
            if (loop_ptrs == 0) {
                n->loop_init->is_unsigned = last_type_is_unsigned;
                n->loop_init->elem_is_unsigned = 0;
            } else {
                n->loop_init->is_unsigned = 0;
                n->loop_init->elem_is_unsigned = last_type_is_unsigned;
                n->loop_init->elem_is_float = last_type_is_float;
            }
            if (n->loop_init->name && *n->loop_init->name) {
                pvar_add_ex(n->loop_init->name, last_struct_tag, n->loop_init->is_float, n->loop_init->is_unsigned, 0);
                if (last_struct_tag && *last_struct_tag)
                    pvar_set_struct_type(n->loop_init->name, resolve_struct_type(last_struct_tag));
                if (loop_ptrs > 0 && last_type_is_float)
                    pvar_set_elem_float(n->loop_init->name, last_type_is_float);
            }
            if (match(p, TOK_EQ))
                n->loop_init->expr = parse_expr_comma(p);
        } else {
            n->loop_init = new_ast(p, AST_EXPR_STMT);
            n->loop_init->expr = parse_expr_comma(p);
        }
    }
    expect(p, TOK_SEMI);

    /* condition */
    if (peek(p).kind != TOK_SEMI)
        n->loop_cond = parse_expr(p);
    expect(p, TOK_SEMI);

    /* step */
    if (peek(p).kind != TOK_RPAREN) {
        n->loop_step = parse_expr_comma(p);
    }
    expect(p, TOK_RPAREN);

    p->loop_depth++;
    n->loop_body = parse_statement(p);
    p->loop_depth--;
    return n;
}

static AstNode *parse_do_while(Parser *p) {
    consume(p);  /* do */
    AstNode *n = new_ast(p, AST_DO_WHILE);
    p->loop_depth++;
    n->loop_body = parse_statement(p);
    p->loop_depth--;
    expect(p, TOK_WHILE);
    expect(p, TOK_LPAREN);
    n->loop_cond = parse_expr(p);
    expect(p, TOK_RPAREN);
    expect(p, TOK_SEMI);
    return n;
}

/* ─── switch/case/default ─── */

static AstNode *parse_switch_statement(Parser *p) {
    consume(p);  /* switch */
    if (p->tok.kind != TOK_LPAREN) {
        error_at(p, "expected '(' after 'switch'");
        error_recover(p);
        return NULL;
    }
    consume(p);
    AstNode *n = new_ast(p, AST_SWITCH);
    n->cond = parse_expr(p);
    expect(p, TOK_RPAREN);
    /* switch 体使用复合语句解析器，由其处理 case/default 标签 */
    n->stmts = NULL;
    p->switch_depth++;
    if (peek(p).kind == TOK_LBRACE) {
        AstNode *block = parse_compound_statement(p);
        if (block && block->stmts)
            n->stmts = block->stmts;
    }
    p->switch_depth--;
    return n;
}

static AstNode *parse_break(Parser *p) {
    if (p->loop_depth == 0 && p->switch_depth == 0) {
        error_at(p, "'break' outside loop or switch");
        consume(p);
        error_recover(p);
        return NULL;
    }
    consume(p);
    expect(p, TOK_SEMI);
    return new_ast(p, AST_BREAK);
}

static AstNode *parse_continue(Parser *p) {
    if (p->loop_depth == 0) {
        error_at(p, "'continue' outside loop");
        consume(p);
        error_recover(p);
        return NULL;
    }
    consume(p);
    expect(p, TOK_SEMI);
    return new_ast(p, AST_CONTINUE);
}

/* ─── 复合语句 ─── */

AstNode *parse_compound_statement(Parser *p) {
    if (!expect(p, TOK_LBRACE)) {
        error_recover(p);
    }
    p->block_depth++;

    AstNode *head = NULL;
    AstNode **tail = &head;

    while (peek(p).kind != TOK_RBRACE && peek(p).kind != TOK_EOF) {
        const char *prev_pos = p->lexer->pos;  /* 防死循环 */
        /* 检测标签：IDENT : */
        if (peek(p).kind == TOK_IDENT) {
            const char *lsp = p->lexer->pos;
            int lsl = p->lexer->line, lsc = p->lexer->col;
            Token lst = p->tok;
            Token lid = consume(p);
            if (peek(p).kind == TOK_COLON) {
                consume(p); /* 冒号 */
                AstNode *label_node = new_ast(p, AST_LABEL);
                label_node->name = arena_strdup(p->arena, lid.start, lid.len);
                *tail = label_node;
                tail = &label_node->next;
                continue;
            }
            /* 恢复，不是标签 */
            p->lexer->pos = lsp;
            p->lexer->line = lsl;
            p->lexer->col = lsc;
            p->tok = lst;
        }
        /* 处理 register __asm__ 变量声明 */
        if (peek(p).kind == TOK_REGISTER) {
            skip_register_asm(p);
            continue;
        }
        /* 跳过限定符（const/volatile/restrict）和存储类（static/extern/inline/typedef） */
        {
            const char *qpos = p->lexer->pos;
            int qline = p->lexer->line;
            int qcol = p->lexer->col;
            Token qtok = p->tok;
            int nq = 0;
            int q_static = 0;
            int q_typedef = 0;
            while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
                   peek(p).kind == TOK_RESTRICT || peek(p).kind == TOK_STATIC ||
                   peek(p).kind == TOK_EXTERN || peek(p).kind == TOK_INLINE ||
                   peek(p).kind == TOK_TYPEDEF) {
                if (peek(p).kind == TOK_STATIC) q_static = 1;
                if (peek(p).kind == TOK_TYPEDEF) q_typedef = 1;
                consume(p); nq++;
            }
            /* 处理函数体中的 typedef */
            if (q_typedef) {
                while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
                       peek(p).kind == TOK_RESTRICT || peek(p).kind == TOK__ATTRIBUTE__)
                    consume(p);
                int tsz = parse_type_specifier(p);
                int tptr_level = 0;
                const char *tname = parse_declarator(p, &tptr_level);
                if (tname && *tname) {
                    if (typedef_count >= MAX_TYPEDEFS) {
                        error_at(p, "too many typedefs");
                        break; }
                    int dup = 0, ti;
                    for (ti = 0; ti < typedef_count; ti++)
                        if (strcmp(typedef_table[ti].name, tname) == 0) { dup = 1; break; }
                    if (!dup) {
                        TypedefEntry *te = &typedef_table[typedef_count];
                        te->name = tname;
                        te->size = tptr_level > 0 ? 8 : tsz;
                        te->type_kind = tptr_level > 0 ? 2 : (last_struct_member_count > 0 ? 1 : 0);
                        te->ptr_level = tptr_level;
                        te->points_to = tptr_level > 0 ? tsz : 0;
                        te->is_unsigned = (tptr_level == 0) ? last_type_is_unsigned : 0;
                        if (last_struct_member_count > 0) {
                            te->member_count = last_struct_member_count;
                            int mi;
                            for (mi = 0; mi < last_struct_member_count && mi < MAX_MEMBERS; mi++)
                                te->members[mi] = last_struct_members[mi];
                            /* reg typedef'd struct to tag_table for member lookup.
                             * Only add if tag doesn't already exist — prevents dup entries
                             * that trigger the ambiguity detector in ->/. member lookup. */
                            if (!find_struct_tag(tname)) {
                                if (tag_count >= MAX_TAGS) {
                                    __write(2, "toyc: too many struct/union tags\n", 32);
                                    __exit(1); }
                                StructType *st = &tag_table[tag_count++];
                                st->tag = tname;
                                st->total_size = tsz;
                                st->member_count = last_struct_member_count;
                                for (mi = 0; mi < last_struct_member_count && mi < MAX_MEMBERS; mi++)
                                    st->members[mi] = last_struct_members[mi];
                            }
                        }
                        typedef_count++;
                    }
                }
                expect(p, TOK_SEMI);
                continue;
            }
            int decl_is_double = (peek(p).kind == TOK_DOUBLE);
            int decl_is_float_type = (peek(p).kind == TOK_FLOAT || peek(p).kind == TOK_DOUBLE);
            /* 检查类型说明符是否是指针 typedef（提前到 parse_type_specifier 之前 peek） */
            int ptr_typedef_pts = 0;
            {
                Token pt = peek(p);
                if (pt.kind == TOK_IDENT) {
                    char ptn[128];
                    int pnl = pt.len < 127 ? pt.len : 127;
                    int pci; for (pci = 0; pci < pnl; pci++) ptn[pci] = pt.start[pci]; ptn[pnl] = '\0';
                    for (int pti = 0; pti < typedef_count; pti++) {
                        if (strcmp(typedef_table[pti].name, ptn) == 0 && typedef_table[pti].ptr_level > 0) {
                            ptr_typedef_pts = typedef_table[pti].points_to; break; } } }
            }
            /* 检查类型是否为 struct typedef（提前到 parse_type_specifier 之前 peek） */
            const char *decl_typedef_tag = NULL;
            {
                Token pt = peek(p);
                if (pt.kind == TOK_IDENT) {
                    char ptn[128];
                    int pnl = pt.len < 127 ? pt.len : 127;
                    int pci; for (pci = 0; pci < pnl; pci++) ptn[pci] = pt.start[pci]; ptn[pnl] = ' ';
                    for (int pti = 0; pti < typedef_count; pti++) {
                        if (strcmp(typedef_table[pti].name, ptn) == 0 && typedef_table[pti].member_count > 0) {
                            decl_typedef_tag = typedef_table[pti].name; break; } } }
            }
            int ts = parse_type_specifier(p);
            if (ts < 0 && nq > 0) {
                /* 限定符后没有类型说明符 — 恢复，当作表达式处理 */
                p->lexer->pos = qpos;
                p->lexer->line = qline;
                p->lexer->col = qcol;
                p->tok = qtok;
                decl_is_double = (peek(p).kind == TOK_DOUBLE);
                decl_is_float_type = (peek(p).kind == TOK_FLOAT || peek(p).kind == TOK_DOUBLE);
                ts = parse_type_specifier(p);
            }
            /* 保存 struct 标签（逗号分隔变量需要，last_struct_tag 会在第一个变量后被清除） */
            const char *saved_struct_tag = last_struct_tag;
            if (ts >= 0) {
                AstNode *decl = new_ast(p, AST_VAR_DECL);
                int dv_ptrs = 0;
                decl->name = parse_declarator(p, &dv_ptrs);
                /* 函数指针数组：(*name[N])(params) — parse_declarator 捕获了维度，这里乘入总大小 */
                if (dv_ptrs > 0 && pdecl_fptr_array_dim > 0) {
                    decl->ival = 8 * pdecl_fptr_array_dim;
                    decl->type_size = 8;
                    decl->elem_size = 8;
                } else {
                    decl->ival = dv_ptrs > 0 ? 8 : (ts > 0 ? ts : 4);
                    decl->type_size = decl->ival;
                    decl->elem_size = (dv_ptrs > 1) ? 8 : (dv_ptrs == 1 ? ts : 0);
                }
                pdecl_fptr_array_dim = 0;
                /* 检查类型本身是否是指针 typedef（如 typedef char* string; string s;） */
                if (dv_ptrs == 0 && ts > 0 && ptr_typedef_pts > 0) {
                    decl->elem_size = ptr_typedef_pts; }
                decl->is_float = (dv_ptrs == 0 && decl_is_float_type) ?
                    last_type_is_float : 0;
                /* 指针类型的变量本身无符号性，但元素可能有 */
                if (dv_ptrs == 0) {
                    decl->is_unsigned = last_type_is_unsigned;
                    decl->elem_is_unsigned = last_type_is_unsigned;
                } else {
                    decl->is_unsigned = 0;
                    decl->elem_is_unsigned = last_type_is_unsigned;
                    decl->elem_is_float = last_type_is_float;
                }
                decl->is_static = q_static;
                if (decl->name && *decl->name) {
                    const char *resolved_tag = NULL;
                    if (decl_typedef_tag) {
                        pvar_add_ex(decl->name, decl_typedef_tag, decl->is_float, decl->is_unsigned, decl->ival);
                        resolved_tag = decl_typedef_tag;
                    } else if (saved_struct_tag || last_struct_member_count > 0) {
                        pvar_add_ex(decl->name, saved_struct_tag ? saved_struct_tag : "",
                                 decl->is_float, decl->is_unsigned, decl->ival);
                        resolved_tag = saved_struct_tag;
                    } else {
                        int ti;
                        int found_typedef = 0;
                        for (ti = 0; ti < typedef_count; ti++) {
                            if (typedef_table[ti].member_count > 0 && ts == typedef_table[ti].size) {
                                pvar_add_ex(decl->name, typedef_table[ti].name,
                                         decl->is_float, decl->is_unsigned, decl->ival);
                                resolved_tag = typedef_table[ti].name;
                                found_typedef = 1;
                                break;
                            }
                        }
                        if (!found_typedef)
                            pvar_add_ex(decl->name, NULL, decl->is_float, decl->is_unsigned, decl->ival);
                        /* 指针类型：记录指向类型是否为 float/double */
                        if (dv_ptrs > 0 && last_type_is_float)
                            pvar_set_elem_float(decl->name, last_type_is_float);
                    }
                    if (resolved_tag)
                        pvar_set_struct_type(decl->name, resolve_struct_type(resolved_tag));
                }
                last_struct_tag = NULL;
                /* 处理数组后缀 [N]（将数组维数乘入 ival；表达式维数跳过处理） */
                int first_dim = 0;
                int dim_count = 0;
                int bracket_count = 0;
                while (peek(p).kind == TOK_LBRACKET) {
                    bracket_count++;
                    consume(p);
                    if (peek(p).kind == TOK_RBRACKET) {
                        /* 空维度 int arr[] — 无操作 */
                    } else {
                        /* 维度表达式：int arr[4], int arr[4*1024], int arr[(4)] */
                        AstNode *dim_expr = parse_expr(p);
                        if (dim_expr) {
                            long long dim_val = eval_const_expr(dim_expr);
                            if (dim_val > 0) {
                                if (dim_count == 0) first_dim = (int)dim_val;
                                decl->ival *= (int)dim_val;
                                dim_count++;
                            }
                        }
                        /* 跳过到匹配的 ] */
                        int d = 1;
                        while (d > 0 && peek(p).kind != TOK_EOF) {
                            if (peek(p).kind == TOK_LBRACKET) d++;
                            if (peek(p).kind == TOK_RBRACKET) d--;
                            if (d) consume(p);
                        }
                    }
                    if (peek(p).kind == TOK_RBRACKET) consume(p);
                }
                if (dim_count > 0 && first_dim > 0) {
                    /* 数组：elem_size = 元素/行大小，base_elem_size = 基础元素类型 */
                    int elem_ts = (dv_ptrs > 0) ? 8 : (ts > 0 ? ts : 4);
                    decl->base_elem_size = (dv_ptrs == 1 && bracket_count > 0) ? (ts > 0 ? ts : 4) : elem_ts;
                    if (dv_ptrs > 0 && bracket_count > 0)
                        decl->elem_is_ptr = 1;
                    decl->is_array = 1;  /* 显式标记为数组 */
                    if (dim_count > 1)
                        decl->elem_size = decl->ival / first_dim; /* row size */
                    else
                        decl->elem_size = elem_ts;
                    /* 注册数组元素大小到 pvar（供 sizeof(arr[i]) 使用） */
                    if (decl->name && *decl->name) {
                        int pe = (dim_count > 1) ? decl->elem_size : elem_ts;
                        pvar_set_elem_size(decl->name, pe);
                    }
                } else {
                    if (dv_ptrs > 0 && bracket_count > 0) {
                        decl->elem_size = 8;
                        decl->base_elem_size = (dv_ptrs == 1) ? (ts > 0 ? ts : 4) : 8;
                        decl->elem_is_ptr = 1;
                    } else {
                        decl->base_elem_size = decl->elem_size;
                    }
                }
                /* 数组处理后更新 pvar 中的变量大小 */
                if (decl->name && *decl->name && decl->ival > 4) {
                    int pi;
                    for (pi = pvar_count - 1; pi >= 0; pi--)
                        if (strcmp(pvar_name[pi], decl->name) == 0)
                            { pvar_size_arr[pi] = decl->ival; break; }
                }
                /* 逗号分隔的多变量声明（含初始化）：int a = 1, b = 2, c; */
                AstNode *chain_tail = decl;
                /* 第一个变量的初始化 */
                if (match(p, TOK_EQ)) {
                    if (peek(p).kind == TOK_LBRACE) {
                        /* 检查是否为 struct 初始化（而非数组） */
                        int struct_brace = 0;
                        Member struct_mems[MAX_MEMBERS];
                        int struct_mem_cnt = 0;
                        if (dv_ptrs == 0 && ts > 0 && bracket_count == 0) {
                            if (decl_typedef_tag) {
                                int ti;
                                for (ti = 0; ti < typedef_count; ti++) {
                                    if (strcmp(typedef_table[ti].name, decl_typedef_tag) == 0 &&
                                        typedef_table[ti].member_count > 0) {
                                        struct_brace = 1;
                                        struct_mem_cnt = typedef_table[ti].member_count;
                                        int mi; for (mi = 0; mi < struct_mem_cnt && mi < MAX_MEMBERS; mi++)
                                            struct_mems[mi] = typedef_table[ti].members[mi];
                                        break;
                                    }
                                }
                            } else if (last_struct_member_count > 0) {
                                struct_brace = 1;
                                struct_mem_cnt = last_struct_member_count;
                                int mi; for (mi = 0; mi < struct_mem_cnt && mi < MAX_MEMBERS; mi++)
                                    struct_mems[mi] = last_struct_members[mi];
                            }
                        }
                        /* { expr1, expr2, ... } — 简单数组初始化 */
                        consume(p);
                        AstNode *prev_init = NULL;
                        int init_idx = 0;
                        while (peek(p).kind != TOK_RBRACE && peek(p).kind != TOK_EOF) {
                            const char *ipos = p->lexer->pos;  /* 防死循环：不支持 .member 指派初始化器 */
                            /* 多维数组嵌套 { } 初始化：int a[2][3] = {{1,2,3},{4,5,6}} */
                            if (!struct_brace && dim_count > 1 && peek(p).kind == TOK_LBRACE) {
                                consume(p); /* 跳过内层 { */
                                int inner_idx = 0;
                                while (peek(p).kind != TOK_RBRACE && peek(p).kind != TOK_EOF) {
                                    AstNode *ie = parse_expr(p);
                                    if (ie && decl->name) {
                                        /* 创建赋值 a[init_idx][inner_idx] = ie */
                                        AstNode *outer_idx = new_ast(p, AST_CONSTANT);
                                        outer_idx->ival = init_idx;
                                        AstNode *inner_idx_node = new_ast(p, AST_CONSTANT);
                                        inner_idx_node->ival = inner_idx;
                                        AstNode *var_node = new_ast(p, AST_VAR);
                                        var_node->name = decl->name;
                                        AstNode *outer_sub = new_ast(p, AST_BINOP);
                                        outer_sub->op = TOK_LBRACKET;
                                        outer_sub->left = var_node;
                                        outer_sub->right = outer_idx;
                                        AstNode *inner_sub = new_ast(p, AST_BINOP);
                                        inner_sub->op = TOK_LBRACKET;
                                        inner_sub->left = outer_sub;
                                        inner_sub->right = inner_idx_node;
                                        AstNode *assign = new_ast(p, AST_ASSIGN);
                                        assign->left = inner_sub;
                                        assign->right = ie;
                                        assign->type_size = (ts > 0 ? ts : 4);
                                        if (prev_init) prev_init->next = assign;
                                        else decl->expr = assign;
                                        prev_init = assign;
                                        inner_idx++;
                                    }
                                    if (peek(p).kind == TOK_COMMA) consume(p);
                                }
                                if (peek(p).kind == TOK_RBRACE) consume(p);
                                init_idx++;
                            } else {
                                AstNode *ie = parse_expr(p);
                                if (!ie && p->lexer->pos == ipos) { consume(p); continue; }
                                if (ie && decl->name) {
                                    if (struct_brace && init_idx < struct_mem_cnt) {
                                        /* struct 初始化：s.member = expr */
                                        AstNode *var = new_ast(p, AST_VAR);
                                        var->name = decl->name;
                                        AstNode *member = new_ast(p, AST_MEMBER);
                                        member->left = var;
                                        member->member_name = struct_mems[init_idx].name;
                                        member->ival = struct_mems[init_idx].offset;
                                        member->type_size = struct_mems[init_idx].size;
                                        AstNode *assign = new_ast(p, AST_ASSIGN);
                                        assign->left = member;
                                        assign->right = ie;
                                        assign->type_size = struct_mems[init_idx].size;
                                        if (prev_init) prev_init->next = assign;
                                        else decl->expr = assign;
                                        prev_init = assign;
                                    } else {
                                        /* 数组初始化：a[i] = expr */
                                        AstNode *idx = new_ast(p, AST_CONSTANT);
                                        idx->ival = init_idx;
                                        AstNode *sub = new_ast(p, AST_BINOP);
                                        sub->op = TOK_LBRACKET;
                                        AstNode *var = new_ast(p, AST_VAR);
                                        var->name = decl->name;
                                        sub->left = var;
                                        sub->right = idx;
                                        AstNode *assign = new_ast(p, AST_ASSIGN);
                                        assign->left = sub;
                                        assign->right = ie;
                                        assign->type_size = (ts > 0 ? ts : 4);
                                        if (prev_init) prev_init->next = assign;
                                        else decl->expr = assign;
                                        prev_init = assign;
                                    }
                                }
                                init_idx++;
                            }
                            if (peek(p).kind == TOK_COMMA) consume(p);
                        }
                        if (peek(p).kind == TOK_RBRACE) consume(p);
                        /* 根据初始化元素修正数组大小（int arr[] = {1,2,3} → 3 元素） */
                        /* struct 初始化的变量大小 = sizeof(struct)，不由 init_idx 修正 */
                        if (!struct_brace && dim_count == 0 && init_idx > 0) {
                            int elem_ts = (dv_ptrs > 0) ? 8 : (ts > 0 ? ts : 4);
                            decl->ival = init_idx * elem_ts;
                            /* type_size 保持元素类型大小（不改写成 total size），
                             * 以便 cgen.c 的 is_array 判断能正确识别 */
                            decl->elem_size = elem_ts;
                            /* base_elem_size: 指针数组如 char *arr[] 的基础元素是 char(ts) 而非指针 */
                            if (dv_ptrs > 0)
                                decl->base_elem_size = (ts > 0 ? ts : 4);
                            else
                                decl->base_elem_size = elem_ts;
                            decl->is_array = 1;
                            /* 更新局部变量表的大小 */
                            if (decl->name && *decl->name) {
                                int pi;
                                for (pi = 0; pi < pvar_count; pi++)
                                    if (strcmp(pvar_name[pi], decl->name) == 0)
                                        { pvar_size_arr[pi] = decl->ival; break; }
                            }
                        }
                    } else {
                        decl->expr = parse_expr(p);
                        /* char buf[] = "str" — 用字符串长度修正数组类型大小 */
                        if (ts == 1 && bracket_count > 0 && dim_count == 0 &&
                            decl->expr && decl->expr->kind == AST_STRING) {
                            int slen = 0;
                            while (decl->expr->str_val[slen]) slen++;
                            slen++; /* 包含 null 终止符 */
                            decl->ival = slen;
                            /* type_size 保持元素类型大小（不改成 total size） */
                            decl->elem_size = ts;
                            decl->base_elem_size = ts;
                            decl->is_array = 1;
                            if (decl->name && *decl->name)
                                pvar_set_elem_size(decl->name, ts);
                        }
                    }
                }
                /* 逗号后的后续变量 */
                while (peek(p).kind == TOK_COMMA) {
                    consume(p);
                    int c_ptrs = 0;
                    const char *cname = parse_declarator(p, &c_ptrs);
                    AstNode *cdecl = new_ast(p, AST_VAR_DECL);
                    cdecl->name = cname;
                    cdecl->ival = (c_ptrs > 0) ? 8 : (ts > 0 ? ts : 4);
                    cdecl->type_size = cdecl->ival;
                    cdecl->elem_size = (c_ptrs > 1) ? 8
                        : (c_ptrs == 1 ? ts : 0);
                    cdecl->is_float = (c_ptrs == 0 && decl_is_float_type) ? last_type_is_float : 0;
                    if (c_ptrs == 0) {
                        cdecl->is_unsigned = last_type_is_unsigned;
                        cdecl->elem_is_unsigned = 0;
                    } else {
                        cdecl->is_unsigned = 0;
                        cdecl->elem_is_unsigned = last_type_is_unsigned;
                        cdecl->elem_is_float = last_type_is_float;
                    }
                    cdecl->is_static = q_static;
                    /* 注册局部变量名 */
                    if (cname && *cname) {
                        pvar_add_ex(cname, saved_struct_tag ? saved_struct_tag : NULL, cdecl->is_float, cdecl->is_unsigned, cdecl->ival);
                        if (saved_struct_tag && *saved_struct_tag)
                            pvar_set_struct_type(cname, resolve_struct_type(saved_struct_tag));
                        if (c_ptrs > 0 && last_type_is_float)
                            pvar_set_elem_float(cname, last_type_is_float);
                    }
                    /* 处理数组后缀 */
                    int comma_was_bracket = 0;
                    while (peek(p).kind == TOK_LBRACKET) {
                        comma_was_bracket = 1;
                        consume(p);
                        if (peek(p).kind == TOK_NUMBER && peek(p).ival > 0) {
                            cdecl->ival *= peek(p).ival;
                            consume(p);
                        }
                        int d = 1;
                        while (d > 0 && peek(p).kind != TOK_EOF) {
                            if (peek(p).kind == TOK_LBRACKET) d++;
                            if (peek(p).kind == TOK_RBRACKET) d--;
                            if (d) consume(p);
                        }
                        if (peek(p).kind == TOK_RBRACKET) consume(p);
                    }
                    if (comma_was_bracket && cdecl->elem_size > 0)
                        cdecl->is_array = 1;
                    /* 数组处理后更新 pvar 中的变量大小 */
                    if (cname && *cname && cdecl->ival > 4) {
                        int pi;
                        for (pi = 0; pi < pvar_count; pi++)
                            if (strcmp(pvar_name[pi], cname) == 0)
                                { pvar_size_arr[pi] = cdecl->ival; break; }
                    }
                    /* 后续变量的初始化 */
                    if (match(p, TOK_EQ)) {
                        if (peek(p).kind == TOK_LBRACE) {
                            int d = 1; consume(p);
                            while (d > 0 && peek(p).kind != TOK_EOF) {
                                if (peek(p).kind == TOK_LBRACE) d++;
                                if (peek(p).kind == TOK_RBRACE) d--;
                                if (d) consume(p);
                            }
                            if (peek(p).kind == TOK_RBRACE) consume(p);
                        } else {
                            cdecl->expr = parse_expr(p);
                        }
                    }
                    chain_tail->next = cdecl;
                    chain_tail = cdecl;
                }
                /* 函数原型 int snprintf(...); — 遇 LPAREN 则跳过 */
                if (peek(p).kind == TOK_LPAREN) {
                    int d = 1; consume(p);
                    while (d > 0 && peek(p).kind != TOK_EOF) {
                        if (peek(p).kind == TOK_LPAREN) d++;
                        if (peek(p).kind == TOK_RPAREN) d--;
                        if (d) consume(p);
                    }
                    if (peek(p).kind == TOK_RPAREN) consume(p);
                    expect(p, TOK_SEMI);
                    continue;
                }
                expect(p, TOK_SEMI);
                *tail = decl;
                /* 找到链条末尾的 next 指针（逗号循环可能已添加多个节点） */
                {
                    AstNode *last = decl;
                    while (last->next) last = last->next;
                    tail = &last->next;
                }
            } else {
                AstNode *stmt = parse_statement(p);
                if (stmt) {
                    *tail = stmt;
                    tail = &stmt->next;
                } else if (p->lexer->pos == prev_pos) {
                    break;  /* 没有消耗任何 token，防死循环 */
                }
            }
        }
    }

    expect(p, TOK_RBRACE);
    p->block_depth--;

    AstNode *block = new_ast(p, AST_BLOCK);
    block->stmts = head;
    return block;
}

/* ─── 分派语句解析 ─── */

static AstNode *parse_statement(Parser *p) {
    Token t = peek(p);
    switch (t.kind) {
    case TOK_RETURN:  return parse_return_statement(p);
    case TOK_IF:      return parse_if_statement(p);
    case TOK_WHILE:   return parse_while_statement(p);
    case TOK_FOR:     return parse_for_statement(p);
    case TOK_DO:      return parse_do_while(p);
    case TOK_SWITCH:  return parse_switch_statement(p);
    case TOK_CASE:
        if (p->switch_depth == 0) {
            error_at(p, "'case' outside switch");
            consume(p); /* skip 'case' */
            /* 跳过直至 : 或 ; 或 } 来恢复 */
            while (p->tok.kind != TOK_EOF && p->tok.kind != TOK_SEMI
                   && p->tok.kind != TOK_RBRACE && p->tok.kind != TOK_COLON)
                consume(p);
            if (p->tok.kind == TOK_COLON || p->tok.kind == TOK_SEMI) consume(p);
            return NULL;
        }
        consume(p);
        { AstNode *n = new_ast(p, AST_CASE);
          AstNode *val = parse_expr(p);
          n->ival = (val && val->kind == AST_CONSTANT) ? val->ival : 0;
          n->right = NULL;  /* default: no range */
          if (peek(p).kind == TOK_ELLIPSIS) {
              consume(p);  /* ... */
              AstNode *val2 = parse_expr(p);
              AstNode *rnode = new_ast(p, AST_CONSTANT);
              rnode->ival = (val2 && val2->kind == AST_CONSTANT) ? val2->ival : 0;
              n->right = rnode;
          }
          expect(p, TOK_COLON); return n; }
    case TOK_DEFAULT:
        if (p->switch_depth == 0) {
            error_at(p, "'default' outside switch");
            consume(p); /* skip 'default' */
            error_recover(p);
            return NULL;
        }
        consume(p);
        expect(p, TOK_COLON);
        return new_ast(p, AST_DEFAULT);
    case TOK_BREAK:   return parse_break(p);
    case TOK_CONTINUE: return parse_continue(p);
    case TOK_GOTO:
        consume(p);
        if (peek(p).kind == TOK_IDENT) {
            Token lt = consume(p);
            AstNode *n = new_ast(p, AST_GOTO);
            n->name = arena_strdup(p->arena, lt.start, lt.len);
            expect(p, TOK_SEMI);
            return n;
        }
        error_at(p, "expected label name");
        return NULL;
    case TOK__ASM__: {
        
        /* __asm__ [volatile] ("template" [: outputs [: inputs [: clobbers ]]]) */

        int asm_volatile = 0;
        consume(p);
        if (peek(p).kind == TOK_VOLATILE) { consume(p); asm_volatile = 1; }
        expect(p, TOK_LPAREN);
        AstNode *n = new_ast(p, AST_ASM);
        n->asm_.asm_template = NULL;
        n->asm_.is_volatile = asm_volatile;
        n->asm_.outputs = NULL; n->asm_.output_count = 0;
        n->asm_.inputs  = NULL; n->asm_.input_count  = 0;
        n->asm_.clobbers = NULL; n->asm_.clobber_count = 0;

        /* 模板字符串 */
        if (peek(p).kind == TOK_STRING) {
            Token s = consume(p);
            int slen = s.len - 2;
            if (slen > 0) {
                char *buf = arena_alloc(p->arena, slen + 1);
                int ci;
                for (ci = 0; ci < slen; ci++) buf[ci] = s.start[ci + 1];
                buf[slen] = '\0';
                n->asm_.asm_template = buf;
            }
        }

        #define MAX_ASM_OPS 16

        /* ── :输出 ── */
        if (peek(p).kind == TOK_COLON) {
            consume(p);
            AsmOperand ops[MAX_ASM_OPS];
            int cnt = 0;
            while (peek(p).kind != TOK_COLON && peek(p).kind != TOK_RPAREN && peek(p).kind != TOK_EOF) {
                if (cnt >= MAX_ASM_OPS) { error_at(p, "too many asm operands"); break; }
                if (peek(p).kind == TOK_LBRACKET) { consume(p); consume(p); expect(p, TOK_RBRACKET); }
                if (peek(p).kind != TOK_STRING) { error_at(p, "expected constraint string"); break; }
                Token cstr = consume(p);
                int clen = cstr.len - 2;
                char *cbuf = arena_alloc(p->arena, clen + 1);
                int ci; for (ci = 0; ci < clen; ci++) cbuf[ci] = cstr.start[ci + 1];
                cbuf[clen] = '\0';
                ops[cnt].constraint = cbuf;
                expect(p, TOK_LPAREN);
                ops[cnt].expr = parse_expr_comma(p);
                expect(p, TOK_RPAREN);
                cnt++;
                if (peek(p).kind == TOK_COMMA) consume(p);
            }
            if (cnt > 0) {
                n->asm_.outputs = arena_alloc(p->arena, sizeof(AsmOperand) * cnt);
                int i; for (i = 0; i < cnt; i++) n->asm_.outputs[i] = ops[i];
                n->asm_.output_count = cnt;
            }
        }

        /* ── :输入 ── */
        if (peek(p).kind == TOK_COLON) {
            consume(p);
            AsmOperand ops[MAX_ASM_OPS];
            int cnt = 0;
            while (peek(p).kind != TOK_COLON && peek(p).kind != TOK_RPAREN && peek(p).kind != TOK_EOF) {
                if (cnt >= MAX_ASM_OPS) { error_at(p, "too many asm operands"); break; }
                if (peek(p).kind == TOK_LBRACKET) { consume(p); consume(p); expect(p, TOK_RBRACKET); }
                if (peek(p).kind != TOK_STRING) { error_at(p, "expected constraint string"); break; }
                Token cstr = consume(p);
                int clen = cstr.len - 2;
                char *cbuf = arena_alloc(p->arena, clen + 1);
                int ci; for (ci = 0; ci < clen; ci++) cbuf[ci] = cstr.start[ci + 1];
                cbuf[clen] = '\0';
                ops[cnt].constraint = cbuf;
                expect(p, TOK_LPAREN);
                ops[cnt].expr = parse_expr_comma(p);
                expect(p, TOK_RPAREN);
                cnt++;
                if (peek(p).kind == TOK_COMMA) consume(p);
            }
            if (cnt > 0) {
                n->asm_.inputs = arena_alloc(p->arena, sizeof(AsmOperand) * cnt);
                int i; for (i = 0; i < cnt; i++) n->asm_.inputs[i] = ops[i];
                n->asm_.input_count = cnt;
            }
        }

        /* ── :破坏列表 ── */
        if (peek(p).kind == TOK_COLON) {
            consume(p);
            const char *clist[MAX_ASM_OPS];
            int cnt = 0;
            while (peek(p).kind != TOK_RPAREN && peek(p).kind != TOK_EOF) {
                if (cnt >= MAX_ASM_OPS) { error_at(p, "too many clobbers"); break; }
                if (peek(p).kind != TOK_STRING) break;
                Token ct = consume(p);
                int clen = ct.len - 2;
                char *cbuf = arena_alloc(p->arena, clen + 1);
                int ci; for (ci = 0; ci < clen; ci++) cbuf[ci] = ct.start[ci + 1];
                cbuf[clen] = '\0';
                clist[cnt++] = cbuf;
                if (peek(p).kind == TOK_COMMA) consume(p);
            }
            if (cnt > 0) {
                n->asm_.clobbers = arena_alloc(p->arena, sizeof(char *) * cnt);
                int i; for (i = 0; i < cnt; i++) n->asm_.clobbers[i] = clist[i];
                n->asm_.clobber_count = cnt;
            }
        }

        expect(p, TOK_RPAREN);
        expect(p, TOK_SEMI);
        return n;
    }
    case TOK_LBRACE:  return parse_compound_statement(p);
    case TOK_SEMI:    consume(p); return new_ast(p, AST_NULL_STMT);
    default: {
        AstNode *expr = parse_expr_comma(p);
        if (!expect(p, TOK_SEMI)) error_recover(p);
        if (!expr) return NULL;
        AstNode *n = new_ast(p, AST_EXPR_STMT);
        n->expr = expr;
        return n;
    }
    }
}

/* ─── 参数列表 ─── */
/* is_variadic: 输出，表示是否有 ... */

static AstNode *parse_parameter_list(Parser *p, int *is_variadic) {
    if (is_variadic) *is_variadic = 0;
    /* 函数指针声明器 (*name)(params) 已在 parse_declarator 中跳过了参数，
     * 此时当前 token 不是 '('，直接返回 NULL */
    if (peek(p).kind != TOK_LPAREN) return NULL;
    consume(p);  /* 跳过 '(' */
    AstNode *head = NULL;
    AstNode **tail = &head;

    /* 空参数列表函数声明 f() */
    if (peek(p).kind == TOK_RPAREN) {
        consume(p);
        return head;
    }

    while (peek(p).kind != TOK_RPAREN && peek(p).kind != TOK_EOF) {
        if (peek(p).kind == TOK_ELLIPSIS) {
            consume(p);
            if (is_variadic) *is_variadic = 1;
            break;
        }
        /* 跳过限定符 */
        while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
               peek(p).kind == TOK_RESTRICT) consume(p);

        int param_is_double = 0;
        int param_is_float_type = 0;
        int psz = 0;

        if (peek(p).kind == TOK_VOID) {
            /* 注意：不能做 Lexer save/restore——stage-1 tcc 的 struct copy 有 bug */
            consume(p);
            if (peek(p).kind == TOK_RPAREN) {
                /* f(void) */
                expect(p, TOK_RPAREN);
                return head;
            }
            if (peek(p).kind == TOK_COMMA) {
                /* f(void, ...) — 跳过 void */
                consume(p);
                continue;
            }
            /* void *name 或 void name — psz=0, param_is_double=0 */
            last_type_is_unsigned = 0;
        } else {
            param_is_double = (peek(p).kind == TOK_DOUBLE);
            param_is_float_type = (peek(p).kind == TOK_FLOAT || peek(p).kind == TOK_DOUBLE);
            psz = parse_type_specifier(p);
        }

        int ptr_level = 0;
        const char *pname = parse_declarator(p, &ptr_level);
        /* [] 在参数中退化为指针，计入有效指针层数 */
        int has_brackets = 0;
        while (peek(p).kind == TOK_LBRACKET) {
            has_brackets = 1;
            int d = 1; consume(p);
            while (d > 0 && peek(p).kind != TOK_EOF) {
                if (peek(p).kind == TOK_LBRACKET) d++;
                if (peek(p).kind == TOK_RBRACKET) d--;
                if (d) consume(p);
            }
            if (peek(p).kind == TOK_RBRACKET) consume(p);
        }
        if (pname && *pname) {
            int effective_ptr = ptr_level + has_brackets;
            AstNode *pd = new_ast(p, AST_VAR_DECL);
            pd->name = pname;
            /* 指针类型统一为 8 字节，避免与相邻局部变量偏移重叠 */
            pd->ival = (effective_ptr > 0) ? 8 : (psz > 0 ? psz : 4);
            pd->type_size = pd->ival;
            pd->elem_size = (effective_ptr > 1) ? 8 : (effective_ptr == 1 ? psz : 0);
            pd->base_elem_size = (effective_ptr > 1) ? (psz > 0 ? psz : 4) : (effective_ptr == 1 ? psz : 0);
            pd->is_float = (ptr_level == 0 && param_is_float_type) ? last_type_is_float : 0;
            pd->is_unsigned = (effective_ptr == 0) ? last_type_is_unsigned : 0;
            pd->elem_is_unsigned = (effective_ptr > 0) ? last_type_is_unsigned : 0;
            {
                const char *param_tag = NULL;
                if (last_struct_tag && *last_struct_tag)
                    param_tag = last_struct_tag;
                else {
                    /* 检查是否为 typedef struct */
                    Token ptok = peek(p);
                    if (ptok.kind == TOK_IDENT) {
                        char ptn[128];
                        int pnl = ptok.len < 127 ? ptok.len : 127;
                        int pci; for (pci = 0; pci < pnl; pci++) ptn[pci] = ptok.start[pci]; ptn[pnl] = '\0';
                        int pti;
                        for (pti = 0; pti < typedef_count; pti++) {
                            if (strcmp(typedef_table[pti].name, ptn) == 0 && typedef_table[pti].member_count > 0) {
                                param_tag = typedef_table[pti].name;
                                break;
                            }
                        }
                    }
                }
                pvar_add_ex(pname, param_tag, pd->is_float, pd->is_unsigned, pd->ival);
                if (param_tag)
                    pvar_set_struct_type(pname, resolve_struct_type(param_tag));
            }
            *tail = pd;
            tail = &pd->next;
        }
        if (peek(p).kind == TOK_COMMA) consume(p);
    }
    expect(p, TOK_RPAREN);
    return head;
}


/* ─── 顶层解析入口 ─── */

AstNode *parse_program(Parser *p) {
    parsed_func_ret_count = 0;  /* 重置解析期返回类型表（每个文件一个编译单元） */
    AstNode *head = NULL;
    AstNode **tail = &head;

    while (peek(p).kind != TOK_EOF) {
        /* 处理顶层 __asm__ */
        if (peek(p).kind == TOK__ASM__) {
            AstNode *asm_node = parse_statement(p);
            if (asm_node) { *tail = asm_node; tail = &asm_node->next; }
            continue;
        }
        /* 处理 typedef */
        if (peek(p).kind == TOK_TYPEDEF) {
            consume(p);
            /* 解析 typedef 定义 */
            while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
                   peek(p).kind == TOK_RESTRICT || peek(p).kind == TOK__ATTRIBUTE__) {
                if (peek(p).kind == TOK__ATTRIBUTE__) {
                    consume(p); expect(p, TOK_LPAREN); expect(p, TOK_LPAREN);
                    int d = 2; while (d > 0 && peek(p).kind != TOK_EOF) {
                        if (peek(p).kind == TOK_LPAREN) d++;
                        if (peek(p).kind == TOK_RPAREN) d--;
                        consume(p); }
                } else { consume(p); }
            }
            int tsz = parse_type_specifier(p);
            /* 匿名 typedef（如 typedef enum { ... };）— 无 declarator */
            if (peek(p).kind == TOK_SEMI) {
                expect(p, TOK_SEMI);
                continue;
            }
            int tptr_level = 0;
            const char *tname = parse_declarator(p, &tptr_level);
            if (tname && *tname) {
                if (typedef_count >= MAX_TYPEDEFS) {
                    error_at(p, "too many typedefs");
                    break; }
                /* 检查是否已存在同名 typedef（头文件多重包含导致重复） */
                int dup = 0;
                int ti;
                for (ti = 0; ti < typedef_count; ti++) {
                    if (strcmp(typedef_table[ti].name, tname) == 0) { dup = 1; break; }
                }
                if (!dup) {
                    TypedefEntry *te = &typedef_table[typedef_count];
                    te->name = tname;
                    te->size = tptr_level > 0 ? 8 : tsz;
                    te->type_kind = tptr_level > 0 ? 2 : (last_struct_member_count > 0 ? 1 : 0);
                    te->ptr_level = tptr_level;
                    te->points_to = tptr_level > 0 ? tsz : 0;
                    te->is_unsigned = (tptr_level == 0) ? last_type_is_unsigned : 0;
                    te->struct_idx = -1;
                    /* 对 struct typedef 保存成员信息 */
                    if (last_struct_member_count > 0) {
                        te->member_count = last_struct_member_count;
                        int mi;
                        for (mi = 0; mi < last_struct_member_count && mi < MAX_MEMBERS; mi++)
                            te->members[mi] = last_struct_members[mi];
                        /* 注册 typedef 名到 tag_table（匿名 struct 用 typedef 名做 tag） */
                        if (!find_struct_tag(tname)) {
                            if (tag_count >= MAX_TAGS) {
                                __write(2, "toyc: too many struct/union tags\n", 32);
                                __exit(1); }
                            StructType *st = &tag_table[tag_count++];
                            st->tag = tname;
                            st->total_size = tsz;
                            st->member_count = last_struct_member_count;
                            for (mi = 0; mi < last_struct_member_count && mi < MAX_MEMBERS; mi++)
                                st->members[mi] = last_struct_members[mi];
                        }
                    }
                    typedef_count++;
                } else if (last_struct_member_count > 0 && tsz > 0) {
                    /* 重复的 struct typedef（前向声明 + 完整定义模式）：
                     * 用真实 body 的大小和成员覆盖前向声明的占位值 */
                    TypedefEntry *te = &typedef_table[ti];
                    te->size = tptr_level > 0 ? 8 : tsz;
                    te->type_kind = 1;
                    te->member_count = last_struct_member_count;
                    te->ptr_level = tptr_level;
                    te->points_to = tptr_level > 0 ? tsz : 0;
                    {
                        int mi;
                        for (mi = 0; mi < last_struct_member_count && mi < MAX_MEMBERS; mi++)
                            te->members[mi] = last_struct_members[mi];
                    }
                    /* 更新 tag_table 中的对应条目（如果存在） */
                    {
                        StructType *st = find_struct_tag(tname);
                        if (st) {
                            st->total_size = tsz;
                            st->member_count = last_struct_member_count;
                            int mi;
                            for (mi = 0; mi < last_struct_member_count && mi < MAX_MEMBERS; mi++)
                                st->members[mi] = last_struct_members[mi];
                        }
                    }
                }
            }
            expect(p, TOK_SEMI);
            continue;
        }

        /* 跳过 extern "C" { ... } */
        if (peek(p).kind == TOK_EXTERN) {
            const char *ep = p->lexer->pos;
            int el = p->lexer->line, ec = p->lexer->col;
            Token et = p->tok;
            consume(p);
            if (peek(p).kind == TOK_STRING) {
                consume(p); /* "C" */
                if (peek(p).kind == TOK_LBRACE) {
                    int d = 1; consume(p);
                    while (d > 0 && peek(p).kind != TOK_EOF) {
                        if (peek(p).kind == TOK_LBRACE) d++;
                        if (peek(p).kind == TOK_RBRACE) d--;
                        if (d) consume(p);
                    }
                    if (peek(p).kind == TOK_RBRACE) consume(p);
                }
                continue;
            }
            p->lexer->pos = ep; p->lexer->line = el;
            p->lexer->col = ec; p->tok = et;
        }
        /* 跳过存储类和限定符 */
        /* 处理 enum 定义 — 委托给 parse_type_specifier 注册成员值 */
        if (peek(p).kind == TOK_ENUM) {
            int esz = parse_type_specifier(p);
            if (esz < 0) esz = 4;
            /* 跳过可选的变量声明、逗号分隔和初始化器 */
            while (peek(p).kind == TOK_IDENT) {
                consume(p);
                /* 跳过数组后缀 */
                while (peek(p).kind == TOK_LBRACKET) {
                    int d = 1; consume(p);
                    while (d > 0 && peek(p).kind != TOK_EOF) {
                        if (peek(p).kind == TOK_LBRACKET) d++;
                        if (peek(p).kind == TOK_RBRACKET) d--;
                        if (d) consume(p);
                    }
                    if (peek(p).kind == TOK_RBRACKET) consume(p);
                }
                if (match(p, TOK_EQ)) {
                    if (peek(p).kind == TOK_LBRACE) {
                        int d = 1; consume(p);
                        while (d > 0 && peek(p).kind != TOK_EOF) {
                            if (peek(p).kind == TOK_LBRACE) d++;
                            if (peek(p).kind == TOK_RBRACE) d--;
                            if (d) consume(p);
                        }
                        if (peek(p).kind == TOK_RBRACE) consume(p);
                    } else {
                        parse_expr(p);
                    }
                }
                if (peek(p).kind == TOK_COMMA) { consume(p); continue; }
                break;
            }
            expect(p, TOK_SEMI);
            continue;
        }
        int current_static = 0;
        int current_extern = 0;
        while (peek(p).kind == TOK_STATIC || peek(p).kind == TOK_EXTERN ||
               peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
               peek(p).kind == TOK_RESTRICT || peek(p).kind == TOK_REGISTER ||
               peek(p).kind == TOK_INLINE || peek(p).kind == TOK__ATTRIBUTE__) {
            if (peek(p).kind == TOK_STATIC)
                current_static = 1;
            if (peek(p).kind == TOK_EXTERN)
                current_extern = 1;
            if (peek(p).kind == TOK__ATTRIBUTE__) {
                consume(p);
                expect(p, TOK_LPAREN);
                expect(p, TOK_LPAREN);
                int depth = 2;
                while (depth > 0 && peek(p).kind != TOK_EOF) {
                    if (peek(p).kind == TOK_LPAREN) depth++;
                    if (peek(p).kind == TOK_RPAREN) depth--;
                    consume(p);
                }
            } else {
                consume(p);
            }
        }

        /* 检查类型是否为 struct typedef（用于全局变量 pvar 注册） */
        const char *global_typedef_tag = NULL;
        {
            Token pt = peek(p);
            if (pt.kind == TOK_IDENT) {
                char ptn[128];
                int pnl = pt.len < 127 ? pt.len : 127;
                int pci; for (pci = 0; pci < pnl; pci++) ptn[pci] = pt.start[pci]; ptn[pnl] = '\0';
                for (int pti = 0; pti < typedef_count; pti++) {
                    if (strcmp(typedef_table[pti].name, ptn) == 0 && typedef_table[pti].member_count > 0) {
                        global_typedef_tag = typedef_table[pti].name; break; } } }
        }
        int typesize = parse_type_specifier(p);
        /* 保存返回类型的 struct tag、float、unsigned 状态（其后 parse_parameter_list 会覆盖这些全局变量） */
        const char *ret_struct_tag = last_struct_tag;
        int ret_type_is_float = last_type_is_float;
        int ret_type_is_unsigned = last_type_is_unsigned;
        if (typesize < 0) {
            error_at(p, "expected type specifier");
            break;
        }

        /* 单独的 struct { ... }; 或 struct tag { ... }; 定义（无变量名） */
        if (peek(p).kind == TOK_SEMI) {
            consume(p);
            continue;
        }

        const char *save_pos = p->lexer->pos;
        int save_line = p->lexer->line;
        int save_col = p->lexer->col;
        Token saved_tok = p->tok;

        while (peek(p).kind == TOK_STAR) consume(p);
        while (peek(p).kind == TOK__ATTRIBUTE__) {
            consume(p); expect(p, TOK_LPAREN); expect(p, TOK_LPAREN);
            int d = 2;
            while (d > 0 && peek(p).kind != TOK_EOF) {
                if (peek(p).kind == TOK_LPAREN) d++;
                if (peek(p).kind == TOK_RPAREN) d--;
                consume(p);
            }
        }

        /* 检查是否为函数指针声明器 (*name)(...) 或其变体 */
        if (peek(p).kind == TOK_LPAREN) {
            /* 可能是 (*name)(params) 函数指针 — 使用 parse_declarator */
        } else if (peek(p).kind != TOK_IDENT) {
            p->lexer->pos = save_pos;
            p->lexer->line = save_line;
            p->lexer->col = save_col;
            p->tok = saved_tok;
            error_at(p, "expected identifier");
            break;
        } else {
            consume(p);
        }
        int is_func = (peek(p).kind == TOK_LPAREN);

        p->lexer->pos = save_pos;
        p->lexer->line = save_line;
        p->lexer->col = save_col;
        p->tok = saved_tok;

        if (is_func) {
            /* 先解析声明符 + 参数列表，看是定义还是声明 */
            int fptr_level = 0;
            const char *fname = parse_declarator(p, &fptr_level);
            int is_variadic_f = 0;
            AstNode *fparams = parse_parameter_list(p, &is_variadic_f);
            if (peek(p).kind == TOK_SEMI) {
                /* 函数原型：只声明不定义 */
                /* 记录返回类型大小供代码生成器使用（跨 TU 函数调用的 hidden pointer 约定、
                 * int→long 符号扩展等） */
                if (fptr_level == 0 && fname &&
                    parsed_func_ret_count < MAX_FUNC_RET_TYPES) {
                    parsed_func_ret_names[parsed_func_ret_count] = fname;
                    parsed_func_ret_sizes[parsed_func_ret_count] = typesize;
                    parsed_func_ret_float[parsed_func_ret_count] = ret_type_is_float;
                    parsed_func_ret_unsigned[parsed_func_ret_count] = ret_type_is_unsigned;
                    parsed_func_ret_count++;
                }
                /* 记录 struct 返回类型的 tag（供 AST_MEMBER .member 查找成员偏移） */
                if (fptr_level == 0 && typesize > 8 && parse_func_ret_count < MAX_PARSE_FUNC_RET) {
                    StructType *ret_st = NULL;
                    if (ret_struct_tag && *ret_struct_tag)
                        ret_st = find_struct_tag(ret_struct_tag);
                    if (ret_st) {
                        parse_func_ret_name[parse_func_ret_count] = fname;
                        parse_func_ret_type[parse_func_ret_count] = ret_st;
                        parse_func_ret_count++;
                    }
                }
                consume(p);
            } else if (peek(p).kind == TOK_EQ) {
                /* 函数指针变量带初始化器：int (*f)(args) = value; */
                consume(p); /* = */
                if (peek(p).kind == TOK_LBRACE) {
                    int d = 1; consume(p);
                    while (d > 0 && peek(p).kind != TOK_EOF) {
                        if (peek(p).kind == TOK_LBRACE) d++;
                        if (peek(p).kind == TOK_RBRACE) d--;
                        if (d) consume(p);
                    }
                    if (peek(p).kind == TOK_RBRACE) consume(p);
                } else {
                    parse_expr(p);
                }
                expect(p, TOK_SEMI);
            } else {
                /* 函数定义 */
                p->func_depth++;
                AstNode *fbody = parse_compound_statement(p);
                p->func_depth--;
                /* 先统计参数个数（在链入 body 之前） */
                int pcount = 0; { AstNode *pp; for (pp = fparams; pp; pp = pp->next) pcount++; }
                /* 将参数声明前置到函数体 */
                if (fparams) {
                    AstNode *last_p = fparams;
                    while (last_p->next) last_p = last_p->next;
                    last_p->next = fbody->stmts;
                    fbody->stmts = fparams;
                }
                AstNode *func = new_ast(p, AST_FUNC_DEF);
                func->name = fname;
                func->params = fparams;
                func->body = fbody;
                func->is_static = current_static;
                func->is_variadic = is_variadic_f;
                func->ival = pcount;
                func->type_size = (fptr_level > 0) ? 8 : typesize;  /* 存储返回类型大小：指针返回 8 字节，struct 按值返回使用原大小 */
                func->is_float = (fptr_level == 0) ? ret_type_is_float : 0;
                func->is_unsigned = (fptr_level == 0) ? ret_type_is_unsigned : 0;
                /* 记录到 parsed_func_ret 表供调用点代码生成与返回类型推断 */
                if (fptr_level == 0 && fname &&
                    parsed_func_ret_count < MAX_FUNC_RET_TYPES) {
                    parsed_func_ret_names[parsed_func_ret_count] = fname;
                    parsed_func_ret_sizes[parsed_func_ret_count] = typesize;
                    parsed_func_ret_float[parsed_func_ret_count] = func->is_float;
                    parsed_func_ret_unsigned[parsed_func_ret_count] = ret_type_is_unsigned;
                    parsed_func_ret_count++;
                }
                /* 记录 struct 返回类型（供解析期 func().member 使用） */
                if (fptr_level == 0 && typesize > 8 && parse_func_ret_count < MAX_PARSE_FUNC_RET) {
                    StructType *ret_st = NULL;
                    if (ret_struct_tag && *ret_struct_tag)
                        ret_st = find_struct_tag(ret_struct_tag);
                    if (ret_st) {
                        parse_func_ret_name[parse_func_ret_count] = fname;
                        parse_func_ret_type[parse_func_ret_count] = ret_st;
                        parse_func_ret_count++;
                    }
                }
                *tail = func;
                tail = &func->next;
            }
        } else {
            /* 全局变量声明（支持逗号分隔多变量：int a, b, c;） */
            while (1) {
                int gv_ptrs = 0;
                while (peek(p).kind == TOK_STAR) { consume(p); gv_ptrs++; }
                if (peek(p).kind == TOK_IDENT) {
                    Token gv_name = consume(p);
                    /* 处理数组后缀 [N][M]...（多层） */
                    int gv_arr_len = 1;
                    int gv_bracket_count = 0;
                    int gv_unspecified_dim = 0;
                    int gv_first_dim = 0;  /* [] 空维度标记 */
                    while (peek(p).kind == TOK_LBRACKET) {
                        gv_bracket_count++;
                        consume(p);
                        if (peek(p).kind == TOK_RBRACKET) {
                            /* 空维度 char buf[] — 标记为未指定，后续由初始化器决定 */
                            gv_unspecified_dim = 1;
                        } else {
                            /* 维度表达式：char buf[4], buf[4*1024], buf[(4)] */
                            AstNode *dim_expr = parse_expr(p);
                            if (dim_expr) {
                                long long dim_val = eval_const_expr(dim_expr);
                                if (dim_val > 0) {
                                    if (gv_first_dim == 0) gv_first_dim = (int)dim_val;
                                    gv_arr_len *= (int)dim_val;
                                }
                            }
                            /* 跳过到匹配的 ] */
                            int d = 1;
                            while (d > 0 && peek(p).kind != TOK_EOF) {
                                if (peek(p).kind == TOK_LBRACKET) d++;
                                if (peek(p).kind == TOK_RBRACKET) d--;
                                if (d) consume(p);
                            }
                        }
                        if (peek(p).kind == TOK_RBRACKET) consume(p);
                    }
                    /* 计算总大小 */
                    int gv_unit = gv_ptrs > 0 ? 8 : (typesize > 0 ? typesize : 4);
                    int gv_total = gv_arr_len > 1 ? gv_unit * gv_arr_len : gv_unit;
                    int gv_is_array = (gv_bracket_count > 0);
                    int gv_elem_at = gv_unit;
                    if (gv_is_array && gv_bracket_count > 1) {
                        if (gv_first_dim > 0)
                            gv_elem_at = gv_unit * (gv_arr_len / gv_first_dim);
                        else
                            gv_elem_at = gv_unit * gv_arr_len;
                    }
                    /* 注册 struct 标签和大小（供 sizeof 查找） */
                    {
                        const char *gvn = arena_strdup(p->arena, gv_name.start, gv_name.len);
                        int gv_is_float = (gv_ptrs == 0) ? last_type_is_float : 0;
                        pvar_add_ex(gvn, global_typedef_tag ? global_typedef_tag : (last_struct_tag ? last_struct_tag : ""), gv_is_float, last_type_is_unsigned, gv_total);
                        {
                            const char *gv_tag = global_typedef_tag ? global_typedef_tag : last_struct_tag;
                            if (gv_tag)
                                pvar_set_struct_type(gvn, resolve_struct_type(gv_tag));
                        }
                        if (gv_is_array)
                            pvar_set_elem_size(gvn, gv_elem_at);
                        if (gv_ptrs > 0 && last_type_is_float)
                            pvar_set_elem_float(gvn, last_type_is_float);
                        if (pvar_count > 3800) {
                            int _na = 0; while (gvn[_na]) _na++;
                            __write(2, "PVAR_FULL:", 10);
                            __write(2, gvn, _na);
                            __write(2, " cnt=", 5);
                            { char _bb[8]; int _nv=pvar_count; int _ii=0; do{_bb[_ii++]='0'+_nv%10;_nv/=10;}while(_nv>0);int _jj;for(_jj=0;_jj<_ii/2;_jj++){char _t=_bb[_jj];_bb[_jj]=_bb[_ii-1-_jj];_bb[_ii-1-_jj]=_t;}_bb[_ii]='\n';__write(2,_bb,_ii+1); }
                        }
                    }
                    /* extern 声明不产生定义，不创建 AST 节点 */
                    AstNode *gvar = NULL;
                    if (!current_extern) {
                        gvar = new_ast(p, AST_VAR_DECL);
                        gvar->name = arena_strdup(p->arena, gv_name.start, gv_name.len);
                        gvar->is_static = current_static;
                        gvar->ival = gv_total;
                        gvar->type_size = gv_total;
                        gvar->elem_size = (gv_arr_len > 1) ? gv_elem_at : (gv_ptrs > 0 ? (gv_ptrs > 1 ? 8 : (typesize > 0 ? typesize : 4)) : 0);
                        gvar->base_elem_size = (gv_ptrs == 1 && gv_bracket_count > 0)
                            ? (typesize > 0 ? typesize : 4) : gv_unit;
                        gvar->elem_is_ptr = (gv_ptrs > 0 && gv_bracket_count > 0) ? 1 : 0;
                        gvar->is_array = (gv_bracket_count > 0) ? 1 : 0;  /* int arr[1] 也能正确标记 */
                        gvar->elem_is_unsigned = (gv_ptrs > 0 || gv_bracket_count > 0) ? last_type_is_unsigned : 0;
                        gvar->is_float = (gv_ptrs == 0) ? last_type_is_float : 0;
                        gvar->elem_is_float = (gv_ptrs > 0) ? last_type_is_float : 0;
                        /* 设置 struct_type（供 cgen 按成员实际大小发射初始化数据） */
                        {
                            const char *gv_tag = global_typedef_tag ? global_typedef_tag :
                                (last_struct_tag && last_struct_member_count > 0 ? last_struct_tag : NULL);
                            if (gv_tag)
                                gvar->struct_type = resolve_struct_type(gv_tag);
                        }
                        *tail = gvar;
                        tail = &gvar->next;
                    }
                    /* 保存初始化器 */
                    if (match(p, TOK_EQ)) {
                        if (peek(p).kind == TOK_LBRACE) {
                            /* 先计数顶层元素，用于推断未指定数组维数 */
                            int init_count = 0;
                            {
                                const char *save_pos = p->lexer->pos;
                                int save_line = p->lexer->line;
                                int save_col = p->lexer->col;
                                Token save_tok = p->tok;

                                int brace_depth = 1;
                                int paren_depth = 0;
                                consume(p);  /* skip { */
                                while (brace_depth > 0 && peek(p).kind != TOK_EOF) {
                                    TokenKind tk = peek(p).kind;
                                    if (tk == TOK_LPAREN) { paren_depth++; consume(p); }
                                    else if (tk == TOK_RPAREN) { if (paren_depth > 0) paren_depth--; consume(p); }
                                    else if (tk == TOK_LBRACE) { brace_depth++; consume(p); }
                                    else if (tk == TOK_RBRACE) { brace_depth--; if (brace_depth > 0) consume(p); }
                                    else if (tk == TOK_COMMA && brace_depth == 1 && paren_depth == 0) { init_count++; consume(p); }
                                    else { consume(p); }
                                }
                                if (peek(p).kind == TOK_RBRACE) consume(p);
                                init_count++;  /* +1 for last element */

                                p->lexer->pos = save_pos;
                                p->lexer->line = save_line;
                                p->lexer->col = save_col;
                                p->tok = save_tok;
                            }
                            /* 如果数组维数未指定（[]），用计数修正 */
                            if (gv_unspecified_dim && init_count > 0) {
                                gv_arr_len *= init_count;
                                gv_total = gv_unit * gv_arr_len;
                                if (gvar) {
                                    gvar->ival = gv_total;
                                    gvar->type_size = gv_total;
                                    gvar->elem_size = gv_unit;
                                    gvar->is_array = 1;
                                }
                                /* 更新 pvar 表（pvar_add_ex 在计数前已调用，当时 size=16） */
                                if (gvar && gvar->name) pvar_update_size(gvar->name, gv_total);
                            }
                            /* 解析初始化器值，存储到 gvar->init_items[] */
                            if (gvar) {
                                int max_est;
                                if (gv_arr_len > 0) {
                                    /* 结构体数组：每个元素产生 member_count 个 InitItem */
                                    int member_est = 1;
                                    if (gvar->struct_type && gvar->struct_type->member_count > 0)
                                        member_est = gvar->struct_type->member_count;
                                    max_est = gv_arr_len * member_est;
                                    if (max_est < 64) max_est = 64;
                                } else if (init_count > 0) {
                                    max_est = init_count * 8;
                                } else {
                                    max_est = 64;
                                }
                                if (max_est > 65536) max_est = 65536;
                                InitItem *items = arena_alloc(p->arena, max_est * sizeof(InitItem));
                                int elem_count = 0;
                                int actual = parse_init_list(p, items, max_est, &elem_count);
                                if (actual > max_est) actual = max_est;
                                /* 用实际解析的元素数修正 gv_total（应对尾随逗号） */
                                if (actual > 0 && gv_unit * actual != gv_total) {
                                    /* 多维数组（bracket_count>1）的 elem_count 是行数，
                                     * 用 actual（总标量项数）计算总大小。
                                     * 结构体/1D 数组的 elem_count 是正确元素数。 */
                                    if (gv_bracket_count > 1) {
                                        gv_total = gv_unit * actual;
                                    } else {
                                        gv_total = gv_unit * elem_count;
                                    }
                                    gvar->ival = gv_total;
                                    gvar->type_size = gv_total;
                                    if (gvar->name) pvar_update_size(gvar->name, gv_total);
                                }
                                gvar->init_items = items;
                                gvar->init_count = actual;
                                /* 计算每个数组元素的标量项目数（直接解析得到，不受尾随逗号影响） */
                                if (elem_count > 0 && actual >= elem_count) {
                                    gvar->init_items_per_elem = actual / elem_count;
                                } else {
                                    gvar->init_items_per_elem = actual;
                                }
                                gvar->expr = new_ast(p, AST_CONSTANT);
                                gvar->expr->ival = 0;
                            } else {
                                /* 无 gvar，仅跳过 */
                                int d = 1; consume(p);
                                while (d > 0 && peek(p).kind != TOK_EOF) {
                                    if (peek(p).kind == TOK_LBRACE) d++;
                                    if (peek(p).kind == TOK_RBRACE) d--;
                                    if (d) consume(p);
                                }
                                if (peek(p).kind == TOK_RBRACE) consume(p);
                            }
                        } else if (gvar) {
                            gvar->expr = parse_expr(p);
                        } else {
                            parse_expr(p);
                        }
                    }
                } else {
                    error_at(p, "expected identifier in global declaration");
                }
                if (peek(p).kind == TOK_COMMA) consume(p); else break;
            }
            expect(p, TOK_SEMI);
            current_static = 0;
            current_extern = 0;
        }
    }
    AstNode *prog = new_ast(p, AST_PROGRAM);
    prog->body = head;
    return prog;
}
