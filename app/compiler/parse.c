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

#define MAX_PVARS 256
static const char *pvar_name[MAX_PVARS];   /* 变量名 */
static const char *pvar_tag[MAX_PVARS];    /* struct 标签 */
static int pvar_count;

static void pvar_add(const char *name, const char *tag) {
    if (pvar_count < MAX_PVARS && name && *name) {
        pvar_name[pvar_count] = name;
        pvar_tag[pvar_count] = tag;
        pvar_count++;
    }
}
static const char *pvar_find_tag(const char *name) {
    int i;
    for (i = 0; i < pvar_count; i++)
        if (strcmp(pvar_name[i], name) == 0) return pvar_tag[i];
    return NULL;
}
/* 在 struct 中查找成员偏移 */
static int find_member_offset(const char *struct_tag, const char *member) {
    if (struct_tag && *struct_tag) {
        /* 先查 typedef 表 */
        int ti;
        for (ti = 0; ti < typedef_count; ti++) {
            if (strcmp(typedef_table[ti].name, struct_tag) == 0 && typedef_table[ti].member_count > 0) {
                int mi;
                for (mi = 0; mi < typedef_table[ti].member_count; mi++)
                    if (strcmp(typedef_table[ti].members[mi].name, member) == 0)
                        return typedef_table[ti].members[mi].offset;
            }
        }
        /* 再查 struct 标签表 */
        StructType *st = find_struct_tag(struct_tag);
        if (st) {
            int i;
            for (i = 0; i < st->member_count; i++)
                if (strcmp(st->members[i].name, member) == 0)
                    return st->members[i].offset;
        }
    }
    /* 回退：查 last_struct_members */
    int i;
    for (i = 0; i < last_struct_member_count; i++)
        if (strcmp(last_struct_members[i].name, member) == 0)
            return last_struct_members[i].offset;
    return 0;
}

/* ─── 类型系统全局表 ─── */

StructType tag_table[MAX_TAGS];
int tag_count;

TypedefEntry typedef_table[MAX_TYPEDEFS];
int typedef_count;

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
    __printf("error [line %d]: %s\n", p->lexer->line, msg);
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
}

/* 前向声明 */
static AstNode *parse_expr(Parser *p);
static AstNode *parse_statement(Parser *p);
static AstNode *parse_compound_statement(Parser *p);
int parse_type_specifier(Parser *p);

/* ─── 一元表达式的解析 ─── */
/* 按优先级从低到高定义 */

static AstNode *new_ast(Parser *p, AstKind kind) {
    AstNode *n = arena_alloc(p->arena, sizeof(AstNode));
    n->kind = kind;
    n->next = NULL;
    return n;
}

