/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * cgen_asm.c — 内联汇编代码生成
 *
 * 处理 GCC 扩展 asm 的约束加载 + 已知模板的机器码发射。
 *
 * 约束处理流程：
 *   输入 "a"(expr)  → cgen_expr(expr) 得值在 rax → 按约束字母移入目标寄存器
 *   输出 "=a"(var)  → 将 rax 存入 var 的栈槽
 *   模板            → 匹配已知字符串并发射对应 x86_64 机器码
 */

#include "toyc.h"

/* ─── 在模板字符串中查找子串 ─── */

static int str_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return 1;
        haystack++;
    }
    return 0;
}

/* ─── 约束字母 → 寄存器 mov (从 rax 移入) ─── */

static void emit_mov_from_rax(char constraint_letter) {
    /*
     * 编码：48 89 /r  (REX.W + MOV r/m64, r64)
     * reg 字段 = source (0 = rax)
     * r/m 字段 = destination
     */
    switch (constraint_letter) {
    case 'a': /* rax — already there */ break;
    case 'D': e1(0x48); e1(0x89); e1(0xC7); break; /* mov rdi, rax */
    case 'S': e1(0x48); e1(0x89); e1(0xC6); break; /* mov rsi, rax */
    case 'd': e1(0x48); e1(0x89); e1(0xC2); break; /* mov rdx, rax */
    case 'b': e1(0x48); e1(0x89); e1(0xC3); break; /* mov rbx, rax */
    case 'c': e1(0x48); e1(0x89); e1(0xC1); break; /* mov rcx, rax */
    default: break;
    }
}

/* ─── 约束字母 → 扩展寄存器 mov (r10/r8/r9) ─── */

static void emit_mov_ext_from_rax(int reg_num) {
    /*
     * REX.W + REX.B + MOV r/m64, r64
     * reg=0 (rax), r/m=low3(reg_num), with REX.B for r8-r15
     * r10=10 → r/m=010, REX.B=1 → 49 89 C2
     * r8 =8  → r/m=000, REX.B=1 → 49 89 C0
     * r9 =9  → r/m=001, REX.B=1 → 49 89 C1
     */
    int modrm = 0xC0 | (reg_num & 7);  /* mod=11, reg=0, r/m=low3(reg_num) */
    e1(0x49);  /* REX.W + REX.B */
    e1(0x89);
    e1(modrm);
}

/* ─── 主入口：为 AST_ASM 节点生成代码 ─── */

