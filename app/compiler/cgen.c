/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * cgen.c — x86_64 代码生成器
 *
 * 机制：遍历 AST 节点，向代码缓冲区中写入 x86_64 机器码。
 *       每识别一个函数定义就记录符号，为后续 ELF 写入做准备。
 *       表达式求值结果始终放在 eax/rax 寄存器（Phase 1）。
 *
 * 指令速查：
 *   55          push rbp
 *   48 89 e5    mov rbp, rsp
 *   b8 xx..     mov eax, imm32
 *   48 c7 c0..  mov rax, imm64
 *   5d          pop rbp
 *   c3          ret
 */

#include "tcc.h"

/* ─── 全局缓冲区 ─── */

unsigned char code_buf[65536];
int code_size;

CgenSym syms[MAX_SYMS];
int sym_count;

Elf64_Rela rels[MAX_RELS];
int rel_count;

char strtab[65536];
int strtab_len;

/* ─── 内部辅助 ─── */

static int add_sym(const char *name, int offset, int size,
                   int is_global, int is_func) {
    if (sym_count >= MAX_SYMS)
        return -1;
    CgenSym *s = &syms[sym_count++];
    s->name = name;
    s->offset = offset;
    s->size = size;
    s->is_global = is_global;
    s->is_func = is_func;
    s->sym_idx = -1;   /* 由 ELF 写入时分配 */
    return sym_count - 1;
}

/* ─── 字节发射 ─── */

static void emit1(int b) {
    code_buf[code_size++] = b & 0xFF;
}

static void emit4(int v) {
    emit1(v);
    emit1(v >> 8);
    emit1(v >> 16);
    emit1(v >> 24);
}

/* ─── x86-64 指令发射 ─── */

static void emit_mov_rbp_rsp(void) {
    emit1(0x55);          /* push rbp */
    emit1(0x48);          /* REX.W */
    emit1(0x89);
    emit1(0xE5);          /* mov rbp, rsp */
}

static void emit_pop_rbp(void) {
    emit1(0x5D);          /* pop rbp */
}

static void emit_ret(void) {
    emit1(0xC3);          /* ret */
}

static void emit_mov_eax_imm(int val) {
    emit1(0xB8);          /* mov eax, imm32 */
    emit4(val);
}

/* ─── 表达式代码生成 ─── */

static void cgen_expr(AstNode *expr) {
    switch (expr->kind) {
    case AST_CONSTANT:
        emit_mov_eax_imm(expr->ival);
        break;
    case AST_VAR:
        /* Phase 2+ */
        break;
    default:
        break;
    }
}

/* ─── 语句代码生成 ─── */

static void cgen_stmt(AstNode *stmt) {
    if (stmt == NULL)
        return;

    switch (stmt->kind) {
    case AST_RETURN:
        if (stmt->expr)
            cgen_expr(stmt->expr);
        emit_pop_rbp();
        emit_ret();
        break;
    case AST_EXPR_STMT:
        if (stmt->expr)
            cgen_expr(stmt->expr);
        break;
    case AST_NULL_STMT:
        break;
    case AST_BLOCK:
        /* 递归处理块内语句 */
        for (AstNode *s = stmt->stmts; s; s = s->next)
            cgen_stmt(s);
        break;
    default:
        break;
    }
}

/* ─── 函数代码生成 ─── */

static void cgen_func_def(AstNode *func) {
    int func_start = code_size;

    /* 函数序言 */
    emit_mov_rbp_rsp();

    /* 函数体（compound statement） */
    if (func->body)
        cgen_stmt(func->body);

    int func_end = code_size;

    /* 记录符号 */
    add_sym(func->name ? func->name : "", func_start,
            func_end - func_start, 1, 1);
}

/* ─── 入口 ─── */

void cgen_init(void) {
    code_size = 0;
    sym_count = 0;
    rel_count = 0;
    strtab_len = 0;
    /* 预留空字符串在索引 0 */
    strtab[strtab_len++] = '\0';
}

void cgen_program(AstNode *prog) {
    if (prog == NULL || prog->kind != AST_PROGRAM)
        return;

    for (AstNode *func = prog->body; func; func = func->next) {
        if (func->kind == AST_FUNC_DEF)
            cgen_func_def(func);
    }
}