/* 基本表达式: identifier, number, string, (expr) */
static AstNode *parse_primary(Parser *p) {
    Token t = peek(p);

    if (t.kind == TOK_NUMBER) {
        consume(p);
        AstNode *n = new_ast(p, AST_CONSTANT);
        n->ival = t.ival;
        return n;
    }

    if (t.kind == TOK_IDENT ||
        t.kind == TOK__BUILTIN_VA_START || t.kind == TOK__BUILTIN_VA_ARG ||
        t.kind == TOK__BUILTIN_VA_END || t.kind == TOK__BUILTIN_VA_LIST ||
        t.kind == TOK__ASM__ || t.kind == TOK__ATTRIBUTE__) {
        consume(p);
        AstNode *n = new_ast(p, AST_VAR);
        n->name = arena_strdup(p->arena, t.start, t.len);
        return n;
    }

    /* 类型关键字在表达式中当作常量（兼容 __builtin_va_arg(ap, int)） */
    if (t.kind == TOK_INT || t.kind == TOK_CHAR || t.kind == TOK_SHORT ||
        t.kind == TOK_LONG || t.kind == TOK_VOID || t.kind == TOK_DOUBLE) {
        consume(p);
        AstNode *n = new_ast(p, AST_CONSTANT);
        n->ival = 4;
        return n;
    }

    if (t.kind == TOK_STRING) {
        consume(p);
        AstNode *n = new_ast(p, AST_CONSTANT);
        n->ival = 0;  /* 字符串常量地址暂不支持 */
        return n;
    }

    if (t.kind == TOK_LPAREN) {
        consume(p);
        AstNode *n = parse_expr(p);
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
                *tail = parse_expr(p);
                if (*tail == NULL && call->name) {
                    /* __builtin_va_arg(ap, type) — 类型参数 */
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
            /* 查找成员偏移 */
            if (left && left->kind == AST_VAR) {
                const char *tag = pvar_find_tag(left->name);
                if (tag) n->ival = find_member_offset(tag, n->member_name);
            }
            left = n;

        } else if (t.kind == TOK_ARROW) {
            consume(p);
            Token m = consume(p);
            AstNode *n = new_ast(p, AST_MEMBER);
            n->left = left;
            n->member_name = arena_strdup(p->arena, m.start, m.len);
            n->op = TOK_ARROW;
            /* 通过指针的 struct 标签查找 — 暂简化 */
            if (left && left->kind == AST_VAR) {
                const char *tag = pvar_find_tag(left->name);
                if (tag) n->ival = find_member_offset(tag, n->member_name);
            }
            left = n;

        } else if (t.kind == TOK_PLUS_PLUS) {
            consume(p);
            AstNode *n = new_ast(p, AST_UNARY);
            n->op = TOK_PLUS_PLUS;
            n->expr = left;
            left = n;

        } else if (t.kind == TOK_MINUS_MINUS) {
            consume(p);
            AstNode *n = new_ast(p, AST_UNARY);
            n->op = TOK_MINUS_MINUS;
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
        return n;
    }

    if (t.kind == TOK_SIZEOF) {
        consume(p);
        AstNode *n = new_ast(p, AST_CONSTANT);
        /* sizeof(int) = 4, Phase 3+ 支持更复杂类型 */
        if (peek(p).kind == TOK_LPAREN) {
            consume(p);
            int sz = parse_type_specifier(p);
            if (sz < 0) sz = 4;
            expect(p, TOK_RPAREN);
            n->ival = sz;
        } else {
            n->ival = 4;  /* 默认 sizeof(expr) = 4 */
        }
        return n;
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
        return n;
    }
    return cond;
}

/* 赋值: = += -= *= /= %= <<= >>= &= ^= |= */
static AstNode *parse_assign(Parser *p) {
    AstNode *left = parse_ternary(p);
    if (peek(p).kind == TOK_EQ || peek(p).kind == TOK_PLUS_EQ ||
        peek(p).kind == TOK_MINUS_EQ || peek(p).kind == TOK_STAR_EQ ||
        peek(p).kind == TOK_SLASH_EQ || peek(p).kind == TOK_PERCENT_EQ ||
        peek(p).kind == TOK_LESS_LESS_EQ || peek(p).kind == TOK_GREATER_GREATER_EQ ||
        peek(p).kind == TOK_AND_EQ || peek(p).kind == TOK_OR_EQ ||
        peek(p).kind == TOK_CARET_EQ) {
        Token op = consume(p);
        AstNode *right = parse_assign(p);
        AstNode *n = new_ast(p, AST_ASSIGN);
        n->left = left;
        n->right = right;
        n->op = op.kind;
        return n;
    }
    return left;
}

/* 顶层表达式 */
static AstNode *parse_expr(Parser *p) {
    return parse_assign(p);
}

/* ─── 解析 struct 体（返回成员列表和总大小） ─── */

static int parse_struct_body(Parser *p, Member *members, int *out_count) {
    expect(p, TOK_LBRACE);
    int count = 0;
    int offset = 0;

    while (peek(p).kind != TOK_RBRACE && peek(p).kind != TOK_EOF) {
        int sz = parse_type_specifier(p);
        if (sz < 0) { error_at(p, "invalid struct member type"); break; }

        while (peek(p).kind == TOK_STAR) { consume(p); sz = 8; }  /* 指针 */

        Token id = peek(p);
        if (id.kind == TOK_IDENT) {
            consume(p);
            if (count < MAX_MEMBERS) {
                members[count].name = arena_strdup(p->arena, id.start, id.len);
                members[count].offset = offset;
                members[count].size = sz;
                count++;
                offset += sz;
            }
        }

        /* 可能的位域或数组 — 跳过 */
        if (peek(p).kind == TOK_COLON) { consume(p);
            while (peek(p).kind != TOK_SEMI && peek(p).kind != TOK_EOF) consume(p); }

        expect(p, TOK_SEMI);
    }
    expect(p, TOK_RBRACE);

    /* 对齐到最大成员对齐（简单四字节对齐） */
    offset = (offset + 3) & ~3;

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
        out->tag = tag;
        /* 保存到 last_struct_* 供变量声明和成员访问使用 */
        last_struct_tag = tag;
        last_struct_member_count = out->member_count;
        int mi;
        for (mi = 0; mi < out->member_count && mi < MAX_MEMBERS; mi++)
            last_struct_members[mi] = out->members[mi];

        if (tag) {
            StructType *existing = find_struct_tag(tag);
            if (!existing) add_struct_tag(tag, out);
        }
    } else if (tag) {
        /* struct tag (前置声明或引用) */
        StructType *existing = find_struct_tag(tag);
        if (existing) {
            *out = *existing;
        } else {
            /* 不完全类型，暂不支持 */
        }
    }
    return out->total_size;
}

