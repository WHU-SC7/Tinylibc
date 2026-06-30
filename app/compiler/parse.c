/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * parse.c — C 递归下降解析器
 *
 * 机制：将 Token 流解析为 AST。每个语法产生式对应一个函数。
 * 当前支持：函数定义、return 语句、整数常量。
 *
 * 索引：
 *   parse_program            翻译单元：解析函数定义序列
 *   parse_function_definition 类型 标识符 ( 参数 ) { 语句 }
 *     parse_compound_statement { 语句序列 }
 *     parse_statement         根据当前 Token 分派
 *       parse_return_statement return 表达式 ;
 *     parse_expression        表达式（Phase 1：仅常量）
 */

#include "tcc.h"

/* ─── 错误报告 ─── */

void error_at(Parser *p, const char *msg) {
    __printf("error [line %d]: %s\n", p->lexer->line, msg);
    p->had_error = 1;
}

/* ─── Token 辅助 ─── */

static Token peek(Parser *p) {
    return p->tok;
}

static Token consume(Parser *p) {
    Token t = p->tok;
    p->tok = lexer_next(p->lexer);
    return t;
}

static int match(Parser *p, TokenKind kind) {
    if (p->tok.kind == kind) {
        consume(p);
        return 1;
    }
    return 0;
}

static int expect(Parser *p, TokenKind kind) {
    if (p->tok.kind == kind) {
        consume(p);
        return 1;
    }
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

/* ─── 类型说明符 ─── */

static int parse_type_specifier(Parser *p) {
    /* 返回类型的尺寸（字节数），0 表示 void */
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
        return -1;  /* 不是类型说明符 */
    }
}

/* ─── 声明符 ─── */

static const char *parse_declarator(Parser *p) {
    /* 处理指针 */
    while (match(p, TOK_STAR))
        ;

    /* 标识符 */
    Token t = peek(p);
    if (t.kind == TOK_IDENT) {
        consume(p);
        return arena_strdup(p->arena, t.start, t.len);
    }

    error_at(p, "expected identifier");
    return "";
}

/* ─── 表达式 ─── */

static AstNode *parse_expression(Parser *p) {
    Token t = peek(p);

    if (t.kind == TOK_NUMBER) {
        consume(p);
        AstNode *n = arena_alloc(p->arena, sizeof(AstNode));
        n->kind = AST_CONSTANT;
        n->next = NULL;
        n->ival = t.ival;
        return n;
    }

    if (t.kind == TOK_IDENT) {
        consume(p);
        AstNode *n = arena_alloc(p->arena, sizeof(AstNode));
        n->kind = AST_VAR;
        n->next = NULL;
        n->name = arena_strdup(p->arena, t.start, t.len);
        return n;
    }

    if (t.kind == TOK_LPAREN) {
        consume(p);
        AstNode *e = parse_expression(p);
        expect(p, TOK_RPAREN);
        return e;
    }

    if (t.kind == TOK_STRING) {
        consume(p);
        return NULL;
    }

    error_at(p, "expected expression");
    return NULL;
}

/* 前向声明 */
static AstNode *parse_compound_statement(Parser *p);

/* ─── return 语句 ─── */

static AstNode *parse_return_statement(Parser *p) {
    consume(p);  /* return */
    AstNode *n = arena_alloc(p->arena, sizeof(AstNode));
    n->kind = AST_RETURN;
    n->next = NULL;
    n->expr = parse_expression(p);
    expect(p, TOK_SEMI);
    return n;
}

/* ─── 语句 ─── */

