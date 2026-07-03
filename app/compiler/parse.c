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

#include "tcc.h"

/* 解析 struct 类型时捕获的信息（供变量声明和成员访问追踪） */
static const char *last_struct_tag = NULL;
static Member last_struct_members[MAX_MEMBERS];
static int last_struct_member_count = 0;

/* ─── 局部变量类型追踪（解析阶段用，供 .m 成员访问计算偏移） ─── */

#define MAX_PVARS 4096
static const char *pvar_name[MAX_PVARS];   /* 变量名 */
static const char *pvar_tag[MAX_PVARS];    /* struct 标签 */
static int pvar_is_float_arr[MAX_PVARS];  /* 是否为 double 类型 */
static int pvar_size_arr[MAX_PVARS];      /* 变量大小（用于 sizeof） */
static int pvar_count;

static void pvar_add_ex(const char *name, const char *tag, int is_float, int size) {
    if (pvar_count < MAX_PVARS && name && *name) {
        pvar_name[pvar_count] = name;
        pvar_tag[pvar_count] = tag;
        pvar_is_float_arr[pvar_count] = is_float;
        pvar_size_arr[pvar_count] = size;
        pvar_count++;
    }
}
#define pvar_add(name, tag, is_float) pvar_add_ex(name, tag, is_float, 0)
static const char *pvar_find_tag(const char *name) {
    int i;
    for (i = 0; i < pvar_count; i++)
        if (strcmp(pvar_name[i], name) == 0) return pvar_tag[i];
    return NULL;
}
static int pvar_find_size(const char *name) {
    int i;
    for (i = 0; i < pvar_count; i++) {
        int match = strcmp(pvar_name[i], name);
        if (match == 0) return pvar_size_arr[i];
    }
    return 0;
}
static int pvar_find_float(const char *name) {
    int i;
    for (i = 0; i < pvar_count; i++)
        if (strcmp(pvar_name[i], name) == 0) return pvar_is_float_arr[i];
    return 0;
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

/* ─── 类型系统全局表 ─── */

StructType tag_table[MAX_TAGS];
int tag_count;

TypedefEntry typedef_table[MAX_TYPEDEFS];
int typedef_count;

EnumEntry enum_vals[MAX_ENUM_VALS];
int enum_val_count;

void register_enum_val(const char *name, int value) {
    if (enum_val_count < MAX_ENUM_VALS) {
        enum_vals[enum_val_count].name = name;
        enum_vals[enum_val_count].value = value;
        enum_val_count++;
    }
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
    if (tag_count >= MAX_TAGS) return -1;
    StructType *s = &tag_table[tag_count++];
    s->tag = tag;
    int i;
    for (i = 0; i < st->member_count; i++)
        s->members[i] = st->members[i];
    s->member_count = st->member_count;
    s->total_size = st->total_size;
    return tag_count - 1;
}

/* ─── 错误报告 ─── */

void error_at(Parser *p, const char *msg) {
    __printf("error [line %d tok=%d]: %s\n", p->lexer->line, p->tok.kind, msg);
    /* Debug: print surrounding context */
    if (p->tok.start) {
        int ctx = 0;
        while (ctx < 40 && p->tok.start + ctx < p->lexer->end) {
            char c = p->tok.start[ctx];
            if (c == '\n') { __printf("\\n"); break; }
            if (c >= 32) __printf("%c", c);
            else __printf("\\x%02x", (unsigned char)c);
            ctx++;
        }
        __printf("\n");
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
    error_at(p, "unexpected token");
    return 0;
}

/* ─── 初始化 ─── */

void parser_init(Parser *p, Lexer *lx, Arena *a) {
    p->lexer = lx;
    p->arena = a;
    p->had_error = 0;
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
    n->is_float = 0;
    n->is_static = 0;
    n->is_variadic = 0;
    n->ival = 0;
    n->dval = 0.0;
    n->op = 0;
    n->base_elem_size = 0;
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
        n->ival = t.ival;
        n->is_float = t.is_float;
        if (t.is_float) n->dval = t.dval;
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
                return n;
            }
        }
        AstNode *n = new_ast(p, AST_VAR);
        n->name = arena_strdup(p->arena, t.start, t.len);
        n->is_float = pvar_find_float(n->name);
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
                    case '0': dst[pos++] = '\0'; break;
                    case '\\': dst[pos++] = '\\'; break;
                    case '"': dst[pos++] = '"'; break;
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
            call->args = NULL;
            consume(p);
            AstNode **tail = &call->args;
            while (peek(p).kind != TOK_RPAREN && peek(p).kind != TOK_EOF) {
                /* 先检查是否类型关键字（__builtin_va_arg 的类型参数） */
                if (call->name && (peek(p).kind == TOK_INT || peek(p).kind == TOK_CHAR ||
                    peek(p).kind == TOK_SHORT || peek(p).kind == TOK_LONG ||
                    peek(p).kind == TOK_VOID || peek(p).kind == TOK_DOUBLE ||
                    peek(p).kind == TOK_UNSIGNED || peek(p).kind == TOK_SIGNED ||
                    peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE)) {
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
                    }
                } else {
                    *tail = parse_expr(p);
                }
                /* 回退路径：parse_expr 返回 NULL 时尝试类型 */
                if (*tail == NULL && call->name) {
                    int tsz = parse_type_specifier(p);
                    if (tsz > 0) {
                        *tail = new_ast(p, AST_CONSTANT);
                        (*tail)->ival = tsz;
                    }
                }
                if (*tail) {
                    tail = &(*tail)->next;
                }
                if (peek(p).kind == TOK_COMMA) consume(p);
                else break;
            }
            expect(p, TOK_RPAREN);
            /* 启发式：若任一实参为 float，则假定函数返回 double */
            {
                AstNode *a;
                for (a = call->args; a; a = a->next)
                    if (a->is_float) { call->is_float = 1; break; }
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
            left = n;

        } else if (t.kind == TOK_DOT) {
            consume(p);
            Token m = consume(p);
            AstNode *n = new_ast(p, AST_MEMBER);
            n->left = left;
            n->member_name = arena_strdup(p->arena, m.start, m.len);
            n->op = TOK_DOT;
            /* 查找成员偏移：优先用 pvar 记录的 struct 标签 */
            const char *lookup_tag = NULL;
            if (left && left->kind == AST_VAR) {
                lookup_tag = pvar_find_tag(left->name);
            } else if (left && left->kind == AST_BINOP && left->op == TOK_LBRACKET &&
                       left->left && left->left->kind == AST_VAR) {
                /* a[i].member — 从数组变量查找 struct 标签 */
                lookup_tag = pvar_find_tag(left->left->name);
            }
            int struct_found = 0;
            if (lookup_tag) {
                StructType *st = find_struct_tag(lookup_tag);
                if (st) {
                    struct_found = 1;
                    int fi;
                    for (fi = 0; fi < st->member_count; fi++) {
                        if (strcmp(st->members[fi].name, n->member_name) == 0) {
                            n->ival = st->members[fi].offset;
                            n->type_size = st->members[fi].size;
                            break;
                        }
                    }
                } else {
                    /* 回退到 typedef 表（匿名 struct typedef 没有 tag 名） */
                    int ti;
                    for (ti = 0; ti < typedef_count; ti++) {
                        if (strcmp(typedef_table[ti].name, lookup_tag) == 0 && typedef_table[ti].member_count > 0) {
                            struct_found = 1;
                            int mi;
                            for (mi = 0; mi < typedef_table[ti].member_count; mi++) {
                                if (strcmp(typedef_table[ti].members[mi].name, n->member_name) == 0) {
                                    n->ival = typedef_table[ti].members[mi].offset;
                                    n->type_size = typedef_table[ti].members[mi].size;
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
            }
            /* 回退：仅当未确认 struct 类型时遍历所有 struct tag */
            if (!struct_found && n->ival == 0) {
                int ti, mi;
                for (ti = 0; ti < tag_count && n->ival == 0; ti++) {
                    for (mi = 0; mi < tag_table[ti].member_count; mi++) {
                        if (strcmp(tag_table[ti].members[mi].name, n->member_name) == 0) {
                            n->ival = tag_table[ti].members[mi].offset;
                            n->type_size = tag_table[ti].members[mi].size;
                            break;
                        }
                    }
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
            /* 通过指针的 struct 标签查找成员偏移 */
            int struct_found = 0;
            if (left && left->kind == AST_VAR) {
                const char *tag = pvar_find_tag(left->name);
                if (tag) {
                    StructType *st = find_struct_tag(tag);
                    if (st) {
                        struct_found = 1;
                        int fi;
                        for (fi = 0; fi < st->member_count; fi++) {
                            if (strcmp(st->members[fi].name, n->member_name) == 0) {
                                n->ival = st->members[fi].offset;
                                n->type_size = st->members[fi].size;
                                break;
                            }
                        }
                    } else {
                        /* 回退到 typedef 表（匿名 struct typedef 没有 tag 名） */
                        int ti;
                        for (ti = 0; ti < typedef_count; ti++) {
                            if (strcmp(typedef_table[ti].name, tag) == 0 && typedef_table[ti].member_count > 0) {
                                struct_found = 1;
                                int mi;
                                for (mi = 0; mi < typedef_table[ti].member_count; mi++) {
                                    if (strcmp(typedef_table[ti].members[mi].name, n->member_name) == 0) {
                                        n->ival = typedef_table[ti].members[mi].offset;
                                        n->type_size = typedef_table[ti].members[mi].size;
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
            /* 回退：仅当未确认 struct 类型时遍历所有 struct */
            if (!struct_found) {
                if (n->ival == 0) {
                    int ti, mi;
                    for (ti = 0; ti < typedef_count && n->ival == 0; ti++) {
                        if (typedef_table[ti].member_count > 0) {
                            for (mi = 0; mi < typedef_table[ti].member_count; mi++) {
                                if (strcmp(typedef_table[ti].members[mi].name, n->member_name) == 0) {
                                    n->ival = typedef_table[ti].members[mi].offset;
                                    n->type_size = typedef_table[ti].members[mi].size;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (n->ival == 0) {
                    int ti, mi;
                    for (ti = 0; ti < tag_count && n->ival == 0; ti++) {
                        for (mi = 0; mi < tag_table[ti].member_count; mi++) {
                            if (strcmp(tag_table[ti].members[mi].name, n->member_name) == 0) {
                                n->ival = tag_table[ti].members[mi].offset;
                                n->type_size = tag_table[ti].members[mi].size;
                                break;
                            }
                        }
                    }
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
        /* +/- 和 ++/-- 保留操作数的浮点类型 */
        if ((t.kind == TOK_PLUS || t.kind == TOK_MINUS ||
             t.kind == TOK_PLUS_PLUS || t.kind == TOK_MINUS_MINUS) &&
            n->expr && n->expr->is_float)
            n->is_float = 1;
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
                if (vsize > 0 && vsize != 8)
                    n->ival = vsize;
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
        int csz = parse_type_specifier(p);
        if (csz >= 0) {
            /* 跳过星号和更多限定符（处理 char *, const unsigned char * 等） */
            while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
                   peek(p).kind == TOK_RESTRICT) consume(p);
            while (peek(p).kind == TOK_STAR) {
                consume(p);
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
                if (inner && cast_to_double) inner->is_float = 1;
                return inner;  /* 跳过转换，返回内部表达式 */
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
        /* 浮点传播：* 和 / 保留浮点类型 */
        if (op.kind != TOK_PERCENT &&
            ((left && left->is_float) || (right && right->is_float)))
            n->is_float = 1;
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
        if ((left && left->is_float) || (right && right->is_float))
            n->is_float = 1;
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
            (else_expr && else_expr->is_float))
            n->is_float = 1;
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
        if (right && right->is_float) n->is_float = 1;
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
        if (right && right->is_float) bin->is_float = 1;
        n->right = bin;
        if (bin->is_float) n->is_float = 1;
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
        if (right && right->is_float) cn->is_float = 1;
        n = cn;
    }
    return n;
}

/* ─── 解析 struct 体（返回成员列表和总大小） ─── */

static int parse_struct_body(Parser *p, Member *members, int *out_count) {
    expect(p, TOK_LBRACE);
    int count = 0;
    int offset = 0;

    while (peek(p).kind != TOK_RBRACE && peek(p).kind != TOK_EOF) {
        /* 跳过限定符 */
        while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
               peek(p).kind == TOK_RESTRICT) consume(p);
        int sz = parse_type_specifier(p);
        if (sz < 0) { error_at(p, "invalid struct member type"); break; }

        /* 指针 */
        while (peek(p).kind == TOK_STAR) { consume(p); sz = 8; }

        /* 函数指针 (*name)(params) */
        if (peek(p).kind == TOK_LPAREN) {
            consume(p);
            if (peek(p).kind == TOK_STAR) {
                consume(p); sz = 8;
            } else {
                /* 不是 (*name)，无法处理 */
            }
        }

        Token id = peek(p);
        if (id.kind == TOK_IDENT) {
            consume(p);
            if (count < MAX_MEMBERS) {
                /* 按类型自然对齐（指针/long long/double=8 字节对齐） */
                int member_align = (sz >= 8) ? 8 : (sz >= 4) ? 4 : (sz >= 2) ? 2 : 1;
                offset = (offset + member_align - 1) & ~(member_align - 1);
                members[count].name = arena_strdup(p->arena, id.start, id.len);
                members[count].offset = offset;
                members[count].size = sz;
                count++;
                offset += sz;
            }
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
        /* 跳过数组 [...] */
        if (peek(p).kind == TOK_LBRACKET) { consume(p);
            int d = 1;
            while (d > 0 && peek(p).kind != TOK_EOF) {
                if (peek(p).kind == TOK_LBRACKET) d++;
                if (peek(p).kind == TOK_RBRACKET) d--;
                if (d) consume(p);
            }
            if (peek(p).kind == TOK_RBRACKET) consume(p);
        }

        /* 逗号分隔的成员：int a, b, c; */
        while (peek(p).kind == TOK_COMMA) {
            consume(p);
            while (peek(p).kind == TOK_STAR) { consume(p); }
            Token cid = peek(p);
            if (cid.kind == TOK_IDENT) {
                consume(p);
                if (count < MAX_MEMBERS) {
                    int member_align = (sz >= 8) ? 8 : (sz >= 4) ? 4 : (sz >= 2) ? 2 : 1;
                    offset = (offset + member_align - 1) & ~(member_align - 1);
                    members[count].name = arena_strdup(p->arena, cid.start, cid.len);
                    members[count].offset = offset;
                    members[count].size = sz;
                    count++;
                    offset += sz;
                }
            }
            /* 跳过数组 [...] 在逗号后成员上 */
            if (peek(p).kind == TOK_LBRACKET) { consume(p);
                int d2 = 1;
                while (d2 > 0 && peek(p).kind != TOK_EOF) {
                    if (peek(p).kind == TOK_LBRACKET) d2++;
                    if (peek(p).kind == TOK_RBRACKET) d2--;
                    if (d2) consume(p);
                }
                if (peek(p).kind == TOK_RBRACKET) consume(p);
            }
        }

        expect(p, TOK_SEMI);
    }
    expect(p, TOK_RBRACE);

    /* 对齐到 8 字节（支持指针/long long/double 等 8 字节类型） */
    offset = (offset + 7) & ~7;

    *out_count = count;
    return offset;
}

/* 解析 struct 类型：struct [tag] { ... }  或 struct tag
 * 返回 0 表示失败或 forward-declaration */
static int parse_struct_type(Parser *p, StructType *out) {
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
        /* struct tag { ... } */
        out->total_size = parse_struct_body(p, out->members, &out->member_count);
        while (peek(p).kind == TOK__ATTRIBUTE__) {
            consume(p); expect(p, TOK_LPAREN); expect(p, TOK_LPAREN);
            int d = 2;
            while (d > 0 && peek(p).kind != TOK_EOF) {
                if (peek(p).kind == TOK_LPAREN) d++;
                if (peek(p).kind == TOK_RPAREN) d--;
                consume(p);
            }
        }
        out->tag = tag;
        /* 保存到 last_struct_* 供变量声明和成员访问使用 */
        last_struct_tag = tag;
        last_struct_member_count = out->member_count;
        int mi;
        for (mi = 0; mi < out->member_count && mi < MAX_MEMBERS; mi++)
            last_struct_members[mi] = out->members[mi];

        /* 始终注册到 tag_table（匿名 struct 用空字符串标记） */
        {
            const char *reg_tag = tag ? tag : "";
            StructType *existing = find_struct_tag(reg_tag);
            if (!existing) add_struct_tag(reg_tag, out);
        }
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

int parse_type_specifier(Parser *p) {
    Token t = peek(p);
    /* 检查 typedef 名（先提取词素，t.start 不是 null 终止的） */
    if (t.kind == TOK_IDENT) {
        char tname[128];
        int nl = t.len < 127 ? t.len : 127;
        int ci; for (ci = 0; ci < nl; ci++) tname[ci] = t.start[ci]; tname[nl] = '\0';
        int ti;
        for (ti = 0; ti < typedef_count; ti++) {
            if (strcmp(typedef_table[ti].name, tname) == 0) {
                consume(p);
                return typedef_table[ti].size;
            }
        }
    }
    switch (t.kind) {
    case TOK_INT:      consume(p); return 4;
    case TOK_CHAR:     consume(p); return 1;
    case TOK_SHORT:    consume(p); return 2;
    case TOK_DOUBLE:   consume(p); return 8;
    case TOK__BUILTIN_VA_LIST: consume(p); return 24;  /* va_list = 4+4+8+8 bytes */
    case TOK_LONG:
        consume(p);
        if (peek(p).kind == TOK_LONG) { consume(p); if (peek(p).kind == TOK_INT) { consume(p); } return 8; }
        if (peek(p).kind == TOK_INT) { consume(p); } return 8;
    case TOK_STRUCT:
    case TOK_UNION: {
        consume(p);
        StructType st;
        int sz = parse_struct_type(p, &st);
        return sz > 0 ? sz : 4;
    }
    case TOK_ENUM: {
        consume(p);
        if (peek(p).kind == TOK_IDENT) consume(p);  /* 可选的标签名 */
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
        return 4;  /* enum 大小 = int */
    }
    case TOK_VOID:     consume(p); return 0;
    case TOK_UNSIGNED:
        consume(p);
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
            while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
                   peek(p).kind == TOK_RESTRICT) consume(p);
            Token nt = peek(p);
            const char *name = "";
            if (nt.kind == TOK_IDENT) { consume(p); name = arena_strdup(p->arena, nt.start, nt.len); }
            /* 跳过数组后缀 [...]（函数指针数组）*/
            while (peek(p).kind == TOK_LBRACKET) {
                int d = 1; consume(p);
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
    consume(p);
    AstNode *n = new_ast(p, AST_RETURN);
    if (peek(p).kind != TOK_SEMI)
        n->expr = parse_expr_comma(p);
    expect(p, TOK_SEMI);
    return n;
}

static AstNode *parse_if_statement(Parser *p) {
    consume(p);  /* if */
    expect(p, TOK_LPAREN);
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
    expect(p, TOK_LPAREN);
    AstNode *n = new_ast(p, AST_WHILE);
    n->loop_cond = parse_expr(p);
    expect(p, TOK_RPAREN);
    n->loop_body = parse_statement(p);
    return n;
}

static AstNode *parse_for_statement(Parser *p) {
    consume(p);  /* for */
    expect(p, TOK_LPAREN);
    AstNode *n = new_ast(p, AST_FOR);

    /* init */
    if (peek(p).kind != TOK_SEMI) {
        /* 跳过限定符：for (const char *p = ...) */
        while (peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
               peek(p).kind == TOK_RESTRICT) consume(p);
        int loop_is_double = (peek(p).kind == TOK_DOUBLE);
        int ts = parse_type_specifier(p);
        if (ts >= 0) {
            int loop_ptrs = 0;
            n->loop_init = new_ast(p, AST_VAR_DECL);
            n->loop_init->name = parse_declarator(p, &loop_ptrs);
            n->loop_init->ival = loop_ptrs > 0 ? 8 : (ts > 0 ? ts : 4);
            n->loop_init->type_size = n->loop_init->ival;
            n->loop_init->is_float = (loop_is_double && loop_ptrs == 0);
            if (n->loop_init->name && *n->loop_init->name)
                pvar_add(n->loop_init->name, NULL, loop_is_double);
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

    n->loop_body = parse_statement(p);
    return n;
}

static AstNode *parse_do_while(Parser *p) {
    consume(p);  /* do */
    AstNode *n = new_ast(p, AST_DO_WHILE);
    n->loop_body = parse_statement(p);
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
    expect(p, TOK_LPAREN);
    AstNode *n = new_ast(p, AST_SWITCH);
    n->cond = parse_expr(p);
    expect(p, TOK_RPAREN);
    /* switch 体使用复合语句解析器，由其处理 case/default 标签 */
    n->stmts = NULL;
    if (peek(p).kind == TOK_LBRACE) {
        AstNode *block = parse_compound_statement(p);
        if (block && block->stmts)
            n->stmts = block->stmts;
    }
    return n;
}

static AstNode *parse_break(Parser *p) {
    consume(p);
    expect(p, TOK_SEMI);
    return new_ast(p, AST_BREAK);
}

static AstNode *parse_continue(Parser *p) {
    consume(p);
    expect(p, TOK_SEMI);
    return new_ast(p, AST_CONTINUE);
}

/* ─── 复合语句 ─── */

AstNode *parse_compound_statement(Parser *p) {
    expect(p, TOK_LBRACE);

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
                if (tname && *tname && typedef_count < MAX_TYPEDEFS) {
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
                        if (last_struct_member_count > 0) {
                            te->member_count = last_struct_member_count;
                            int mi;
                            for (mi = 0; mi < last_struct_member_count && mi < MAX_MEMBERS; mi++)
                                te->members[mi] = last_struct_members[mi];
                            /* reg anonymous struct to tag_table for member lookup */
                            if (tag_count < MAX_TAGS) {
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
                ts = parse_type_specifier(p);
            }
            if (ts >= 0) {
                AstNode *decl = new_ast(p, AST_VAR_DECL);
                int dv_ptrs = 0;
                decl->name = parse_declarator(p, &dv_ptrs);
                decl->ival = dv_ptrs > 0 ? 8 : (ts > 0 ? ts : 4);
                decl->type_size = decl->ival;
                decl->elem_size = (dv_ptrs > 1) ? 8 : (dv_ptrs == 1 ? ts : 0);
                /* 检查类型本身是否是指针 typedef（如 typedef char* string; string s;） */
                if (dv_ptrs == 0 && ts > 0 && ptr_typedef_pts > 0) {
                    decl->elem_size = ptr_typedef_pts; }
                decl->is_float = (decl_is_double && dv_ptrs == 0);
                decl->is_static = q_static;
                if (decl->name && *decl->name) {
                    if (decl_typedef_tag) {
                        pvar_add_ex(decl->name, decl_typedef_tag, decl->is_float, decl->ival);
                    } else if (last_struct_tag || last_struct_member_count > 0) {
                        pvar_add_ex(decl->name, last_struct_tag ? last_struct_tag : "",
                                 decl->is_float, decl->ival);
                    } else {
                        int ti;
                        int found_typedef = 0;
                        for (ti = 0; ti < typedef_count; ti++) {
                            if (typedef_table[ti].member_count > 0 && ts == typedef_table[ti].size) {
                                pvar_add_ex(decl->name, typedef_table[ti].name,
                                         decl->is_float, decl->ival);
                                found_typedef = 1;
                                break;
                            }
                        }
                        if (!found_typedef)
                            pvar_add_ex(decl->name, NULL, decl->is_float, decl->ival);
                    }
                }
                last_struct_tag = NULL;
                /* 处理数组后缀 [N]（将数组维数乘入 ival；表达式维数跳过处理） */
                int first_dim = 0;
                int dim_count = 0;
                int bracket_count = 0;
                while (peek(p).kind == TOK_LBRACKET) {
                    bracket_count++;
                    consume(p);
                    if (peek(p).kind == TOK_NUMBER && peek(p).ival > 0) {
                        if (dim_count == 0) first_dim = peek(p).ival;
                        decl->ival *= peek(p).ival;
                        consume(p);
                        dim_count++;
                    }
                    /* 跳过到匹配的 ]（处理表达式维数如 4*1024 或 MAX*1024） */
                    int d = 1;
                    while (d > 0 && peek(p).kind != TOK_EOF) {
                        if (peek(p).kind == TOK_LBRACKET) d++;
                        if (peek(p).kind == TOK_RBRACKET) d--;
                        if (d) consume(p);
                    }
                    if (peek(p).kind == TOK_RBRACKET) consume(p);
                }
                if (dim_count > 0 && first_dim > 0) {
                    /* 数组：elem_size = 元素/行大小，base_elem_size = 基础元素类型 */
                    int elem_ts = (dv_ptrs > 0) ? 8 : (ts > 0 ? ts : 4);
                    decl->base_elem_size = elem_ts;
                    if (dim_count > 1)
                        decl->elem_size = decl->ival / first_dim; /* row size */
                    else
                        decl->elem_size = elem_ts;
                } else {
                    if (dv_ptrs > 0 && bracket_count > 0) {
                        decl->elem_size = 8;
                        decl->base_elem_size = ts > 0 ? ts : 4;
                    } else {
                        decl->base_elem_size = decl->elem_size;
                    }
                }
                /* 数组处理后更新 pvar 中的变量大小 */
                if (decl->name && *decl->name && decl->ival > 4) {
                    int pi;
                    for (pi = 0; pi < pvar_count; pi++)
                        if (strcmp(pvar_name[pi], decl->name) == 0)
                            { pvar_size_arr[pi] = decl->ival; break; }
                }
                /* 逗号分隔的多变量声明（含初始化）：int a = 1, b = 2, c; */
                AstNode *chain_tail = decl;
                /* 第一个变量的初始化 */
                if (match(p, TOK_EQ)) {
                    if (peek(p).kind == TOK_LBRACE) {
                        /* { expr1, expr2, ... } — 简单数组初始化 */
                        consume(p);
                        AstNode *prev_init = NULL;
                        int init_idx = 0;
                        while (peek(p).kind != TOK_RBRACE && peek(p).kind != TOK_EOF) {
                            const char *ipos = p->lexer->pos;  /* 防死循环：不支持 .member 指派初始化器 */
                            AstNode *ie = parse_expr(p);
                            if (!ie && p->lexer->pos == ipos) { consume(p); continue; }
                            if (ie && decl->name) {
                                /* 构建 a[i] = expr 赋值节点 */
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
                                assign->type_size = 4;
                                if (prev_init) prev_init->next = assign;
                                else decl->expr = assign;
                                prev_init = assign;
                            }
                            init_idx++;
                            if (peek(p).kind == TOK_COMMA) consume(p);
                        }
                        if (peek(p).kind == TOK_RBRACE) consume(p);
                    } else {
                        decl->expr = parse_expr(p);
                        /* char buf[] = "str" — 用字符串长度修正数组类型大小 */
                        if (ts == 1 && bracket_count > 0 && dim_count == 0 &&
                            decl->expr && decl->expr->kind == AST_STRING) {
                            int slen = 0;
                            while (decl->expr->str_val[slen]) slen++;
                            slen++; /* 包含 null 终止符 */
                            decl->ival = slen;
                            decl->type_size = slen;
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
                    cdecl->is_float = (decl_is_double && c_ptrs == 0);
                    cdecl->is_static = q_static;
                    /* 注册局部变量名 */
                    if (cname && *cname)
                        pvar_add_ex(cname, NULL, cdecl->is_float, cdecl->ival);
                    /* 处理数组后缀 */
                    while (peek(p).kind == TOK_LBRACKET) {
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
        consume(p);
        { AstNode *n = new_ast(p, AST_CASE);
          AstNode *val = parse_expr(p);
          n->ival = (val && val->kind == AST_CONSTANT) ? val->ival : 0;
          expect(p, TOK_COLON); return n; }
    case TOK_DEFAULT:
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
        
        consume(p);
        if (peek(p).kind == TOK_VOLATILE) consume(p);
        expect(p, TOK_LPAREN);
        AstNode *n = new_ast(p, AST_ASM);
        n->asm_template = NULL;
        if (peek(p).kind == TOK_STRING) {
            Token s = consume(p);
            /* 复制模板字符串（去掉引号） */
            int slen = s.len - 2;
            if (slen > 0) {
                char *buf = arena_alloc(p->arena, slen + 1);
                int ci;
                for (ci = 0; ci < slen; ci++) buf[ci] = s.start[ci + 1];
                buf[slen] = '\0';
                n->asm_template = buf;
            }
        }
        /* 跳过 :输出 :输入 :破坏列表 */
        while (peek(p).kind != TOK_RPAREN && peek(p).kind != TOK_EOF) {
            if (peek(p).kind == TOK_COLON) { consume(p); continue; }
            if (peek(p).kind == TOK_STRING) { consume(p); continue; }
            if (peek(p).kind == TOK_LPAREN) {
                int depth = 1;
                consume(p);
                while (depth > 0 && peek(p).kind != TOK_EOF) {
                    if (peek(p).kind == TOK_LPAREN) depth++;
                    if (peek(p).kind == TOK_RPAREN) depth--;
                    if (depth) consume(p);
                }
                if (depth == 0) consume(p);
                continue;
            }
            /* 跳过标识符和数字 */
            consume(p);
        }
        expect(p, TOK_RPAREN);
        expect(p, TOK_SEMI);
        return n;
    }
    case TOK_LBRACE:  return parse_compound_statement(p);
    case TOK_SEMI:    consume(p); return new_ast(p, AST_NULL_STMT);
    default: {
        AstNode *expr = parse_expr_comma(p);
        expect(p, TOK_SEMI);
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
    if (peek(p).kind == TOK_VOID) {
        /* 仅 void 单独作为参数时（无名称）才消耗 */
        Lexer save_lx = *p->lexer;
        Token save_tok = p->tok;
        consume(p);
        if (peek(p).kind == TOK_RPAREN || peek(p).kind == TOK_COMMA) {
            /* void 单独：空参数列表 */
            expect(p, TOK_RPAREN);
            return head;
        }
        /* void name 或 void *name — 回退，当作正常类型处理 */
        *p->lexer = save_lx;
        p->tok = save_tok;
    } else if (peek(p).kind == TOK_RPAREN) {
        /* 空参数列表 */
        expect(p, TOK_RPAREN);
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
        int param_is_double = (peek(p).kind == TOK_DOUBLE);
        int psz = parse_type_specifier(p);
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
            pd->is_float = (param_is_double && ptr_level == 0);
            pvar_add(pname, NULL, pd->is_float);
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
            int tptr_level = 0;
            const char *tname = parse_declarator(p, &tptr_level);
            if (tname && *tname && typedef_count < MAX_TYPEDEFS) {
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
                    te->struct_idx = -1;
                    /* 对 struct typedef 保存成员信息 */
                    if (last_struct_member_count > 0) {
                        te->member_count = last_struct_member_count;
                        int mi;
                        for (mi = 0; mi < last_struct_member_count && mi < MAX_MEMBERS; mi++)
                            te->members[mi] = last_struct_members[mi];
                        /* 注册 typedef 名到 tag_table（匿名 struct 用 typedef 名做 tag） */
                        if (tag_count < MAX_TAGS && !find_struct_tag(tname)) {
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
            const char *fname = parse_declarator(p, NULL);
            int is_variadic_f = 0;
            AstNode *fparams = parse_parameter_list(p, &is_variadic_f);
            if (peek(p).kind == TOK_SEMI) {
                /* 函数原型：只声明不定义 */
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
                AstNode *fbody = parse_compound_statement(p);
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
                    while (peek(p).kind == TOK_LBRACKET) {
                        consume(p);
                        if (peek(p).kind == TOK_NUMBER) {
                            gv_arr_len *= peek(p).ival;
                            consume(p);
                        }
                        expect(p, TOK_RBRACKET);
                    }
                    /* 计算总大小 */
                    int gv_unit = gv_ptrs > 0 ? 8 : (typesize > 0 ? typesize : 4);
                    int gv_total = gv_arr_len > 1 ? gv_unit * gv_arr_len : gv_unit;
                    /* 注册 struct 标签和大小（供 sizeof 查找） */
                    {
                        const char *gvn = arena_strdup(p->arena, gv_name.start, gv_name.len);
                        pvar_add_ex(gvn, global_typedef_tag ? global_typedef_tag : (last_struct_tag ? last_struct_tag : ""), 0, gv_total);
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
                        gvar->elem_size = (gv_arr_len > 1) ? gv_unit : 0;
                        *tail = gvar;
                        tail = &gvar->next;
                    }
                    /* 保存初始化器 */
                    if (match(p, TOK_EQ)) {
                        if (peek(p).kind == TOK_LBRACE) {
                            int d = 1; consume(p);
                            while (d > 0 && peek(p).kind != TOK_EOF) {
                                if (peek(p).kind == TOK_LBRACE) d++;
                                if (peek(p).kind == TOK_RBRACE) d--;
                                if (d) consume(p);
                            }
                            if (peek(p).kind == TOK_RBRACE) consume(p);
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
