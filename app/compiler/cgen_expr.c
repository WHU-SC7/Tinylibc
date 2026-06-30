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
static void pop_rax(void) { e1(0x58); }   /* pop rax */
static void pop_rcx(void) { e1(0x59); }   /* pop rcx */
static void push_rcx(void) { e1(0x51); }  /* push rcx */

/* ─── 加载常量到 eax ─── */

static void mov_eax_imm(int v) { e1(0xB8); e4(v); }

/* ─── 加载/存储局部变量 [rbp+disp8] ─── */
/* mov eax, [rbp+disp8] */
static void load_eax_from_rbp(int disp8) {
    e1(0x8B); e1(0x45); e1(disp8 & 0xFF);
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
        s->is_global = 1;   /* GLOBAL 以便链接器解析 */
        s->is_func = 1;
        s->sym_idx = -1;
    }

    /* 记录重定位 */
    if (rel_count >= MAX_RELS) return;
    Elf64_Rela *r = &rels[rel_count++];
    int call_off = code_size;
    r->r_offset = call_off + 1;
    r->r_info = ELF64_R_INFO(sym_idx + 1, R_X86_64_PLT32);
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
            if (node->expr && node->expr->kind == AST_VAR) {
                int found = 0;
                int i;
                for (i = 0; i < local_count; i++) {
                    if (strcmp(locals[i].name, node->expr->name) == 0) {
                        e1(0x8D); e1(0x45); e1(locals[i].offset & 0xFF);
                        found = 1;
                        break;
                    }
                }
                if (!found && node->expr->name) {
                    /* 函数/全局符号 — 插入重定位 R_X86_64_32 */
                    int sym_idx = -1;
                    for (i = 0; i < sym_count; i++) {
                        if (syms[i].name && strcmp(syms[i].name, node->expr->name) == 0)
                            { sym_idx = i; break; }
                    }
                    if (sym_idx < 0 && sym_count < MAX_SYMS) {
                        sym_idx = sym_count;
                        CgenSym *s = &syms[sym_count++];
                        s->name = node->expr->name;
                        s->offset = 0; s->size = 0;
                        s->is_global = 1; s->is_func = 1;
                        s->sym_idx = -1;
                    }
                    int reloc_off = code_size;
                    e1(0xB8); e4(0);
                    if (sym_idx >= 0 && rel_count < MAX_RELS) {
                        Elf64_Rela *r = &rels[rel_count++];
                        r->r_offset = reloc_off + 1;
                        r->r_info = ELF64_R_INFO(sym_idx + 1, R_X86_64_32);
                        r->r_addend = 0;
                    }
                }
            }
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
                    e1(0x89); e1(0x45); e1(locals[i].offset & 0xFF);
                    break;
                }
            }
        } else if (node->left && node->left->kind == AST_MEMBER) {
            int moff = node->left->ival;
            if (node->left->op == TOK_DOT && node->left->left->kind == AST_VAR) {
                const char *vname = node->left->left->name;
                int i;
                for (i = 0; i < local_count; i++) {
                    if (strcmp(locals[i].name, vname) == 0) {
                        e1(0x89); e1(0x45); e1((locals[i].offset + moff) & 0xFF);
                        break;
                    }
                }
            }
        }
        break;
    }

    case AST_CALL: {
        int argc = 0;
        AstNode *arg = node->args;
        while (arg) { argc++; arg = arg->next; }
        if (argc > 6) argc = 6;

        /* 处理 __builtin_va_* */
        if (node->name && node->name[0] == '_' && node->name[1] == '_') {
            if (strcmp(node->name, "__builtin_va_start") == 0 && node->args) {
                /* va_start(ap, last_param) — 需要 ap 的地址 */
                if (node->args->kind == AST_VAR) {
                    int vi;
                    for (vi = 0; vi < local_count; vi++) {
                        if (strcmp(locals[vi].name, node->args->name) == 0) {
                            e1(0x48); e1(0x8D); e1(0x45); e1(locals[vi].offset & 0xFF);
                            break;
                        }
                    }
                } else {
                    cgen_expr(node->args);
                }
                push_rax();
                /* gp_offset = 8 * 命名参数个数 */
                pop_rcx();
                e1(0xC7); e1(0x01); e4(func_nparams * 8);
                /* 设置 fp_offset = 48 */
                e1(0xC7); e1(0x41); e1(0x04); e4(48);  /* mov dword [rcx+4], 48 */
                /* 设置 overflow_arg_area = rbp+16 */
                e1(0x48); e1(0x8D); e1(0x45); e1(0x10);  /* lea rax, [rbp+16] */
                e1(0x48); e1(0x89); e1(0x41); e1(0x08);  /* mov [rcx+8], rax */
                /* 设置 reg_save_area = rsp （寄存器保存区在栈底） */
                e1(0x48); e1(0x89); e1(0xE0);  /* mov rax, rsp */
                e1(0x48); e1(0x89); e1(0x41); e1(0x10);  /* mov [rcx+16], rax */
                break;
            }
            if (strcmp(node->name, "__builtin_va_arg") == 0 && node->args) {
                /* va_arg(ap, type) — 读取下一个参数 */
                cgen_expr(node->args);  /* ap → eax */
                push_rax();
                pop_rcx();
                /* 从 reg_save_area + gp_offset 读取 */
                e1(0x8B); e1(0x41); e1(0x10);  /* mov eax, [rcx+16] — reg_save_area */
                push_rax();
                /* gp_offset */
                e1(0x8B); e1(0x09);  /* mov ecx, [rcx] — gp_offset */
                pop_rax();
                /* 读取值： mov eax, [reg_save_area + gp_offset] */
                push_rcx();
                e1(0x8B); e1(0x04); e1(0x08);  /* mov eax, [rax + rcx] */
                /* 更新 gp_offset += 8 */
                /* 当前 eax 是返回值，需要保存 */
                /* 最好在更新前计算地址 */
                break;
            }
            if (strcmp(node->name, "__builtin_va_end") == 0) {
                break;  /* no-op */
            }
        }

        /* 检查是否为函数指针调用 */
        int is_fptr = 0;
        int fptr_offset = 0;
        if (node->name) {
            int i;
            for (i = 0; i < local_count; i++) {
                if (strcmp(locals[i].name, node->name) == 0) {
                    is_fptr = 1;
                    fptr_offset = locals[i].offset;
                    break;
                }
            }
        }

        if (is_fptr) {
            load_eax_from_rbp(fptr_offset);
            push_rax();
        }

        /* 求值参数 */
        arg = node->args;
        int ai;
        for (ai = 0; ai < argc; ai++) {
            if (!arg) break;
            cgen_expr(arg);
            push_rax();
            arg = arg->next;
        }

        /* 参数入寄存器 */
        for (ai = argc - 1; ai >= 0; ai--) {
            pop_rcx();
            switch (ai) {
            case 0: e1(0x89); e1(0xCF); break;
            case 1: e1(0x89); e1(0xCE); break;
            case 2: e1(0x89); e1(0xCA); break;
            case 3: /* ecx */ break;
            case 4: e1(0x41); e1(0x89); e1(0xC8); break;
            case 5: e1(0x41); e1(0x89); e1(0xC9); break;
            }
        }

        if (is_fptr) {
            pop_rcx();
            e1(0xFF); e1(0xD1); /* call *rcx */
        } else {
            emit_call(node->name);
        }
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
