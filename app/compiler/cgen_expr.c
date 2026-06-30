/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * cgen_expr.c — x86_64 表达式代码生成
 *
 * 表达式求值约定：结果始终放在 eax 中。
 * 二元运算：左操作数入栈，右操作数求值到 eax，出栈到 ecx，计算 ecx OP eax → eax。
 * 函数调用：参数按顺序移入 rdi/rsi/rdx/rcx/r8/r9，call 指令。
 */

#include "tcc.h"

/* ─── push/pop ─── */

static void push_rax(void) { e1(0x50); }  /* push rax */
static void pop_rcx(void) { e1(0x59); }   /* pop rcx */

/* ─── 加载常量到 eax ─── */

static void mov_eax_imm(int v) { e1(0xB8); e4(v); }

/* ─── 加载/存储局部变量 [rbp+disp8] ─── */
/* mov eax, [rbp+disp8] */
static void load_eax_from_rbp(int disp8) {
    e1(0x8B); e1(0x45); e1(disp8 & 0xFF);
}
/* mov [rbp+disp8], eax */
static void store_eax_to_rbp(int disp8) {
    e1(0x89); e1(0x45); e1(disp8 & 0xFF);
}

/* ─── 二元运算（ecx OP eax → eax） ─── */

static void binop_add(void) { e1(0x01); e1(0xC8); }  /* add eax, ecx → eax = eax+ecx */
/* For left - right: ecx=left, eax=right. Need eax = ecx - eax.
   Swap first: xchg eax,ecx (91). Then sub eax,ecx (29 C8) → eax = old_ecx - old_eax */
static void binop_sub_swapped(void) {
    e1(0x91);        /* xchg eax, ecx */
    e1(0x29); e1(0xC8);  /* sub eax, ecx */
}

static void binop_mul(void) { e1(0x0F); e1(0xAF); e1(0xC1); }  /* imul eax, ecx */
static void binop_div(void) { e1(0x91); e1(0x99); e1(0xF7); e1(0xF9); }  /* xchg; cdq; idiv ecx */
static void binop_mod(void) {
    /* xchg eax,ecx; cdq; idiv ecx → result in edx (remainder) */
    binop_div();  /* same as div, but result in edx */
    /* mov eax, edx */
    e1(0x89); e1(0xD0);
}
static void binop_and(void) { e1(0x21); e1(0xC8); }  /* and eax, ecx */
static void binop_or(void)  { e1(0x09); e1(0xC8); }  /* or eax, ecx */
static void binop_xor(void) { e1(0x31); e1(0xC8); }  /* xor eax, ecx */
static void binop_shl(void) { e1(0x91); e1(0xD3); e1(0xE0); }  /* xchg; shl eax, cl */
static void binop_shr(void) { e1(0x91); e1(0xD3); e1(0xE8); }  /* xchg; shr eax, cl */

/* 比较运算：ecx OP eax → eax (0 or 1) */
static void binop_cmp(int setcc_opcode) {
    e1(0x39); e1(0xC1);           /* cmp ecx, eax  (ecx - eax) */
    e1(0x0F); e1(setcc_opcode); e1(0xC0);  /* setcc al */
    e1(0x0F); e1(0xB6); e1(0xC0); /* movzx eax, al */
}

/* ─── 一元运算（eax OP → eax） ─── */

static void unop_neg(void) { e1(0xF7); e1(0xD8); }  /* neg eax */
static void unop_not(void) { e1(0xF7); e1(0xD0); }  /* not eax */
/* ─── 函数调用 ─── */