static AstNode *parse_statement(Parser *p) {
    Token t = peek(p);

    switch (t.kind) {
    case TOK_RETURN:
        return parse_return_statement(p);

    case TOK_LBRACE:
        return parse_compound_statement(p);

    case TOK_SEMI:
        consume(p);
        {
            AstNode *n = arena_alloc(p->arena, sizeof(AstNode));
            n->kind = AST_NULL_STMT;
            n->next = NULL;
            return n;
        }

    case TOK_IF:
    case TOK_WHILE:
    case TOK_FOR:
    case TOK_DO:
    case TOK_SWITCH:
    case TOK_GOTO:
    case TOK_BREAK:
    case TOK_CONTINUE:
        error_at(p, "this statement type not yet implemented");
        return NULL;

    default: {
        AstNode *expr = parse_expression(p);
        expect(p, TOK_SEMI);
        if (expr == NULL)
            return NULL;
        AstNode *n = arena_alloc(p->arena, sizeof(AstNode));
        n->kind = AST_EXPR_STMT;
        n->next = NULL;
        n->expr = expr;
        return n;
    }
    }
}

/* ─── 复合语句 ─── */

AstNode *parse_compound_statement(Parser *p) {
    expect(p, TOK_LBRACE);

    AstNode *head = NULL;
    AstNode **tail = &head;

    while (peek(p).kind != TOK_RBRACE && peek(p).kind != TOK_EOF) {
        int typesize = parse_type_specifier(p);
        if (typesize >= 0) {
            const char *varname = parse_declarator(p);
            AstNode *decl = arena_alloc(p->arena, sizeof(AstNode));
            decl->kind = AST_VAR_DECL;
            decl->name = varname;
            decl->next = NULL;

            if (match(p, TOK_EQ))
                decl->expr = parse_expression(p);

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

    AstNode *block = arena_alloc(p->arena, sizeof(AstNode));
    block->kind = AST_BLOCK;
    block->stmts = head;
    return block;
}

/* ─── 参数列表 ─── */

static void parse_parameter_list(Parser *p) {
    expect(p, TOK_LPAREN);
    if (peek(p).kind == TOK_VOID)
        consume(p);
    else if (peek(p).kind == TOK_RPAREN) {
        /* 空参数表 */
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

    AstNode *func = arena_alloc(p->arena, sizeof(AstNode));
    func->kind = AST_FUNC_DEF;
    func->name = name;
    func->body = body;
    func->next = NULL;
    return func;
}

/* ─── 顶层解析入口 ─── */

AstNode *parse_program(Parser *p) {
    AstNode *head = NULL;
    AstNode **tail = &head;

    while (peek(p).kind != TOK_EOF) {
        /* 跳过存储类和限定符 */
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

        /* 保存位置用来回溯判断函数/变量 */
        const char *save_pos = p->lexer->pos;
        int save_line = p->lexer->line;
        int save_col = p->lexer->col;
        Token saved_tok = p->tok;

        /* 跳过可能的指针 */
        while (peek(p).kind == TOK_STAR)
            consume(p);

        if (peek(p).kind != TOK_IDENT) {
            /* 不是合法声明，恢复 */
            p->lexer->pos = save_pos;
            p->lexer->line = save_line;
            p->lexer->col = save_col;
            p->tok = saved_tok;
            error_at(p, "expected identifier in declaration");
            break;
        }

        /* 吃一个 token 看后面是不是 '(' */
        consume(p);
        int is_func = (peek(p).kind == TOK_LPAREN);

        /* 恢复标识符 token */
        p->lexer->pos = save_pos;
        p->lexer->line = save_line;
        p->lexer->col = save_col;
        p->tok = saved_tok;

        if (is_func) {
            AstNode *func = parse_function_definition(p, typesize);
            *tail = func;
            tail = &func->next;
        } else {
            while (peek(p).kind == TOK_STAR)
                consume(p);
            if (peek(p).kind == TOK_IDENT) consume(p);
            if (match(p, TOK_EQ))
                parse_expression(p);
            expect(p, TOK_SEMI);
        }
    }

    AstNode *prog = arena_alloc(p->arena, sizeof(AstNode));
    prog->kind = AST_PROGRAM;
    prog->body = head;
    return prog;
}
