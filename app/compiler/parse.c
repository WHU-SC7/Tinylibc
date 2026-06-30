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

    if (t.kind == TOK_IDENT) {
        consume(p);
        AstNode *n = new_ast(p, AST_VAR);
        n->name = arena_strdup(p->arena, t.start, t.len);
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
                tail = &(*tail)->next;
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
            /* 结构体成员 s.m — 暂不支持 */
            consume(p);
            consume(p);  /* 跳过成员名 */
            left = left;

        } else if (t.kind == TOK_ARROW) {
            /* 指针成员 p->m — 暂不支持 */
            consume(p);
            consume(p);
            left = left;

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

/* ─── 类型说明符 ─── */

int parse_type_specifier(Parser *p) {
    Token t = peek(p);
    switch (t.kind) {
    case TOK_INT:      consume(p); return 4;
    case TOK_CHAR:     consume(p); return 1;
    case TOK_SHORT:    consume(p); return 2;
    case TOK_LONG:
        consume(p);
        if (peek(p).kind == TOK_LONG) { consume(p); return 8; }
        return 8;
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
    error_at(p, "expected identifier");
    return "";
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
        int ts = parse_type_specifier(p);
        if (ts >= 0) {
            AstNode *decl = new_ast(p, AST_VAR_DECL);
            while (peek(p).kind == TOK_STAR)
                consume(p);
            Token id = peek(p);
            if (id.kind == TOK_IDENT) {
                consume(p);
                decl->name = arena_strdup(p->arena, id.start, id.len);
            }
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

static void parse_parameter_list(Parser *p) {
    expect(p, TOK_LPAREN);
    if (peek(p).kind == TOK_VOID)
        consume(p);
    else if (peek(p).kind == TOK_RPAREN) {
        /* 空 */
    } else {
        while (peek(p).kind != TOK_RPAREN && peek(p).kind != TOK_EOF) {
            parse_type_specifier(p);
            parse_declarator(p);
            if (peek(p).kind == TOK_COMMA) consume(p);
            else if (peek(p).kind == TOK_ELLIPSIS) { consume(p); break; }
        }
    }
    expect(p, TOK_RPAREN);
}

/* ─── 函数定义 ─── */

static AstNode *parse_function_definition(Parser *p, int ret_size) {
    (void)ret_size;
    const char *name = parse_declarator(p);
    parse_parameter_list(p);
    AstNode *body = parse_compound_statement(p);
    AstNode *func = new_ast(p, AST_FUNC_DEF);
    func->name = name;
    func->body = body;
    return func;
}

/* ─── 顶层解析入口 ─── */

AstNode *parse_program(Parser *p) {
    AstNode *head = NULL;
    AstNode **tail = &head;

    while (peek(p).kind != TOK_EOF) {
        while (peek(p).kind == TOK_STATIC || peek(p).kind == TOK_EXTERN ||
               peek(p).kind == TOK_CONST || peek(p).kind == TOK_VOLATILE ||
               peek(p).kind == TOK_RESTRICT || peek(p).kind == TOK_INLINE ||
               peek(p).kind == TOK__ATTRIBUTE__ || peek(p).kind == TOK_TYPEDEF) {
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