static void emit_call(const char *name) {
    /* 通过符号表查找函数地址。对于 .o 文件，使用 R_X86_64_PLT32 或 R_X86_64_PC32 重定位 */
    /* 先找到或创建符号 */
    int sym_idx = -1;
    int i;
    for (i = 0; i < sym_count; i++) {
        if (strcmp(syms[i].name, name) == 0) { sym_idx = i; break; }
    }
    if (sym_idx < 0) {
        /* 创建未定义符号 */
        if (sym_count >= MAX_SYMS) return;
        sym_idx = sym_count;
        CgenSym *s = &syms[sym_count++];
        s->name = name;
        s->offset = 0;
        s->size = 0;
        s->is_global = 0;
        s->is_func = 0;
        s->sym_idx = -1;
    }

    /* 记录重定位 */
    if (rel_count >= MAX_RELS) return;
    Elf64_Rela *r = &rels[rel_count++];
    r->r_offset = code_size;      /* 偏移在 .text 中 */
    r->r_info = ELF64_R_INFO(sym_idx, R_X86_64_PLT32);
    r->r_addend = -4;

    /* e8 00 00 00 00: call rel32（占位重定位） */
    e1(0xE8); e4(0);
}

/* ─── 对外接口：表达式代码生成 ─── */

void cgen_expr(AstNode *node) {
    if (!node) return;

    switch (node->kind) {

    case AST_CONSTANT:
        mov_eax_imm(node->ival);
        break;

    case AST_VAR: {
        /* 查找局部变量偏移 */
        int i;
        for (i = 0; i < local_count; i++) {
            if (strcmp(locals[i].name, node->name) == 0) {
                load_eax_from_rbp(locals[i].offset);
                return;
            }
        }
        /* 未找到：当作外部符号（全局变量），留空（Phase 3+） */
        mov_eax_imm(0);
        break;
    }

    case AST_BINOP: {
        if (node->op == TOK_AND_AND || node->op == TOK_OR_OR) {
            cgen_expr(node->left);
            push_rax();
            cgen_expr(node->right);
            pop_rcx();
            e1(0x85); e1(0xC9); e1(0x0F); e1(0x95); e1(0xC2);
            e1(0x0F); e1(0xB6); e1(0xD2);
            e1(0x85); e1(0xC0); e1(0x0F); e1(0x95); e1(0xC0);
            e1(0x0F); e1(0xB6); e1(0xC0);
            if (node->op == TOK_AND_AND) { e1(0x21); e1(0xD0); }
            else { e1(0x09); e1(0xD0); }
            break;
        }

        /* 普通二元运算：左→栈，右→eax，出栈→ecx，计算 */
        cgen_expr(node->left);
        push_rax();
        cgen_expr(node->right);
        pop_rcx();  /* ecx = left, eax = right */

        switch (node->op) {
        case TOK_PLUS:  binop_add(); break;  /* eax = eax + ecx = right + left */
        case TOK_MINUS: binop_sub_swapped(); break;  /* eax = ecx - eax = left - right */
        case TOK_STAR:  binop_mul(); break;  /* imul eax, ecx = left * right */
        case TOK_SLASH: binop_div(); break;  /* eax = left / right */
        case TOK_PERCENT: binop_mod(); break;  /* eax = left % right */

        case TOK_LESS:       binop_cmp(0x9C); break;  /* setl (signed <) */
        case TOK_GREATER:    binop_cmp(0x9F); break;  /* setg (signed >) */
        case TOK_LESS_EQ:    binop_cmp(0x9E); break;  /* setle */
        case TOK_GREATER_EQ: binop_cmp(0x9D); break;  /* setge */
        case TOK_EQ_EQ:      binop_cmp(0x94); break;  /* sete */
        case TOK_NOT_EQ:     binop_cmp(0x95); break;  /* setne */

        case TOK_LESS_LESS:       binop_shl(); break;
        case TOK_GREATER_GREATER: binop_shr(); break;

        case TOK_AMPERSAND: binop_and(); break;  /* eax = eax & ecx = right & left */
        case TOK_PIPE:      binop_or();  break;  /* eax = eax | ecx */
        case TOK_CARET:     binop_xor(); break;

        default: break;
        }
        break;
    }

    case AST_UNARY: {
        cgen_expr(node->expr);  /* 子表达式结果在 eax */
        switch (node->op) {
        case TOK_MINUS: unop_neg(); break;
        case TOK_TILDE: unop_not(); break;  /* ~x */
        case TOK_EXCLAM:
            /* !x: cmp eax, 0; sete al; movzx eax, al */
            e1(0x85); e1(0xC0);           /* test eax, eax */
            e1(0x0F); e1(0x94); e1(0xC0); /* sete al */
            e1(0x0F); e1(0xB6); e1(0xC0); /* movzx eax, al */
            break;
        case TOK_PLUS_PLUS:
        case TOK_MINUS_MINUS:
            /* ++ / -- (前缀) 由 cgen_stmt 处理 */
            break;
        case TOK_STAR:
            /* *ptr — Phase 3+ */
            break;
        case TOK_AMPERSAND:
            /* &var — Phase 3+ */
            break;
        default: break;
        }
        break;
    }

    case AST_ASSIGN: {
        cgen_expr(node->right);
        if (node->left && node->left->kind == AST_VAR) {
            const char *vname = node->left->name;
            int i;
            for (i = 0; i < local_count; i++) {
                if (strcmp(locals[i].name, vname) == 0) {
                    store_eax_to_rbp(locals[i].offset);
                    break;
                }
            }
        } else if (node->left && node->left->kind == AST_MEMBER) {
            /* s.member = expr */
            int moff = node->left->ival;
            if (node->left->op == TOK_DOT && node->left->left->kind == AST_VAR) {
                const char *vname = node->left->left->name;
                int i;
                for (i = 0; i < local_count; i++) {
                    if (strcmp(locals[i].name, vname) == 0) {
                        store_eax_to_rbp(locals[i].offset + moff);
                        break;
                    }
                }
            }
        }
        break;
    }

    case AST_CALL: {
        /* 计算参数 */
        int argc = 0;
        AstNode *arg = node->args;
        while (arg) { argc++; arg = arg->next; }

        /* 限制参数数量 */
        if (argc > 6) argc = 6;

        /* 逐个求值参数并移入寄存器 */
        arg = node->args;
        int ai;
        for (ai = 0; ai < argc; ai++) {
            if (!arg) break;
            cgen_expr(arg);
            /* 保存 eax 到栈（因为后续参数求值会覆盖 eax） */
            push_rax();
            arg = arg->next;
        }

        /* 从栈取出参数逆序入寄存器 */
        for (ai = argc - 1; ai >= 0; ai--) {
            pop_rcx();  /* eax 的值在 ecx 中 */
            /* 从 ecx 移到参数寄存器 */
            switch (ai) {
            case 0: e1(0x89); e1(0xCF); break;  /* mov edi, ecx */
            case 1: e1(0x89); e1(0xCE); break;  /* mov esi, ecx */
            case 2: e1(0x89); e1(0xCA); break;  /* mov edx, ecx */
            case 3: e1(0x89); e1(0xCB); break;  /* mov ecx, ecx? 实际上要保留ECX给移位... */
            case 4: e1(0x41); e1(0x89); e1(0xC8); break;  /* mov r8d, ecx */
            case 5: e1(0x41); e1(0x89); e1(0xC9); break;  /* mov r9d, ecx */
            }
        }

        emit_call(node->name);
        break;
    }

    case AST_MEMBER: {
        /* s.member 或 p->member */
        int member_off = node->ival;
        if (node->op == TOK_DOT) {
            /* s.member：加载结构的基地址 + 成员偏移 */
            if (node->left && node->left->kind == AST_VAR) {
                int i;
                for (i = 0; i < local_count; i++) {
                    if (strcmp(locals[i].name, node->left->name) == 0) {
                        int total_off = locals[i].offset + member_off;
                        if (total_off >= -128 && total_off < 0) {
                            load_eax_from_rbp(total_off);
                        }
                        break;
                    }
                }
            } else {
                cgen_expr(node->left);
                if (member_off != 0) {
                    push_rax();
                    mov_eax_imm(member_off);
                    pop_rcx();
                    binop_add();
                }
            }
        } else {
            /* p->member：解引用指针 + 偏移 */
            cgen_expr(node->left);
            if (member_off != 0) {
                push_rax();
                mov_eax_imm(member_off);
                pop_rcx();
                binop_add();
            }
            /* 从地址加载值：mov eax, [eax] */
            e1(0x8B); e1(0x00);  /* mov eax, [eax] */
        }
        break;
    }

    default:
        break;
    }
}