/* ─── 类型说明符 ─── */

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
        if (peek(p).kind == TOK_LONG) { consume(p); return 8; }
        return 8;
    case TOK_STRUCT: {
        consume(p);
        StructType st;
        int sz = parse_struct_type(p, &st);
        return sz > 0 ? sz : 4;
    }
    case TOK_VOID:     consume(p); return 0;
    case TOK_UNSIGNED:
        consume(p);
        if (peek(p).kind == TOK_CHAR) { consume(p); return 1; }
        if (peek(p).kind == TOK_SHORT) { consume(p); return 2; }
        if (peek(p).kind == TOK_LONG) {
            consume(p);
            if (peek(p).kind == TOK_LONG) { consume(p); return 8; }
            return 8;
        }
        if (peek(p).kind == TOK_INT) { consume(p); return 4; }
        return 4;
    case TOK_SIGNED:
        consume(p);
        if (peek(p).kind == TOK_CHAR) { consume(p); return 1; }
        if (peek(p).kind == TOK_SHORT) { consume(p); return 2; }
        if (peek(p).kind == TOK_LONG) {
            consume(p);
            if (peek(p).kind == TOK_LONG) { consume(p); return 8; }
            return 8;
        }
        if (peek(p).kind == TOK_INT) { consume(p); return 4; }
        return 4;
    default:
        return -1;
    }
}

/* ─── 声明符 ─── */

