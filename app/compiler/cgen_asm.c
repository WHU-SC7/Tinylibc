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
 *   "mov $N, %%rax ... syscall"   → 48 C7 C0 imm32 + 0F 05
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

    if (str_contains(t, "syscall")) {
        /* 通用 syscall 模板：从局部变量加载参数到 syscall 寄存器
         * x86_64 syscall 约定：rax=num, rdi, rsi, rdx, r10, r8, r9
         * 参数映射：n→rax, a1→rdi, a2→rsi, a3→rdx, a4→r10, a5→r8, a6→r9 */
        int i;
        /* rax = n (syscall number) */
        for (i = 0; i < local_count; i++) {
            if (strcmp(locals[i].name, "n") == 0) {
                e1(0x48); e1(0x8B); e1(0x45); e1(locals[i].offset & 0xFF);  /* mov rax, [rbp+off] */
                break;
            }
        }
        /* rdi = a1 */
        for (i = 0; i < local_count; i++) {
            if (strcmp(locals[i].name, "a1") == 0) {
                e1(0x48); e1(0x8B); e1(0x7D); e1(locals[i].offset & 0xFF);  /* mov rdi, [rbp+off] */
                break;
            }
        }
        /* rsi = a2 */
        for (i = 0; i < local_count; i++) {
            if (strcmp(locals[i].name, "a2") == 0) {
                e1(0x48); e1(0x8B); e1(0x75); e1(locals[i].offset & 0xFF);  /* mov rsi, [rbp+off] */
                break;
            }
        }
        /* rdx = a3 */
        for (i = 0; i < local_count; i++) {
            if (strcmp(locals[i].name, "a3") == 0) {
                e1(0x48); e1(0x8B); e1(0x55); e1(locals[i].offset & 0xFF);  /* mov rdx, [rbp+off] */
                break;
            }
        }
        /* r10 = a4 */
        for (i = 0; i < local_count; i++) {
            if (strcmp(locals[i].name, "a4") == 0) {
                e1(0x4C); e1(0x8B); e1(0x55); e1(locals[i].offset & 0xFF);  /* mov r10, [rbp+off] */
                break;
            }
        }
        /* r8 = a5 */
        for (i = 0; i < local_count; i++) {
            if (strcmp(locals[i].name, "a5") == 0) {
                e1(0x4C); e1(0x8B); e1(0x45); e1(locals[i].offset & 0xFF);  /* mov r8, [rbp+off] */
                break;
            }
        }
        /* r9 = a6 */
        for (i = 0; i < local_count; i++) {
            if (strcmp(locals[i].name, "a6") == 0) {
                e1(0x4C); e1(0x8B); e1(0x4D); e1(locals[i].offset & 0xFF);  /* mov r9, [rbp+off] */
                break;
            }
        }
        e1(0x0F); e1(0x05);  /* syscall */
        /* 结果在 rax — 存到局部变量 "ret" 的栈槽 */
        for (i = 0; i < local_count; i++) {
            if (strcmp(locals[i].name, "ret") == 0) {
                e1(0x48); e1(0x89); e1(0x45); e1(locals[i].offset & 0xFF);  /* mov [rbp+off], rax */
                break;
            }
        }
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

    /* mov $N, %%rax + syscall — 用于 rt_sigreturn 等直接 syscall */
    if (str_contains(t, "mov $") && str_contains(t, "%%rax") && str_contains(t, "syscall")) {
        /* 解析 $ 后的立即数 */
        const char *p = t;
        while (*p && *p != '$') p++;
        int imm = 0;
        if (*p == '$') {
            p++;
            while (*p >= '0' && *p <= '9') { imm = imm * 10 + (*p - '0'); p++; }
        }
        /* mov $imm, %rax → 48 C7 C0 imm32_le (sign-extended) */
        e1(0x48);          /* REX.W */
        e1(0xC7);          /* MOV r/m64, imm32 */
        e1(0xC0);          /* ModRM: mod=11, reg=0 (/0), r/m=0 (rax) */
        e4(imm);
        /* syscall → 0F 05 */
        e1(0x0F); e1(0x05);
        return;
    }

    /* 未知模板 */
    __printf("tcc: unknown inline asm template: \"%s\"\n", t);
}