void cgen_asm(AstNode *node) {
    if (!node || !node->asm_.asm_template) return;

    const char *t = node->asm_.asm_template;

    /* ================================================================
     * Phase 1: 处理输入约束 — 将表达式值加载到指定寄存器
     * ================================================================
     * 对每个输入 "LETTER"(expr):
     *   cgen_expr(expr) 计算值（结果在 rax）
     *   若 LETTER != 'a': mov REG, rax
     *
     * 注意：对 "r" 约束，按序分配 r10 → r8 → r9 → (r11, rbx, ...)
     * 这是为了兼容 __syscall6 的 "r"(a4), "r"(a5), "r"(a6)
     * ================================================================ */

    int r_counter = 0;  /* "r" 约束分配计数器：0→r10, 1→r8, 2→r9 */

    /* Phase 1: 加载约束值到寄存器
     *
     * 注意：cgen_expr() 每次都将结果放在 rax 中，因此必须最后处理 "a" 约束
     * 否则后续 constraint 的 cgen_expr 会覆盖 rax 中的 syscall number。
     *
     * 策略：
     *   先处理所有非 "a" 约束 (cgen_expr → rax → mov 到目标寄存器)
     *   最后处理 "a" 约束 (cgen_expr → rax，留在 rax 中作为 syscall number)
     */
    int a_idx = -1;
    int i;

    /* 找 "a" 约束的索引 */
    for (i = 0; i < node->asm_.input_count; i++) {
        const char *c = node->asm_.inputs[i].constraint;
        if (c && c[0] == 'a' && c[0] != '=') { a_idx = i; break; }
    }

    /* 处理非 "a" 约束 */
    for (i = 0; i < node->asm_.input_count; i++) {
        if (i == a_idx) continue;
        const char *c = node->asm_.inputs[i].constraint;
        AstNode *expr = node->asm_.inputs[i].expr;
        if (!c || !expr) continue;
        if (c[0] == '=') continue;

        cgen_expr(expr);  /* 结果 → rax */

        if (c[0] == 'r') {
            int ext_regs[] = {10, 8, 9};
            if (r_counter < 3) emit_mov_ext_from_rax(ext_regs[r_counter]);
            r_counter++;
        } else {
            emit_mov_from_rax(c[0]);
        }
    }

    /* 最后处理 "a" 约束 — rax 保留最终值直到 syscall */
    if (a_idx >= 0) {
        AstNode *expr = node->asm_.inputs[a_idx].expr;
        if (expr) cgen_expr(expr);  /* rax = syscall number */
    }

    /* ================================================================
     * Phase 2: 匹配已知模板并发射机器码
     * ================================================================ */

    /* 特定模式要先于通用模式检查，否则 mov $N,%%rax+syscall 会被
     * 前面的 syscall 分支吃掉。 */
    if (str_contains(t, "mov $") && str_contains(t, "%%rax") && str_contains(t, "syscall")) {
        /* mov $N, %%rax + syscall — parse immediate */
        const char *p = t;
        while (*p && *p != '$') p++;
        int imm = 0;
        if (*p == '$') {
            p++;
            while (*p >= '0' && *p <= '9') { imm = imm * 10 + (*p - '0'); p++; }
        }
        e1(0x48); e1(0xC7); e1(0xC0); e4(imm);
        e1(0x0F); e1(0x05);
    } else if (str_contains(t, "syscall")) {
        e1(0x0F); e1(0x05);  /* syscall */
    } else if (str_contains(t, "lock xaddq")) {
        e1(0xF0); e1(0x48); e1(0x0F); e1(0xC1); e1(0x02);
    } else if (str_contains(t, "lock xaddl")) {
        e1(0xF0); e1(0x0F); e1(0xC1); e1(0x02);
    } else if (str_contains(t, "lock cmpxchgq")) {
        e1(0xF0); e1(0x48); e1(0x0F); e1(0xB1); e1(0x02);
    } else if (str_contains(t, "lock cmpxchgl")) {
        e1(0xF0); e1(0x0F); e1(0xB1); e1(0x02);
    } else if (str_contains(t, "%%fs:0")) {
        /* mov %%fs:0, %0 — 64 48 8B 04 25 00 00 00 00 */
        e1(0x64); e1(0x48); e1(0x8B); e1(0x04); e1(0x25);
        e4(0);
    } else if (str_contains(t, "ecall")) {
        /* RISC-V ecall — placeholder */
    } else {
        /* 未知模板 — 警告但不阻止编译 */
        __eprintf("toyc: warning: unknown inline asm template: \"%s\"\n", t);
        return;
    }

    /* ================================================================
     * Phase 3: 处理输出约束 — 将 rax 存入输出变量
     * ================================================================
     * 对输出 "=a"(var):
     *   在 locals[] 中查找 var 的栈偏移 → mov [rbp+off], rax
     * 注意：仅处理 AST_VAR 简单变量，复合表达式暂不支持
     * ================================================================ */

    for (i = 0; i < node->asm_.output_count; i++) {
        const char *c = node->asm_.outputs[i].constraint;
        AstNode *expr = node->asm_.outputs[i].expr;
        if (!c || !expr) continue;

        if (expr->kind != AST_VAR) continue;

        int j;
        for (j = 0; j < local_count; j++) {
            if (strcmp(locals[j].name, expr->name) != 0) continue;

            int off = locals[j].offset & 0xFF;  /* 偏移在 -128~127 内（由 toyc 保证） */
            if (locals[j].size == 8) {
                e1(0x48); e1(0x89); e1(0x45); e1(off);  /* mov [rbp+off], rax */
            } else if (locals[j].size == 4) {
                e1(0x89); e1(0x45); e1(off);              /* mov [rbp+off], eax */
            } else if (locals[j].size == 2) {
                e1(0x66); e1(0x89); e1(0x45); e1(off);   /* mov [rbp+off], ax */
            } else if (locals[j].size == 1) {
                e1(0x88); e1(0x45); e1(off);              /* mov [rbp+off], al */
            }
            break;
        }
    }
}