static const char *parse_declarator(Parser *p) {
    while (match(p, TOK_STAR)) ;
    Token t = peek(p);
    if (t.kind == TOK_IDENT) {
        consume(p);
        return arena_strdup(p->arena, t.start, t.len);
    }
    /* 处理 (*name)(params) — 函数指针 */
    if (t.kind == TOK_LPAREN) {
        consume(p);
        if (peek(p).kind == TOK_STAR) {
            consume(p);
            Token nt = peek(p);
            const char *name = "";
            if (nt.kind == TOK_IDENT) { consume(p); name = arena_strdup(p->arena, nt.start, nt.len); }
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
    n->expr = parse_expr(p);
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
        int ts = parse_type_specifier(p);
        if (ts >= 0) {
            n->loop_init = new_ast(p, AST_VAR_DECL);
            n->loop_init->name = parse_declarator(p);
            if (match(p, TOK_EQ))
                n->loop_init->expr = parse_expr(p);
        } else {
            n->loop_init = new_ast(p, AST_EXPR_STMT);
            n->loop_init->expr = parse_expr(p);
        }
    }
    expect(p, TOK_SEMI);

    /* condition */
    if (peek(p).kind != TOK_SEMI)
        n->loop_cond = parse_expr(p);
    expect(p, TOK_SEMI);

    /* step */
    if (peek(p).kind != TOK_RPAREN) {
        n->loop_step = parse_expr(p);
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
        /* 处理 register __asm__ 变量声明 */
        if (peek(p).kind == TOK_REGISTER) {
            skip_register_asm(p);
            continue;
        }
        int ts = parse_type_specifier(p);
        if (ts >= 0) {
            AstNode *decl = new_ast(p, AST_VAR_DECL);
            decl->ival = ts;
            decl->name = parse_declarator(p);
            if (decl->name && *decl->name) {
                if (last_struct_tag || last_struct_member_count > 0) {
                    pvar_add(decl->name, last_struct_tag ? last_struct_tag : "");
                } else {
                    int ti;
                    for (ti = 0; ti < typedef_count; ti++) {
                        if (typedef_table[ti].member_count > 0 && ts == typedef_table[ti].size) {
                            pvar_add(decl->name, typedef_table[ti].name);
                            break;
                        }
                    }
                }
            }
            last_struct_tag = NULL;
            if (match(p, TOK_EQ))
                decl->expr = parse_expr(p);
            expect(p, TOK_SEMI);
            *tail = decl;
            tail = &decl->next;
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
    case TOK_BREAK:   return parse_break(p);
    case TOK_CONTINUE: return parse_continue(p);
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
        AstNode *expr = parse_expr(p);
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
    expect(p, TOK_LPAREN);
    AstNode *head = NULL;
    if (is_variadic) *is_variadic = 0;
    AstNode **tail = &head;
    if (peek(p).kind == TOK_VOID)
        consume(p);
    else if (peek(p).kind == TOK_RPAREN) {
        /* 空 */
    } else {
        while (peek(p).kind != TOK_RPAREN && peek(p).kind != TOK_EOF) {
            if (peek(p).kind == TOK_ELLIPSIS) {
                consume(p);
                if (is_variadic) *is_variadic = 1;
                break;
            }
            int psz = parse_type_specifier(p);
            const char *pname = parse_declarator(p);
            if (pname && *pname) {
                AstNode *pd = new_ast(p, AST_VAR_DECL);
                pd->name = pname;
                pd->ival = psz > 0 ? psz : 4;
                *tail = pd;
                tail = &pd->next;
            }
            if (peek(p).kind == TOK_COMMA) consume(p);
        }
    }
    expect(p, TOK_RPAREN);
    return head;
}

/* ─── 函数定义 ─── */

static AstNode *parse_function_definition(Parser *p, int ret_size) {
    (void)ret_size;
    const char *name = parse_declarator(p);
    int is_variadic = 0;
    AstNode *params = parse_parameter_list(p, &is_variadic);
    AstNode *body = parse_compound_statement(p);
    /* 在 params 被链接到 block 之前记录参数个数 */
    int pcount = 0;
    { AstNode *pp; for (pp = params; pp; pp = pp->next) pcount++; }
    /* 将参数声明前置到函数体开头 */
    if (params) {
        AstNode *last = params;
        while (last->next) last = last->next;
        last->next = body->stmts;
        body->stmts = params;
    }
    AstNode *func = new_ast(p, AST_FUNC_DEF);
    func->name = name;
    func->params = params;
    func->body = body;
    func->is_static = is_variadic;
    func->ival = pcount;  /* 实际参数个数 */
    return func;
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
            (void)tsz;
            const char *tname = parse_declarator(p);
            if (tname && *tname && typedef_count < MAX_TYPEDEFS) {
                TypedefEntry *te = &typedef_table[typedef_count];
                te->name = tname;
                te->size = tsz;
                te->type_kind = (last_struct_member_count > 0) ? 1 : 0;
                te->struct_idx = -1;
                /* 对 struct typedef 保存成员信息 */
                if (last_struct_member_count > 0) {
                    te->member_count = last_struct_member_count;
                    int mi;
                    for (mi = 0; mi < last_struct_member_count && mi < MAX_MEMBERS; mi++)
                        te->members[mi] = last_struct_members[mi];
                }
                typedef_count++;
            }
            expect(p, TOK_SEMI);
            continue;
        }

        /* 跳过存储类和限定符 */
        while (peek(p).kind == TOK_STATIC || peek(p).kind == TOK_EXTERN ||
               peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
               peek(p).kind == TOK_RESTRICT || peek(p).kind == TOK_REGISTER ||
               peek(p).kind == TOK_INLINE || peek(p).kind == TOK__ATTRIBUTE__) {
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

        if (peek(p).kind != TOK_IDENT) {
            p->lexer->pos = save_pos;
            p->lexer->line = save_line;
            p->lexer->col = save_col;
            p->tok = saved_tok;
            error_at(p, "expected identifier");
            break;
        }

        consume(p);
        int is_func = (peek(p).kind == TOK_LPAREN);

        p->lexer->pos = save_pos;
        p->lexer->line = save_line;
        p->lexer->col = save_col;
        p->tok = saved_tok;

        if (is_func) {
            AstNode *func = parse_function_definition(p, typesize);
            *tail = func;
            tail = &func->next;
        } else {
            while (peek(p).kind == TOK_STAR) consume(p);
            if (peek(p).kind == TOK_IDENT) consume(p);
            if (match(p, TOK_EQ)) parse_expr(p);
            expect(p, TOK_SEMI);
        }
    }

    AstNode *prog = new_ast(p, AST_PROGRAM);
    prog->body = head;
    return prog;
}
