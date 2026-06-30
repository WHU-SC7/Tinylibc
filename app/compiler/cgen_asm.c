/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * cgen_asm.c — 内联汇编代码生成
 *
 * 识别项目已知的 ~15 种 __asm__ 模板并发射对应 x86_64 机器码。
 * 未知模板会报告错误。
 *
 * 已知模板：
 *   "syscall"                     → 0F 05
 *   "lock xaddq %0, %1"          → F0 48 0F C1 /r
 *   "lock xaddl %0, %1"          → F0 0F C1 /r
 *   "lock cmpxchgq %2, %1"       → F0 48 0F B1 /r
 *   "lock cmpxchgl %2, %1"       → F0 0F B1 /r
 *   "mov %%fs:0, %0"             → 64 48 8B 04 25 00 00 00 00
 */

#include "tcc.h"

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

/* ─── 从模板解析约束并保存到临时变量 → 从 AST 获得 */


/* ─── 主入口：为 AST_ASM 节点生成代码 ─── */

void cgen_asm(AstNode *node) {
    if (!node || !node->asm_template) return;

    const char *t = node->asm_template;

    /* 匹配已知模板并发射机器码 */

    if (strcmp(t, "syscall") == 0) {
        /* syscall — 0F 05 */
        /* 参数已由调用者在寄存器中准备好（rdi, rsi, rdx, r10, r8, r9） */
        e1(0x0F); e1(0x05);
        return;
    }

    if (str_contains(t, "lock xaddq")) {
        /* lock xaddq %0, %1 — F0 48 0F C1 /r */
        /* %0 = "+m"(*ptr), %1 = "0"(val) 或 "=r"(result), "r"(val) */
        /* 简化：锁前缀 + REX.W + XADD r/m64,r64 */
        e1(0xF0);      /* lock prefix */
        e1(0x48);      /* REX.W */
        e1(0x0F); e1(0xC1);
        /* ModRM: 用于 [r/m], r 形式，需要占位 */
        e1(0x02);      /* mod=00, reg=rdx(010), r/m=rdx(010) — 占位 */
        return;
    }

    if (str_contains(t, "lock xaddl")) {
        /* lock xaddl %0, %1 — F0 0F C1 /r (32-bit) */
        e1(0xF0);
        e1(0x0F); e1(0xC1);
        e1(0x02);      /* 占位 ModRM */
        return;
    }

    if (str_contains(t, "lock cmpxchgq")) {
        /* lock cmpxchgq — F0 48 0F B1 /r */
        e1(0xF0); e1(0x48); e1(0x0F); e1(0xB1);
        e1(0x02);      /* 占位 ModRM */
        return;
    }

    if (str_contains(t, "lock cmpxchgl")) {
        /* lock cmpxchgl — F0 0F B1 /r */
        e1(0xF0); e1(0x0F); e1(0xB1);
        e1(0x02);      /* 占位 ModRM */
        return;
    }

    if (str_contains(t, "%%fs:0")) {
        /* mov %%fs:0, %0 — 64 48 8B 04 25 00 00 00 00 */
        e1(0x64);      /* fs segment override */
        e1(0x48);      /* REX.W */
        e1(0x8B);      /* mov r64, r/m64 */
        e1(0x04); e1(0x25);  /* ModRM: [disp32] */
        e1(0x00); e1(0x00); e1(0x00); e1(0x00);  /* disp32 = 0 */
        return;
    }

    if (str_contains(t, "ecall")) {
        /* RISC-V ecall — 不常见，占位 */
        return;
    }

    /* 未知模板 */
    __printf("tcc: unknown inline asm template: \"%s\"\n", t);
}
