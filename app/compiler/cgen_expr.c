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
static void mov_rax_imm64(long long v) {
    e1(0x48); e1(0xB8);
    e1(v & 0xFF); e1((v>>8) & 0xFF); e1((v>>16) & 0xFF); e1((v>>24) & 0xFF);
    e1((v>>32) & 0xFF); e1((v>>40) & 0xFF); e1((v>>48) & 0xFF); e1((v>>56) & 0xFF);
}

/* ─── 加载/存储局部变量 [rbp+disp8] ─── */
/* mov eax, [rbp+disp8] */
static void load_eax_from_rbp(int disp8) {
    e1(0x8B); e1(0x45); e1(disp8 & 0xFF);
}

/* mov rax, [rbp+disp8] — 64-bit 加载 */
static void load_rax_from_rbp(int disp8) {
    e1(0x48); e1(0x8B); e1(0x45); e1(disp8 & 0xFF);
}

/* mov [rbp+disp8], eax — 32-bit 存储 */
static void store_eax_to_rbp(int disp8) {
    e1(0x89); e1(0x45); e1(disp8 & 0xFF);
}

/* mov [rbp+disp8], rax — 64-bit 存储 */
static void store_rax_to_rbp(int disp8) {
    e1(0x48); e1(0x89); e1(0x45); e1(disp8 & 0xFF);
}

/* ─── SSE 浮点辅助 ─── */

/* 将 double 立即数加载到 xmm0: mov rax, imm64; movq xmm0, rax */
static void load_double_imm(double d) {
    union { double d; unsigned long long u; } u;
    u.d = d;
    unsigned long long v = u.u;
    e1(0x48); e1(0xB8);           /* mov rax, imm64 */
    e1(v & 0xFF); e1((v >> 8) & 0xFF);
    e1((v >> 16) & 0xFF); e1((v >> 24) & 0xFF);
    e1((v >> 32) & 0xFF); e1((v >> 40) & 0xFF);
    e1((v >> 48) & 0xFF); e1((v >> 56) & 0xFF);
    e1(0x66); e1(0x48); e1(0x0F); e1(0x6E); e1(0xC0);  /* movq xmm0, rax */
}

/* movsd xmm0, [rbp+disp8] — 从局部变量加载 double */
static void load_double_from_rbp(int disp8) {
    e1(0xF2); e1(0x0F); e1(0x10); e1(0x45); e1(disp8 & 0xFF);
}

/* movsd [rbp+disp8], xmm0 — 存储 double 到局部变量 */
static void store_double_to_rbp(int disp8) {
    e1(0xF2); e1(0x0F); e1(0x11); e1(0x45); e1(disp8 & 0xFF);
}

/* 将 xmm0（double）保存到栈顶：sub rsp,8; movsd [rsp],xmm0 */
static void push_xmm0(void) {
    e1(0x48); e1(0x83); e1(0xEC); e1(0x08);     /* sub rsp, 8 */
    e1(0xF2); e1(0x0F); e1(0x11); e1(0x04); e1(0x24);  /* movsd [rsp], xmm0 */
}

/* 从栈顶恢复到 xmm1：movsd xmm1,[rsp]; add rsp,8 */
static void pop_xmm1(void) {
    e1(0xF2); e1(0x0F); e1(0x10); e1(0x0C); e1(0x24);  /* movsd xmm1, [rsp] */
    e1(0x48); e1(0x83); e1(0xC4); e1(0x08);     /* add rsp, 8 */
}

/* 从栈顶恢复到 xmm0：movsd xmm0,[rsp]; add rsp,8 */
static void pop_xmm0(void) {
    e1(0xF2); e1(0x0F); e1(0x10); e1(0x04); e1(0x24);  /* movsd xmm0, [rsp] */
    e1(0x48); e1(0x83); e1(0xC4); e1(0x08);     /* add rsp, 8 */
}

/* int→double: cvtsi2sd xmm0, eax */
static void cvti2d(void) {
    e1(0xF2); e1(0x0F); e1(0x2A); e1(0xC0);
}

/* movapd xmm1, xmm0 — 保存 xmm0 */
static void save_xmm0_to_xmm1(void) {
    e1(0x66); e1(0x0F); e1(0x28); e1(0xC8);
}

/* movapd xmm0, xmm1 — 恢复 xmm1 到 xmm0 */
static void restore_xmm1_to_xmm0(void) {
    e1(0x66); e1(0x0F); e1(0x28); e1(0xC1);
}

/* 双精度取负：翻转符号位 (xorpd xmm0, sign_mask) */
static void negate_double(void) {
    /* mov rax, 0x8000000000000000; movq xmm1, rax; xorpd xmm0, xmm1 */
    e1(0x48); e1(0xB8);
    e1(0x00); e1(0x00); e1(0x00); e1(0x00);
    e1(0x00); e1(0x00); e1(0x00); e1(0x80);  /* 0x8000000000000000 LE */
    e1(0x66); e1(0x48); e1(0x0F); e1(0x6E); e1(0xC8);  /* movq xmm1, rax */
    e1(0x66); e1(0x0F); e1(0x57); e1(0xC1);            /* xorpd xmm0, xmm1 */
}

/* ─── 二元运算（ecx/rcx OP eax/rax → eax/rax） ─── */
/* 32-bit 版本 */
static void binop_add(void) { e1(0x01); e1(0xC8); }  /* add eax, ecx */
static void binop_sub_swapped(void) {
    e1(0x91); e1(0x29); e1(0xC8);  /* xchg eax,ecx; sub eax,ecx — eax=left-right */
}
static void binop_mul(void) { e1(0x0F); e1(0xAF); e1(0xC1); }  /* imul eax, ecx */
static void binop_div(void) { e1(0x91); e1(0x99); e1(0xF7); e1(0xF9); }  /* xchg; cdq; idiv ecx */
static void binop_mod(void) { binop_div(); e1(0x89); e1(0xD0); }  /* div; mov eax,edx */
static void binop_and(void) { e1(0x21); e1(0xC8); }  /* and eax, ecx */
static void binop_or(void)  { e1(0x09); e1(0xC8); }  /* or eax, ecx */
static void binop_xor(void) { e1(0x31); e1(0xC8); }  /* xor eax, ecx */
static void binop_shl(void) { e1(0x91); e1(0xD3); e1(0xE0); }  /* xchg eax,ecx; shl eax, cl */
static void binop_shr(void) { e1(0x91); e1(0xD3); e1(0xE8); }  /* xchg eax,ecx; shr eax, cl */
static void binop_cmp(int setcc_opcode) {
    e1(0x39); e1(0xC1);           /* cmp ecx, eax */
    e1(0x0F); e1(setcc_opcode); e1(0xC0);  /* setcc al */
    e1(0x0F); e1(0xB6); e1(0xC0); /* movzx eax, al */
}

/* 64-bit 版本（带 REX.W 前缀） */
static void binop_add64(void) { e1(0x48); e1(0x01); e1(0xC8); }  /* add rax, rcx */
static void binop_sub_swapped64(void) {
    e1(0x48); e1(0x91);           /* xchg rax, rcx */
    e1(0x48); e1(0x29); e1(0xC8); /* sub rax, rcx */
}
static void binop_mul64(void) { e1(0x48); e1(0x0F); e1(0xAF); e1(0xC1); }  /* imul rax, rcx */
static void binop_div64(void) {
    e1(0x48); e1(0x91);           /* xchg rax, rcx */
    e1(0x48); e1(0x99);           /* cqo (rax→rdx:rax sign-extend) */
    e1(0x48); e1(0xF7); e1(0xF9); /* idiv rcx */
}
static void binop_mod64(void) { binop_div64(); e1(0x48); e1(0x89); e1(0xD0); }  /* mov rax, rdx */
static void binop_and64(void) { e1(0x48); e1(0x21); e1(0xC8); }  /* and rax, rcx */
static void binop_or64(void)  { e1(0x48); e1(0x09); e1(0xC8); }  /* or rax, rcx */
static void binop_xor64(void) { e1(0x48); e1(0x31); e1(0xC8); }  /* xor rax, rcx */
static void binop_shl64(void) { e1(0x48); e1(0x91); e1(0x48); e1(0xD3); e1(0xE0); }  /* xchg; shl rax, cl */
static void binop_shr64(void) { e1(0x48); e1(0x91); e1(0x48); e1(0xD3); e1(0xE8); }  /* xchg; shr rax, cl */
static void binop_cmp64(int setcc_opcode) {
    e1(0x48); e1(0x39); e1(0xC1); /* cmp rcx, rax */
    e1(0x0F); e1(setcc_opcode); e1(0xC0);  /* setcc al */
    e1(0x0F); e1(0xB6); e1(0xC0); /* movzx eax, al */
}

/* ─── 一元运算（eax/rax OP → eax/rax） ─── */

static void unop_neg(void) { e1(0xF7); e1(0xD8); }  /* neg eax */
static void unop_not(void) { e1(0xF7); e1(0xD0); }  /* not eax */
static void unop_neg64(void) { e1(0x48); e1(0xF7); e1(0xD8); }  /* neg rax */
static void unop_not64(void) { e1(0x48); e1(0xF7); e1(0xD0); }  /* not rax */
/* ─── 函数调用 ─── */

static void emit_call(const char *name) {
    if (!name) return;
    /* 通过符号表查找函数地址。对于 .o 文件，使用 R_X86_64_PLT32 或 R_X86_64_PC32 重定位 */
    /* 先找到或创建符号 */
    int sym_idx = -1;
    int i;
    for (i = 0; i < sym_count; i++) {
        if (syms[i].name && strcmp(syms[i].name, name) == 0) { sym_idx = i; break; }
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
        s->shndx = 0;       /* SHN_UNDEF — 外部符号 */
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

/* ─── 左值地址计算（用于 ++/--） ─── */

/* 计算左值的内存地址到 rax 中，支持 AST_VAR、AST_MEMBER、*ptr */
static void cgen_addr(AstNode *node) {
    if (!node) return;

    switch (node->kind) {
    case AST_VAR: {
        /* 局部变量：lea rax, [rbp+off] */
        int i;
        for (i = local_count - 1; i >= 0; i--) {
            if (strcmp(locals[i].name, node->name) == 0 &&
                locals[i].scope_depth <= scope_depth) {
                e1(0x48); e1(0x8D); e1(0x45); e1(locals[i].offset & 0xFF);
                return;
            }
        }
        /* 全局变量：lea rax, [rip+disp32] */
        if (node->name && *node->name) {
            int si = -1;
            for (i = 0; i < sym_count; i++) {
                if (syms[i].name && strcmp(syms[i].name, node->name) == 0) { si = i; break; }
            }
            if (si < 0 && sym_count < MAX_SYMS) {
                si = sym_count;
                CgenSym *s = &syms[sym_count++];
                s->name = node->name; s->offset = 0; s->size = 0;
                s->is_global = 1; s->is_func = 0;
                s->shndx = 0; s->sym_idx = -1;
            }
            if (si >= 0) {
                e1(0x48); e1(0x8D); e1(0x05);
                int ro = code_size; e4(0);
                if (rel_count < MAX_RELS) {
                    Elf64_Rela *r = &rels[rel_count++];
                    r->r_offset = ro;
                    r->r_info = ELF64_R_INFO(si + 1, R_X86_64_PC32);
                    r->r_addend = -4;
                }
            }
        }
        return;
    }
    case AST_BINOP:
        if (node->op == TOK_LBRACKET) {
            /* a[i] 的地址：计算 base + index * elem_size */
            cgen_expr(node->left);       /* 数组基地址 → rax */
            push_rax();
            cgen_expr(node->right);      /* 索引 → eax */
            pop_rcx();                    /* rcx = 基地址 */

            int elem_size = 1;
            int idx_is64 = (node->right && node->right->type_size == 8);
            if (node->left && node->left->kind == AST_VAR) {
                int i;
                for (i = local_count - 1; i >= 0; i--) {
                    if (strcmp(locals[i].name, node->left->name) == 0 &&
                        locals[i].scope_depth <= scope_depth) {
                        if (locals[i].element_size > 0)
                            elem_size = locals[i].element_size;
                        break;
                    }
                }
                if (i < 0) {
                    for (i = 0; i < sym_count; i++) {
                        if (syms[i].name && strcmp(syms[i].name, node->left->name) == 0) {
                            if (i < MAX_SYMS && global_elem_size[i] > 0)
                                elem_size = global_elem_size[i];
                            break;
                        }
                    }
                }
            }

            if (elem_size == 2) {
                if (idx_is64) { e1(0x48); e1(0xC1); e1(0xE0); e1(0x01); }
                else { e1(0xC1); e1(0xE0); e1(0x01); }
            } else if (elem_size == 4) {
                if (idx_is64) { e1(0x48); e1(0xC1); e1(0xE0); e1(0x02); }
                else { e1(0xC1); e1(0xE0); e1(0x02); }
            } else if (elem_size == 8) {
                if (idx_is64) { e1(0x48); e1(0xC1); e1(0xE0); e1(0x03); }
                else { e1(0xC1); e1(0xE0); e1(0x03); }
            } else if (elem_size == 16) {
                if (idx_is64) { e1(0x48); e1(0xC1); e1(0xE0); e1(0x04); }
                else { e1(0xC1); e1(0xE0); e1(0x04); }
            } else if (elem_size > 1) {
                /* 非 2 的幂：用 imul 乘 */
                e1(0x50);                            /* push rax (save index) */
                if (idx_is64) {
                    e1(0xB8); e4(elem_size); e4(0);   /* mov eax, elem_size (lower 32) */
                    /* Need to handle 64-bit elem_size too, but typical struct size is small */
                    e1(0x48); e1(0x0F); e1(0xAF); e1(0x04); e1(0x24);  /* imul rax, [rsp] */
                } else {
                    e1(0xB8); e4(elem_size);           /* mov eax, elem_size */
                    e1(0x0F); e1(0xAF); e1(0x04); e1(0x24);  /* imul eax, [rsp] */
                }
                e1(0x48); e1(0x83); e1(0xC4); e1(0x08);  /* add rsp, 8 (pop and discard, result in rax) */
            }
            e1(0x48); e1(0x01); e1(0xC8);  /* add rax, rcx */
            return;
        }
        break;
    case AST_MEMBER: {
        int moff = node->ival;
        if (node->op == TOK_DOT && node->left && node->left->kind == AST_VAR) {
            /* local_struct.member: lea rax, [rbp + base_off + member_off] */
            int i;
            for (i = local_count - 1; i >= 0; i--) {
                if (strcmp(locals[i].name, node->left->name) == 0 &&
                    locals[i].scope_depth <= scope_depth) {
                    int addr = locals[i].offset + moff;
                    e1(0x48); e1(0x8D); e1(0x45); e1(addr & 0xFF);
                    return;
                }
            }
        }
        /* p->member: 计算指针值，加上成员偏移 */
        if (node->op == TOK_ARROW) {
            cgen_expr(node->left);       /* rax = 指针值 */
            if (moff != 0) {
                push_rax();
                mov_eax_imm(moff);
                pop_rcx();
                e1(0x48); e1(0x01); e1(0xC8);  /* add rax, rcx */
            }
            return;
        }
        /* 非 AST_VAR 的 .member — 用 cgen_addr 取基地址（支持 a[i].member 等复合） */
        cgen_addr(node->left);
        if (moff != 0) {
            push_rax();
            mov_eax_imm(moff);
            pop_rcx();
            e1(0x48); e1(0x01); e1(0xC8);  /* add rax, rcx (64-bit) */
        }
        return;
    }
    case AST_UNARY:
        if (node->op == TOK_STAR) {
            /* *ptr: ptr 的值就是目标地址 */
            cgen_expr(node->expr);
            return;
        }
        break;
    default:
        break;
    }
    /* fallback：求值表达式，期望 rax 中为地址 */
    cgen_expr(node);
}

/* ─── 对外接口：表达式代码生成 ─── */

void cgen_expr(AstNode *node) {
    if (!node) return;

    switch (node->kind) {

    case AST_CONSTANT:
        if (node->is_float)
            load_double_imm(node->dval);
        else
            if (node->ival >= -2147483648LL && node->ival <= 2147483647LL) mov_eax_imm((int)node->ival); else mov_rax_imm64(node->ival);
        break;

    case AST_STRING: {
        /* 字符串字面量：追加到 strpool，创建 LOCAL 符号，发射 lea + 重定位 */
        node->type_size = 8;  /* 字符串字面量是指针 */
        const char *str = node->str_val;
        if (!str) { mov_eax_imm(0); break; }
        int len = 0;
        while (str[len]) len++;
        len++;  /* include null terminator */

        /* 追加到字符串池 */
        int pool_off = strpool_size;
        if (strpool_size + len > STRPOOL_SIZE) {
            __write(2, "tcc: string pool overflow\n", 26);
            __exit(1);
        }
        int i;
        for (i = 0; i < len; i++)
            strpool_buf[strpool_size++] = str[i];

        /* 在 syms[] 中创建 LOCAL 符号（必须早于后续 GLOBAL 创建，确保 ELF 顺序正确） */
        if (str_info_count >= MAX_STRINGS) {
            __write(2, "tcc: string info overflow\n", 26);
            __exit(1);
        }
        /* 先构建符号名 .LC%d */
        char name_buf[16];
        int si = str_info_count;
        char *np = name_buf;
        *np++ = '.'; *np++ = 'L'; *np++ = 'C';
        if (si >= 10000) *np++ = '0' + (si / 10000) % 10;
        if (si >= 1000)  *np++ = '0' + (si / 1000) % 10;
        if (si >= 100)   *np++ = '0' + (si / 100) % 10;
        if (si >= 10)    *np++ = '0' + (si / 10) % 10;
        *np++ = '0' + si % 10;
        *np = '\0';

        /* 用 str_infos 持久化符号名（name 数组在 elf_write 前一直有效） */
        int ni;
        for (ni = 0; ni < 16; ni++)
            str_infos[si].name[ni] = name_buf[ni];

        /* 添加 LOCAL 符号（offset 暂设为 0，后续在 cgen_program 中修正） */
        int sym_idx = -1;
        if (sym_count < MAX_SYMS) {
            sym_idx = sym_count++;
            syms[sym_idx].name = str_infos[si].name;
            syms[sym_idx].offset = 0;
            syms[sym_idx].size = len;
            syms[sym_idx].is_global = 0;
            syms[sym_idx].is_func = 0;
            syms[sym_idx].shndx = 1;  /* .text */
            syms[sym_idx].sym_idx = -1;
        }

        /* 记录字符串信息供后续偏移修正 */
        str_infos[si].pool_offset = pool_off;
        str_infos[si].len = len;
        str_infos[si].sym_index = sym_idx;
        str_info_count++;

        /* 发射 lea rax, [rip + disp32] */
        e1(0x48); e1(0x8D); e1(0x05);   /* lea rax, [rip + disp32] */
        int reloc_off = code_size;
        e4(0);                            /* disp32 占位（链接器覆盖） */

        /* 记录重定位（直接用 sym_idx + 1，此时 syms[] 顺序与 ELF 索引一致） */
        if (rel_count < MAX_RELS && sym_idx >= 0) {
            Elf64_Rela *r = &rels[rel_count++];
            r->r_offset = reloc_off;
            r->r_info = ELF64_R_INFO(sym_idx + 1, R_X86_64_PC32);
            r->r_addend = -4;
        }
        break;
    }

    case AST_VAR: {
        /* 查找局部变量偏移 — 从后往前搜索，用 scope_depth 过滤块作用域阴影 */
        int i;
        for (i = local_count - 1; i >= 0; i--) {
            if (strcmp(locals[i].name, node->name) == 0 &&
                locals[i].scope_depth <= scope_depth) {
                if (locals[i].is_float) {
                    node->is_float = 1;
                    node->type_size = 8;
                    load_double_from_rbp(locals[i].offset);
                } else if (node->is_float) {
                    /* 转型：int 变量被标记为 float（如 (double)i） */
                    load_eax_from_rbp(locals[i].offset);
                    cvti2d();
                } else {
                    node->type_size = locals[i].size;
                    if (locals[i].size > 8) {
                        /* 数组/大结构体：退化为指针（lea rax, [rbp+off]） */
                        e1(0x48); e1(0x8D); e1(0x45); e1(locals[i].offset & 0xFF);
                        node->type_size = 8;  /* 数组→指针衰减 */
                    } else if (locals[i].size == 8)
                        load_rax_from_rbp(locals[i].offset);
                    else
                        load_eax_from_rbp(locals[i].offset);
                }
                return;
            }
        }
        /* 未找到局部变量：当作全局或外部符号，生成带重定位的加载 */
        if (node->name && *node->name) {
            int si = -1;
            for (i = 0; i < sym_count; i++) {
                if (syms[i].name && strcmp(syms[i].name, node->name) == 0)
                    { si = i; break; }
            }
            if (si < 0 && sym_count < MAX_SYMS) {
                si = sym_count;
                CgenSym *s = &syms[sym_count++];
                s->name = node->name;
                s->offset = 0; s->size = 0;
                s->is_global = 1;
                s->is_func = 0;
                s->shndx = 0;  /* SHN_UNDEF — 外部符号 */
                s->sym_idx = -1;
            }
            if (si >= 0) {
                /* 全局变量：使用符号表记录的大小确定加载宽度 */
                int gsz = syms[si].size > 0 ? syms[si].size :
                          (node->type_size > 0 ? node->type_size : 4);
                if (gsz > 8) {
                    /* 数组/大结构体：数组→指针衰减（lea rax, [rip + disp32]） */
                    e1(0x48); e1(0x8D); e1(0x05);
                    node->type_size = 8;
                } else if (gsz == 8) {
                    e1(0x48); e1(0x8B); e1(0x05);  /* mov rax, [rip + disp32] */
                } else {
                    e1(0x8B); e1(0x05);             /* mov eax, [rip + disp32] */
                }
                int ro = code_size;
                e4(0);
                if (rel_count < MAX_RELS) {
                    Elf64_Rela *r = &rels[rel_count++];
                    r->r_offset = ro;
                    r->r_info = ELF64_R_INFO(si + 1, R_X86_64_PC32);
                    r->r_addend = -4;
                }
            } else {
                mov_eax_imm(0);
            }
        } else {
            mov_eax_imm(0);
        }
        break;
    }

    case AST_BINOP: {
        /* 数组下标 a[i] = *(a + i) — 支持指针运算 */
        if (node->op == TOK_LBRACKET) {
            cgen_expr(node->left);   /* 指针 → rax */
            push_rax();
            cgen_expr(node->right);  /* 索引 → eax */
            pop_rcx();               /* rcx = 指针 */

            /* 确定元素大小（默认 1 = char*） */
            int elem_size = 1;
            int idx_is64 = (node->right && node->right->type_size == 8);
            if (node->left && node->left->kind == AST_VAR) {
                int i;
                for (i = local_count - 1; i >= 0; i--) {
                    if (strcmp(locals[i].name, node->left->name) == 0 &&
                        locals[i].scope_depth <= scope_depth) {
                        if (locals[i].element_size > 0)
                            elem_size = locals[i].element_size;
                        break;
                    }
                }
                if (i < 0) {
                    for (i = 0; i < sym_count; i++) {
                        if (syms[i].name && strcmp(syms[i].name, node->left->name) == 0) {
                            if (i < MAX_SYMS && global_elem_size[i] > 0)
                                elem_size = global_elem_size[i];
                            break;
                        }
                    }
                }
            } else if (node->left && node->left->kind == AST_BINOP &&
                       node->left->op == TOK_LBRACKET &&
                       node->left->left && node->left->left->kind == AST_VAR) {
                /* 多维数组外层下标：使用 base_elem_size（内层元素大小） */
                int i;
                for (i = local_count - 1; i >= 0; i--) {
                    if (strcmp(locals[i].name, node->left->left->name) == 0 &&
                        locals[i].scope_depth <= scope_depth) {
                        if (locals[i].base_elem_size > 0)
                            elem_size = locals[i].base_elem_size;
                        else if (locals[i].element_size > 0)
                            elem_size = locals[i].element_size;
                        break;
                    }
                }
                if (i < 0) {
                    for (i = 0; i < sym_count; i++) {
                        if (syms[i].name && strcmp(syms[i].name, node->left->left->name) == 0) {
                            elem_size = 4;  /* 默认 int */
                            break;
                        }
                    }
                }
            }

            /* 索引 * 元素大小（移位加速），索引可能是 64-bit (size_t) */

            if (elem_size == 2) {
                if (idx_is64)
                    { e1(0x48); e1(0xC1); e1(0xE0); e1(0x01); }  /* shl rax, 1 */
                else
                    { e1(0xC1); e1(0xE0); e1(0x01); }             /* shl eax, 1 */
            } else if (elem_size == 4) {
                if (idx_is64)
                    { e1(0x48); e1(0xC1); e1(0xE0); e1(0x02); }  /* shl rax, 2 */
                else
                    { e1(0xC1); e1(0xE0); e1(0x02); }             /* shl eax, 2 */
            } else if (elem_size == 8) {
                if (idx_is64)
                    { e1(0x48); e1(0xC1); e1(0xE0); e1(0x03); }  /* shl rax, 3 */
                else
                    { e1(0xC1); e1(0xE0); e1(0x03); }             /* shl eax, 3 */
            } else if (elem_size == 16) {
                if (idx_is64)
                    { e1(0x48); e1(0xC1); e1(0xE0); e1(0x04); }  /* shl rax, 4 */
                else
                    { e1(0xC1); e1(0xE0); e1(0x04); }             /* shl eax, 4 */
            } else if (elem_size > 1) {
                /* 非 2 的幂：imul rax, rcx（需保存 rcx 中的基地址） */
                e1(0x50);                            /* push rax (save index) */
                if (idx_is64) {
                    e1(0xB8); e4(elem_size); e4(0);   /* mov eax, elem_size */
                    e1(0x48); e1(0x0F); e1(0xAF); e1(0x04); e1(0x24);  /* imul rax, [rsp] */
                } else {
                    e1(0xB8); e4(elem_size);           /* mov eax, elem_size */
                    e1(0x0F); e1(0xAF); e1(0x04); e1(0x24);  /* imul eax, [rsp] */
                }
                e1(0x48); e1(0x83); e1(0xC4); e1(0x08);  /* add rsp, 8 (discard saved index) */
            }

            /* ptr + offset → rax */
            e1(0x48); e1(0x01); e1(0xC8);  /* add rax, rcx */

            /* 按元素大小加载结果（elem_size > 8 表示子数组/结构体→不加载，退化为指针） */
            if (elem_size > 8) {
                /* 子数组/大结构体：不加载，rax 中已是指针 */
                node->type_size = 8;
            } else if (elem_size >= 8) {
                e1(0x48); e1(0x8B); e1(0x00);    /* mov rax, [rax] */
                node->type_size = elem_size;
            } else if (elem_size == 4) {
                e1(0x8B); e1(0x00);                   /* mov eax, [rax] */
                node->type_size = elem_size;
            } else {
                e1(0x0F); e1(0xBE); e1(0x00);          /* movsbl eax, [rax] */
                node->type_size = elem_size;
            }
            break;
        }

        if (node->op == TOK_AND_AND || node->op == TOK_OR_OR) {
            /* 短路求值：用条件跳转避免求值不必要的右操作数 */
            cgen_expr(node->left);
            e1(0x85); e1(0xC0);                          /* test eax, eax */

            int is_or = (node->op == TOK_OR_OR);
            int j1_pos = code_size;

            if (is_or) {
                /* a || b: a 为 true 则跳到 true_path */
                e1(0x0F); e1(0x85); e4(0);               /* jnz L_true */
            } else {
                /* a && b: a 为 false 则跳到 false_path */
                e1(0x0F); e1(0x84); e4(0);               /* jz L_false */
            }

            cgen_expr(node->right);
            e1(0x85); e1(0xC0);
            int j2_pos = code_size;

            if (is_or) {
                /* b 也为 true → 跳到 true_path */
                e1(0x0F); e1(0x85); e4(0);               /* jnz L_true */
            } else {
                /* b 为 false → 跳到 false_path */
                e1(0x0F); e1(0x84); e4(0);               /* jz L_false */
            }

            if (is_or) {
                /* a||b: 两个条件都假 → false_path */
                int false_pos = code_size;
                mov_eax_imm(0);
                int ep = code_size;
                e1(0xE9); e4(0);                          /* jmp L_end */
                int true_pos = code_size;
                /* 回填 j1 和 j2 到 true_pos */
                { int d = true_pos - (j1_pos + 6); code_buf[j1_pos+2]=d&0xFF; code_buf[j1_pos+3]=(d>>8)&0xFF;
                  code_buf[j1_pos+4]=(d>>16)&0xFF; code_buf[j1_pos+5]=(d>>24)&0xFF; }
                { int d = true_pos - (j2_pos + 6); code_buf[j2_pos+2]=d&0xFF; code_buf[j2_pos+3]=(d>>8)&0xFF;
                  code_buf[j2_pos+4]=(d>>16)&0xFF; code_buf[j2_pos+5]=(d>>24)&0xFF; }
                mov_eax_imm(1);
                /* 回填 ep */
                int end = code_size;
                { int d = end - (ep + 5); code_buf[ep+1]=d&0xFF; code_buf[ep+2]=(d>>8)&0xFF;
                  code_buf[ep+3]=(d>>16)&0xFF; code_buf[ep+4]=(d>>24)&0xFF; }
            } else {
                /* a&&b: 两个条件都真 → true_path（fallthrough），第一个假 → false_path */
                int true_pos = code_size;
                mov_eax_imm(1);
                int ep = code_size;
                e1(0xE9); e4(0);                          /* jmp L_end */
                int false_pos = code_size;
                /* 回填 j1 和 j2 到 false_pos */
                { int d = false_pos - (j1_pos + 6); code_buf[j1_pos+2]=d&0xFF; code_buf[j1_pos+3]=(d>>8)&0xFF;
                  code_buf[j1_pos+4]=(d>>16)&0xFF; code_buf[j1_pos+5]=(d>>24)&0xFF; }
                { int d = false_pos - (j2_pos + 6); code_buf[j2_pos+2]=d&0xFF; code_buf[j2_pos+3]=(d>>8)&0xFF;
                  code_buf[j2_pos+4]=(d>>16)&0xFF; code_buf[j2_pos+5]=(d>>24)&0xFF; }
                mov_eax_imm(0);
                /* 回填 ep */
                int end = code_size;
                { int d = end - (ep + 5); code_buf[ep+1]=d&0xFF; code_buf[ep+2]=(d>>8)&0xFF;
                  code_buf[ep+3]=(d>>16)&0xFF; code_buf[ep+4]=(d>>24)&0xFF; }
            }
            break;
        }

        /* 逗号运算符 */
        if (node->op == TOK_COMMA) {
            cgen_expr(node->left);
            if (node->is_float) {
                /* 左表达式的 double 结果需丢弃，但需要在栈上保存右结果 */
            }
            cgen_expr(node->right);
            break;
        }

        /* 检测是否浮点运算 */
        {
        int left_f  = node->left  && node->left->is_float;
        int right_f = node->right && node->right->is_float;

        if (left_f || right_f) {
            int is_cmp = (node->op == TOK_LESS || node->op == TOK_GREATER ||
                          node->op == TOK_LESS_EQ || node->op == TOK_GREATER_EQ ||
                          node->op == TOK_EQ_EQ || node->op == TOK_NOT_EQ);

            /* 浮点比较：用 ucomisd，结果始终是 int (eax=0/1) */
            if (is_cmp) {
                cgen_expr(node->left);
                if (!left_f) cvti2d();    /* 左操作数提升到 double */
                save_xmm0_to_xmm1();      /* xmm1 = left */
                cgen_expr(node->right);
                if (!right_f) cvti2d();   /* 右操作数提升到 double */
                /* ucomisd xmm1, xmm0 (xmm1 - xmm0) */
                e1(0x66); e1(0x0F); e1(0x2E); e1(0xC8);
                /* setcc al */
                switch (node->op) {
                case TOK_LESS:       e1(0x0F); e1(0x92); e1(0xC0); break;  /* setb */
                case TOK_LESS_EQ:    e1(0x0F); e1(0x96); e1(0xC0); break;  /* setbe */
                case TOK_GREATER:    e1(0x0F); e1(0x97); e1(0xC0); break;  /* seta */
                case TOK_GREATER_EQ: e1(0x0F); e1(0x93); e1(0xC0); break;  /* setae */
                case TOK_EQ_EQ:      e1(0x0F); e1(0x94); e1(0xC0); break;  /* sete */
                case TOK_NOT_EQ:     e1(0x0F); e1(0x95); e1(0xC0); break;  /* setne */
                default: break;
                }
                e1(0x0F); e1(0xB6); e1(0xC0);  /* movzx eax, al */
                node->is_float = 0;  /* 比较结果始终是整数 */
                break;
            }

            /* 浮点算术运算 */
            cgen_expr(node->left);
            if (!left_f) cvti2d();    /* 提升 int→double */
            push_xmm0();               /* 保存左操作数 */

            cgen_expr(node->right);
            if (!right_f) cvti2d();   /* 提升 int→double */
            pop_xmm1();                /* xmm1 = left, xmm0 = right */

            /* xmm1 = xmm1 OP xmm0 */
            switch (node->op) {
            case TOK_PLUS:  e1(0xF2); e1(0x0F); e1(0x58); e1(0xC8); break;  /* addsd */
            case TOK_MINUS: e1(0xF2); e1(0x0F); e1(0x5C); e1(0xC8); break;  /* subsd */
            case TOK_STAR:  e1(0xF2); e1(0x0F); e1(0x59); e1(0xC8); break;  /* mulsd */
            case TOK_SLASH: e1(0xF2); e1(0x0F); e1(0x5E); e1(0xC8); break;  /* divsd */
            default: break;
            }
            restore_xmm1_to_xmm0();    /* 结果→xmm0 */
            node->is_float = 1;
            break;
        }
        }

        /* 普通整数二元运算：左→栈，右→eax，出栈→ecx，计算 */
        cgen_expr(node->left);
        push_rax();
        cgen_expr(node->right);
        pop_rcx();  /* rcx = left, rax = right */

        /* 指针算术缩放：ptr + int 或 int + ptr 时，整数操作数乘以元素大小 */
        if ((node->op == TOK_PLUS || node->op == TOK_MINUS) &&
            ((node->left && node->left->type_size == 8 &&
              node->right && node->right->type_size <= 4) ||
             (node->right && node->right->type_size == 8 &&
              node->left && node->left->type_size <= 4))) {
            /* 找到指针操作数及其元素大小 */
            int ptelem = 1;
            AstNode *ptr_node = (node->left->type_size == 8) ? node->left : node->right;
            int right_is_ptr = (node->right->type_size == 8);
            if (ptr_node && ptr_node->kind == AST_VAR && ptr_node->name) {
                int vi;
                for (vi = local_count - 1; vi >= 0; vi--) {
                    if (strcmp(locals[vi].name, ptr_node->name) == 0 &&
                        locals[vi].scope_depth <= scope_depth) {
                        if (locals[vi].element_size > 0) ptelem = locals[vi].element_size;
                        break;
                    }
                }
            }
            if (ptelem > 1) {
                /* 缩放整数操作数（此后由下方 binop_add64/binop_sub... 完成加法） */
                if (right_is_ptr) {
                    /* left 是整数（在 rcx 中），right 是指针（在 rax 中） */
                    /* xchg 使指针在 rcx，整数在 rax：之后 binop 做 add rax,rcx 得到 ptr+scaled_int */
                    e1(0x48); e1(0x91);           /* xchg rax, rcx — rax=int, rcx=ptr */
                    if (ptelem == 2)      { e1(0xC1); e1(0xE0); e1(0x01); }      /* shl eax, 1 */
                    else if (ptelem == 4) { e1(0xC1); e1(0xE0); e1(0x02); }      /* shl eax, 2 */
                    else if (ptelem == 8) { e1(0x48); e1(0xC1); e1(0xE0); e1(0x03); }  /* shl rax, 3 */
                    else                  { e1(0x50); e1(0xB8); e4(ptelem);       /* push rax; mov eax, ptelem */
                                            e1(0x0F); e1(0xAF); e1(0x04); e1(0x24);  /* imul eax, [rsp] */
                                            e1(0x48); e1(0x83); e1(0xC4); e1(0x08); } /* add rsp, 8 */
                    /* rcx=ptr, rax=scaled_int → 下方 binop_add64 做 add rax,rcx 得到 ptr+scaled_int */
                } else {
                    /* right 是整数（在 rax 中），left 是指针（在 rcx 中） */
                    if (ptelem == 2)      { e1(0xC1); e1(0xE0); e1(0x01); }      /* shl eax, 1 */
                    else if (ptelem == 4) { e1(0xC1); e1(0xE0); e1(0x02); }      /* shl eax, 2 */
                    else if (ptelem == 8) { e1(0x48); e1(0xC1); e1(0xE0); e1(0x03); }  /* shl rax, 3 */
                    else                  { push_rax(); mov_eax_imm(ptelem);
                                            e1(0x0F); e1(0xAF); e1(0x04); e1(0x24);  /* imul eax, [rsp] */
                                            e1(0x48); e1(0x83); e1(0xC4); e1(0x08); }
                    /* rcx=ptr, rax=scaled_int → 下方 binop_add64 做 add rax,rcx 得到 ptr+scaled_int */
                }
            }
        }

        /* 判断是否需要 64-bit 运算（任一操作数为 64 位） */
        if (node->left && node->left->type_size == 8)
            { switch (node->op) {
            case TOK_PLUS:  binop_add64(); break;
            case TOK_MINUS: binop_sub_swapped64(); break;
            case TOK_STAR:  binop_mul64(); break;
            case TOK_SLASH: binop_div64(); break;
            case TOK_PERCENT: binop_mod64(); break;
            case TOK_LESS:       binop_cmp64(0x9C); break;
            case TOK_GREATER:    binop_cmp64(0x9F); break;
            case TOK_LESS_EQ:    binop_cmp64(0x9E); break;
            case TOK_GREATER_EQ: binop_cmp64(0x9D); break;
            case TOK_EQ_EQ:      binop_cmp64(0x94); break;
            case TOK_NOT_EQ:     binop_cmp64(0x95); break;
            case TOK_LESS_LESS:       binop_shl64(); break;
            case TOK_GREATER_GREATER: binop_shr64(); break;
            case TOK_AMPERSAND: binop_and64(); break;
            case TOK_PIPE:      binop_or64();  break;
            case TOK_CARET:     binop_xor64(); break;
            default: break;
            } }
        else if (node->right && node->right->type_size == 8)
            { switch (node->op) {
            case TOK_PLUS:  binop_add64(); break;
            case TOK_MINUS: binop_sub_swapped64(); break;
            case TOK_STAR:  binop_mul64(); break;
            case TOK_SLASH: binop_div64(); break;
            case TOK_PERCENT: binop_mod64(); break;
            case TOK_LESS:       binop_cmp64(0x9C); break;
            case TOK_GREATER:    binop_cmp64(0x9F); break;
            case TOK_LESS_EQ:    binop_cmp64(0x9E); break;
            case TOK_GREATER_EQ: binop_cmp64(0x9D); break;
            case TOK_EQ_EQ:      binop_cmp64(0x94); break;
            case TOK_NOT_EQ:     binop_cmp64(0x95); break;
            case TOK_LESS_LESS:       binop_shl64(); break;
            case TOK_GREATER_GREATER: binop_shr64(); break;
            case TOK_AMPERSAND: binop_and64(); break;
            case TOK_PIPE:      binop_or64();  break;
            case TOK_CARET:     binop_xor64(); break;
            default: break;
            } }
        else
            { switch (node->op) {
            case TOK_PLUS:  binop_add(); break;
            case TOK_MINUS: binop_sub_swapped(); break;
            case TOK_STAR:  binop_mul(); break;
            case TOK_SLASH: binop_div(); break;
            case TOK_PERCENT: binop_mod(); break;
            case TOK_LESS:       binop_cmp(0x9C); break;
            case TOK_GREATER:    binop_cmp(0x9F); break;
            case TOK_LESS_EQ:    binop_cmp(0x9E); break;
            case TOK_GREATER_EQ: binop_cmp(0x9D); break;
            case TOK_EQ_EQ:      binop_cmp(0x94); break;
            case TOK_NOT_EQ:     binop_cmp(0x95); break;
            case TOK_LESS_LESS:       binop_shl(); break;
            case TOK_GREATER_GREATER: binop_shr(); break;
            case TOK_AMPERSAND: binop_and(); break;
            case TOK_PIPE:      binop_or();  break;
            case TOK_CARET:     binop_xor(); break;
            default: break;
            } }

        /* 指针减法：q-p 结果需要除以元素大小（以元素个数为单位的差值） */
        if (node->op == TOK_MINUS &&
            node->left && node->left->type_size == 8 &&
            node->right && node->right->type_size == 8) {
            /* 查找指针元素大小 */
            int ptelem = 1;
            if (node->left->kind == AST_VAR && node->left->name) {
                int vi;
                for (vi = local_count - 1; vi >= 0; vi--) {
                    if (strcmp(locals[vi].name, node->left->name) == 0 &&
                        locals[vi].scope_depth <= scope_depth) {
                        if (locals[vi].element_size > 0) ptelem = locals[vi].element_size;
                        break;
                    }
                }
            }
            if (ptelem > 1) {
                /* 用 imul 取倒数不可行，用 idiv：eax 中已有差值，除以 ptelem */
                /* 差值已在 eax（从 64-bit 减法后的 32-bit 截断） */
                /* 正确做法：用 64-bit 差值 */
                /* 先将差值从 eax 符号扩展到 edx:eax */
                e1(0x99);                          /* cdq: sign-extend eax→edx:eax */
                e1(0xB9); e4(ptelem); e1(0xF7); e1(0xF9);  /* mov ecx, ptelem; idiv ecx */
            }
        }
        break;
    }

    case AST_UNARY: {
        cgen_expr(node->expr);  /* 子表达式结果在 eax 或 xmm0 */
        switch (node->op) {
        case TOK_MINUS:
            if (node->expr && node->expr->is_float) {
                negate_double();
                node->is_float = 1;
            } else if (node->expr && node->expr->type_size == 8) {
                unop_neg64();
            } else {
                unop_neg();
            }
            break;
        case TOK_TILDE:
            if (node->expr && node->expr->type_size == 8)
                unop_not64();
            else
                unop_not();
            break;
        case TOK_EXCLAM:
            if (node->expr && node->expr->is_float) {
                /* !double_val: 与 0.0 比较 */
                /* xorpd xmm1, xmm1 (xmm1=0.0); ucomisd xmm1, xmm0; sete al; movzx */
                e1(0x66); e1(0x0F); e1(0x57); e1(0xC9);  /* xorpd xmm1, xmm1 */
                e1(0x66); e1(0x0F); e1(0x2E); e1(0xC8);  /* ucomisd xmm1, xmm0 */
                e1(0x0F); e1(0x94); e1(0xC0);            /* sete al */
                e1(0x0F); e1(0xB6); e1(0xC0);            /* movzx eax, al */
            } else {
                /* !x: cmp eax, 0; sete al; movzx eax, al */
                e1(0x85); e1(0xC0);           /* test eax, eax */
                e1(0x0F); e1(0x94); e1(0xC0); /* sete al */
                e1(0x0F); e1(0xB6); e1(0xC0); /* movzx eax, al */
            }
            break;
        case TOK_PLUS_PLUS:
        case TOK_MINUS_MINUS: {
            /* ++/-- : 计算地址 → 加载值 → 增减 → 写回
             * 用 cgen_addr 统一处理 local/global/struct-member/*ptr */
            int sz = (node->expr && node->expr->type_size == 8) ? 8 : 4;
            /* 后缀(Postfix)：保存旧值，返回旧值
             * 前缀(Prefix)：返回新值（当前行为） */
            cgen_addr(node->expr);       /* rax = 目标地址 */
            push_rax();                  /* 保存地址 */
            pop_rcx();                   /* rcx = 地址 */
            if (node->is_postfix) {
                /* 保存旧值 */
                if (sz == 8) {
                    e1(0x48); e1(0x8B); e1(0x01);  /* mov rax, [rcx] */
                } else {
                    e1(0x8B); e1(0x01);             /* mov eax, [rcx] */
                }
                push_rax();              /* 保存旧值到栈 */
            }
            /* 加载、增减、写回 */
            if (sz == 8) {
                e1(0x48); e1(0x8B); e1(0x01);  /* mov rax, [rcx] */
                if (node->op == TOK_PLUS_PLUS)
                    { e1(0x48); e1(0x83); e1(0xC0); e1(0x01); }  /* add rax, 1 */
                else
                    { e1(0x48); e1(0x83); e1(0xE8); e1(0x01); }  /* sub rax, 1 */
                e1(0x48); e1(0x89); e1(0x01);  /* mov [rcx], rax */
            } else {
                e1(0x8B); e1(0x01);             /* mov eax, [rcx] */
                if (node->op == TOK_PLUS_PLUS)
                    { e1(0x83); e1(0xC0); e1(0x01); }  /* add eax, 1 */
                else
                    { e1(0x83); e1(0xE8); e1(0x01); }  /* sub eax, 1 */
                e1(0x89); e1(0x01);             /* mov [rcx], eax */
            }
            if (node->is_postfix)
                pop_rax();  /* eax/rax = 旧值 */
            node->type_size = sz;
            break;
        }
        case TOK_STAR:
            /* *ptr — 从指针地址加载值 */
            if (node->expr) {
                /* 推测被指向的类型大小。
                 * 在局部变量表中查找指针变量的 element_size：
                 * int * → 4, char * → 1, long * / double * → 8 */
                int deref_size = 1;  /* 默认 char* 解引用 */
                if (node->expr->kind == AST_VAR && node->expr->name) {
                    int vi;
                    for (vi = local_count - 1; vi >= 0; vi--) {
                        if (strcmp(locals[vi].name, node->expr->name) == 0 &&
                            locals[vi].scope_depth <= scope_depth) {
                            if (locals[vi].element_size > 0)
                                deref_size = locals[vi].element_size;
                            break;
                        }
                    }
                }
                if (deref_size == 1) {
                    /* movsbl (%rax), %eax — 字节加载符号扩展 */
                    e1(0x0F); e1(0xBE); e1(0x00);
                } else if (deref_size == 8) {
                    /* mov rax, [rax] — 64-bit 加载 */
                    e1(0x48); e1(0x8B); e1(0x00);
                } else if (deref_size == 2) {
                    /* movswl (%rax), %eax — 字加载符号扩展 */
                    e1(0x0F); e1(0xBF); e1(0x00);
                } else {
                    /* mov (%rax), %eax — 32-bit 加载 */
                    e1(0x8B); e1(0x00);
                }
                node->type_size = deref_size;
            }
            break;
        case TOK_AMPERSAND:
            node->type_size = 8;  /* & 产生指针 */
            if (node->expr && node->expr->kind == AST_VAR) {
                int found = 0;
                int i;
                for (i = local_count - 1; i >= 0; i--) {
                    if (strcmp(locals[i].name, node->expr->name) == 0 &&
                        locals[i].scope_depth <= scope_depth) {
                        e1(0x48); e1(0x8D); e1(0x45); e1(locals[i].offset & 0xFF);  /* lea rax, [rbp+off] */
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
                        s->is_global = 1;
                        s->is_func = 0;  /* 数据符号（不确定类型时保守设 0） */
                        s->shndx = 0;    /* SHN_UNDEF — 外部符号 */
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
            } else if (node->expr) {
                /* &arr[i], &s.member, &*ptr — 用 cgen_addr 计算地址 */
                cgen_addr(node->expr);
            }
            break;
        default: break;
        }
        break;
    }

    case AST_ASSIGN: {
        int rhs_float = node->right && node->right->is_float;
        int rhs_size = node->right ? node->right->type_size : 4;

        /* 数组下标赋值 a[i] = expr — 先计算地址，再求值右操作数并存储 */
        if (node->left && node->left->kind == AST_BINOP &&
            node->left->op == TOK_LBRACKET) {
            /* 计算左地址：指针 + 索引 * 元素大小 */
            cgen_expr(node->left->left);     /* 指针 → rax */
            push_rax();
            cgen_expr(node->left->right);    /* 索引 → eax */
            pop_rcx();                        /* rcx = 指针 */
            int elem_size = 1;
            int idx_is64 = (node->left->right && node->left->right->type_size == 8);
            AstNode *arr_base = node->left->left;
            if (arr_base && arr_base->kind == AST_VAR) {
                int i;
                for (i = local_count - 1; i >= 0; i--) {
                    if (strcmp(locals[i].name, arr_base->name) == 0 &&
                        locals[i].scope_depth <= scope_depth) {
                        if (locals[i].element_size > 0)
                            elem_size = locals[i].element_size;
                        break;
                    }
                }
                if (i < 0) {
                    for (i = 0; i < sym_count; i++) {
                        if (syms[i].name && strcmp(syms[i].name, arr_base->name) == 0) {
                            if (i < MAX_SYMS && global_elem_size[i] > 0)
                                elem_size = global_elem_size[i];
                            break;
                        }
                    }
                }
            } else if (arr_base && arr_base->kind == AST_BINOP &&
                       arr_base->op == TOK_LBRACKET &&
                       arr_base->left && arr_base->left->kind == AST_VAR) {
                /* 多维数组外层下标：使用 base_elem_size */
                int i;
                for (i = local_count - 1; i >= 0; i--) {
                    if (strcmp(locals[i].name, arr_base->left->name) == 0 &&
                        locals[i].scope_depth <= scope_depth) {
                        if (locals[i].base_elem_size > 0)
                            elem_size = locals[i].base_elem_size;
                        else if (locals[i].element_size > 0)
                            elem_size = locals[i].element_size;
                        break;
                    }
                }
            }
            if (elem_size == 2) {
                if (idx_is64)
                    { e1(0x48); e1(0xC1); e1(0xE0); e1(0x01); }
                else
                    { e1(0xC1); e1(0xE0); e1(0x01); }
            } else if (elem_size == 4) {
                if (idx_is64)
                    { e1(0x48); e1(0xC1); e1(0xE0); e1(0x02); }
                else
                    { e1(0xC1); e1(0xE0); e1(0x02); }
            } else if (elem_size == 8) {
                if (idx_is64)
                    { e1(0x48); e1(0xC1); e1(0xE0); e1(0x03); }
                else
                    { e1(0xC1); e1(0xE0); e1(0x03); }
            } else if (elem_size == 16) {
                if (idx_is64)
                    { e1(0x48); e1(0xC1); e1(0xE0); e1(0x04); }
                else
                    { e1(0xC1); e1(0xE0); e1(0x04); }
            } else if (elem_size > 1) {
                e1(0x50);                            /* push rax */
                if (idx_is64) {
                    e1(0xB8); e4(elem_size); e4(0);
                    e1(0x48); e1(0x0F); e1(0xAF); e1(0x04); e1(0x24);
                } else {
                    e1(0xB8); e4(elem_size);
                    e1(0x0F); e1(0xAF); e1(0x04); e1(0x24);
                }
                e1(0x48); e1(0x83); e1(0xC4); e1(0x08);
            }
            e1(0x48); e1(0x01); e1(0xC8);  /* add rax, rcx → 目标地址 */
            push_rax();                      /* 保存目标地址 */

            /* 求值右操作数 */
            cgen_expr(node->right);

            /* 存储到目标地址 — cgen_expr 可能更新了 node->right->type_size，重新读取 */
            pop_rcx();  /* rcx = 目标地址 */
            int rhs_sz_after = node->right ? node->right->type_size : 4;
            if (rhs_float) {
                /* movsd [rcx], xmm0 */
                e1(0xF2); e1(0x0F); e1(0x11); e1(0x01);
            } else if (rhs_sz_after >= 8) {
                e1(0x48); e1(0x89); e1(0x01);  /* mov [rcx], rax */
            } else {
                e1(0x89); e1(0x01);             /* mov [rcx], eax */
            }
            node->type_size = rhs_sz_after;
            break;
        }

        /* 指针解引用赋值 *ptr = expr */
        if (node->left && node->left->kind == AST_UNARY &&
            node->left->op == TOK_STAR) {
            cgen_expr(node->left->expr);       /* 指针 → rax */
            push_rax();
            cgen_expr(node->right);
            pop_rcx();                          /* rcx = 目标地址 */
            /* cgen_expr 已更新 node->right->type_size，重新读取 */
            int rhs_sz_deref = node->right ? node->right->type_size : 4;
            if (rhs_float) {
                e1(0xF2); e1(0x0F); e1(0x11); e1(0x01);  /* movsd [rcx], xmm0 */
            } else if (rhs_sz_deref >= 8) {
                e1(0x48); e1(0x89); e1(0x01);  /* mov [rcx], rax */
            } else {
                e1(0x89); e1(0x01);             /* mov [rcx], eax */
            }
            break;
        }

        cgen_expr(node->right);
        if (node->left && node->left->kind == AST_VAR) {
            const char *vname = node->left->name;
            int i;
            for (i = local_count - 1; i >= 0; i--) {
                if (strcmp(locals[i].name, vname) == 0 &&
                    locals[i].scope_depth <= scope_depth) {
                    if (locals[i].is_float) {
                        /* 右操作数可能是 int，需要转换 */
                        if (!rhs_float) cvti2d();
                        store_double_to_rbp(locals[i].offset);
                    } else {
                        /* 右操作数可能是 double，需要转换 */
                        if (rhs_float) {
                            /* cvttsd2si eax, xmm0 */
                            e1(0xF2); e1(0x0F); e1(0x2C); e1(0xC0);
                        }
                        if (locals[i].size == 8)
                            store_rax_to_rbp(locals[i].offset);
                        else
                            store_eax_to_rbp(locals[i].offset);
                    }
                    break;
                }
            }
            /* 全局变量赋值 — 用重定位生成 mov [rip+disp32], rax/eax */
            if (i < 0 && vname && *vname) {
                int si = -1;
                for (i = 0; i < sym_count; i++) {
                    if (syms[i].name && strcmp(syms[i].name, vname) == 0)
                        { si = i; break; }
                }
                if (si < 0 && sym_count < MAX_SYMS) {
                    si = sym_count;
                    CgenSym *s = &syms[sym_count++];
                    s->name = vname;
                    s->offset = 0; s->size = 0;
                    s->is_global = 1;
                    s->is_func = 0;
                    s->shndx = 0;  /* SHN_UNDEF */
                    s->sym_idx = -1;
                }
                if (si >= 0) {
                    /* 使用变量的声明大小，而非仅 rhs_size */
                    int var_size = syms[si].size;
                    int size8 = (rhs_size == 8 ||
                                 (node->right && node->right->type_size == 8) ||
                                 var_size == 8);
                    if (size8) {
                        e1(0x48); e1(0x89); e1(0x05);  /* mov [rip+disp32], rax */
                    } else {
                        e1(0x89); e1(0x05);             /* mov [rip+disp32], eax */
                    }
                    int ro = code_size;
                    e4(0);
                    if (rel_count < MAX_RELS) {
                        Elf64_Rela *r = &rels[rel_count++];
                        r->r_offset = ro;
                        r->r_info = ELF64_R_INFO(si + 1, R_X86_64_PC32);
                        r->r_addend = -4;
                    }
                }
            }
        } else if (node->left && node->left->kind == AST_MEMBER) {
            /* s.member = expr 或 p->member = expr
             * 注意：此时 eax 中已有 RHS 值（来自上方公共 cgen_expr），
             * 需先保存再计算地址（cgen_addr 会覆写 eax）。
             * 存储宽度用成员类型而非 RHS 类型（long 成员赋 int 常量时用 8 字节）。 */
            int rsize = node->left->type_size > 0 ? node->left->type_size :
                        (node->right ? node->right->type_size : 4);
            if (rsize == 0) rsize = 8;

            push_rax();              /* 保存 RHS 值 */

            cgen_addr(node->left);   /* rax = 成员地址（覆写 eax） */

            e1(0x48); e1(0x89); e1(0xC1);  /* mov rcx, rax (rcx = 地址) */
            pop_rax();               /* rax = RHS 值 */

            if (rsize >= 8) {
                e1(0x48); e1(0x89); e1(0x01);  /* mov [rcx], rax */
            } else {
                e1(0x89); e1(0x01);             /* mov [rcx], eax */
            }
            node->type_size = rsize;
            break;
        }
        break;
    }

    case AST_CALL: {
        int argc = 0;
        AstNode *arg = node->args;
        while (arg) { argc++; arg = arg->next; }
        /* 根据 x86_64 ABI 限制已消除 — 超出 6 个的参数通过栈传递 */
        if (argc > 14) argc = 14;  /* 硬上限防止内部缓冲区溢出 */

        /* 记录各实参的类型 */
        int arg_is_float[16] = {0};
        { AstNode *a = node->args; int ai = 0;
          while (a && ai < 16) {
              arg_is_float[ai] = (a->is_float != 0);
              ai++; a = a->next;
          }
        }

        /* 处理 __builtin_va_* （始终使用整数路径） */
        if (node->name && node->name[0] == '_' && node->name[1] == '_') {
            if (strcmp(node->name, "__builtin_va_start") == 0 && node->args) {
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
                pop_rcx();
                e1(0xC7); e1(0x01); e4(func_nparams * 8);
                e1(0xC7); e1(0x41); e1(0x04); e4(48);
                e1(0x48); e1(0x8D); e1(0x45); e1(0x10);
                e1(0x48); e1(0x89); e1(0x41); e1(0x08);
                e1(0x48); e1(0x89); e1(0xE0);
                e1(0x48); e1(0x89); e1(0x41); e1(0x10);
                break;
            }
            if (strcmp(node->name, "__builtin_va_arg") == 0 && node->args) {
                /* 获取类型大小（第二个参数），默认 4 */
                int type_size = 4;
                if (node->args->next && node->args->next->kind == AST_CONSTANT)
                    type_size = node->args->next->ival;
                /* 默认参数提升：小于 4 升到 4，大于 8 截到 8 */
                if (type_size < 4) type_size = 4;
                if (type_size > 8) type_size = 8;

                cgen_expr(node->args);              /* rax = &ap */
                e1(0x48); e1(0x89); e1(0xC7);       /* mov rdi, rax — 保存 &ap */
                e1(0x48); e1(0x8B); e1(0x47); e1(0x10); /* mov rax, [rdi+0x10] — reg_save_area（64 位指针） */
                e1(0x8B); e1(0x0F);                 /* mov ecx, [rdi] — gp_offset */
                /* 从 reg_save_area[gp_offset] 读值 */
                if (type_size >= 8) {
                    e1(0x48); e1(0x8B); e1(0x04); e1(0x08); /* mov rax, [rax+rcx]（64 位） */
                } else {
                    e1(0x8B); e1(0x04); e1(0x08);            /* mov eax, [rax+rcx]（32 位） */
                }
                /* 更新 gp_offset */
                e1(0x83); e1(0x07); e1(type_size);  /* add dword [rdi], type_size */
                break;
            }
            if (strcmp(node->name, "__builtin_va_end") == 0) {
                break;
            }
        }

        /* 检查是否为函数指针调用 */
        int is_fptr = 0;
        int fptr_offset = 0;
        if (node->name) {
            int i;
            for (i = local_count - 1; i >= 0; i--) {
                if (strcmp(locals[i].name, node->name) == 0 &&
                    locals[i].scope_depth <= scope_depth) {
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

        /* 求值参数：float 用 push_xmm0，int 用 push_rax */
        arg = node->args;
        int ai;
        for (ai = 0; ai < argc; ai++) {
            if (!arg) break;
            cgen_expr(arg);
            if (arg_is_float[ai])
                push_xmm0();
            else
                push_rax();
            arg = arg->next;
        }

        /* 参数入寄存器（逆序） */
        /* 收集各参数的类型大小 */
        int arg_sizes[16] = {0};
        { AstNode *a = node->args; int asi = 0;
          while (a && asi < 16) {
              arg_sizes[asi] = a->type_size;
              asi++; a = a->next;
          }
        }

        for (ai = argc - 1; ai >= 0; ai--) {
            /* 7th+ args stay on stack (x86_64 ABI) */
            if (ai >= 6) continue;
            if (arg_is_float[ai]) {
                /* 从栈弹到 xmm[ai] */
                e1(0xF2); e1(0x0F); e1(0x10);
                e1(0x04 | ((ai & 7) << 3)); e1(0x24);  /* movsd xmmN, [rsp] */
                e1(0x48); e1(0x83); e1(0xC4); e1(0x08); /* add rsp, 8 */
            } else if (ai == 3) {
                /* arg3 在 rcx 中 — 弹出到 rcx 不覆写后续操作 */
                pop_rcx();
            } else {
                /* 弹出到 rax 再移动到目标寄存器，避免覆写 rcx */
                pop_rax();
                int use64 = 1;  /* 总用 64 位传参，防 type_size 丢失导致指针截断 */
                switch (ai) {
                case 0:
                    if (use64) { e1(0x48); e1(0x89); e1(0xC7); }
                    else { e1(0x89); e1(0xC7); }
                    break;
                case 1:
                    if (use64) { e1(0x48); e1(0x89); e1(0xC6); }
                    else { e1(0x89); e1(0xC6); }
                    break;
                case 2:
                    if (use64) { e1(0x48); e1(0x89); e1(0xC2); }
                    else { e1(0x89); e1(0xC2); }
                    break;
                case 4:
                    if (use64) { e1(0x49); e1(0x89); e1(0xC0); }
                    else { e1(0x41); e1(0x89); e1(0xC0); }
                    break;
                case 5:
                    if (use64) { e1(0x49); e1(0x89); e1(0xC1); }
                    else { e1(0x41); e1(0x89); e1(0xC1); }
                    break;
                }
            }
        }

        if (is_fptr) {
            pop_rcx();
            e1(0xFF); e1(0xD1); /* call *rcx */
        } else {
            emit_call(node->name);
        }
        /* 清理栈上传参（7th+ 参数被调用者未清理） */
        if (argc > 6) {
            int stack_args = argc - 6;
            e1(0x48); e1(0x83); e1(0xC4); e1(stack_args * 8);  /* add rsp, N */
        }
        /* 若调用返回 double，标记节点 */
        if (node->is_float)
            ;  /* 结果已在 xmm0 中 */
        else
            node->type_size = 8;  /* x86-64 ABI: rax 中总是 64 位 */
        break;
    }

    case AST_IF: {
        /* 三元运算符 a ? b : c（作为表达式求值）
         * 策略：两个分支都用栈保存结果，最后统一弹出 */
        int is_f = node->is_float ||
                   (node->then_stmt && node->then_stmt->is_float) ||
                   (node->else_stmt && node->else_stmt->is_float);

        /* 条件求值→eax */
        cgen_expr(node->cond);
        e1(0x85); e1(0xC0);              /* test eax, eax */

        /* je else_label（向前跳转，6 字节占位） */
        int je_pos = code_size;
        e1(0x0F); e1(0x84); e4(0);

        /* then 分支 */
        cgen_expr(node->then_stmt);
        if (is_f) push_xmm0(); else push_rax();

        /* jmp end_label（5 字节占位） */
        int jmp_pos = code_size;
        e1(0xE9); e4(0);

        /* else 标签 */
        int else_pos = code_size;
        /* 回填 je: disp = else_pos - (je_pos + 6) */
        { int d = else_pos - (je_pos + 6);
          code_buf[je_pos+2]=d&0xFF; code_buf[je_pos+3]=(d>>8)&0xFF;
          code_buf[je_pos+4]=(d>>16)&0xFF; code_buf[je_pos+5]=(d>>24)&0xFF; }

        cgen_expr(node->else_stmt);
        if (is_f) push_xmm0(); else push_rax();

        /* end 标签 */
        int end_pos = code_size;
        /* 回填 jmp: disp = end_pos - (jmp_pos + 5) */
        { int d = end_pos - (jmp_pos + 5);
          code_buf[jmp_pos+1]=d&0xFF; code_buf[jmp_pos+2]=(d>>8)&0xFF;
          code_buf[jmp_pos+3]=(d>>16)&0xFF; code_buf[jmp_pos+4]=(d>>24)&0xFF; }

        /* 统一结果: 从栈弹出 */
        if (is_f) { pop_xmm0(); node->is_float = 1; }
        else pop_rax();
        /* 从分支推断 type_size（三元表达式可能返回指针） */
        { int _ts = 4;
          if (node->then_stmt && node->then_stmt->type_size > _ts)
              _ts = node->then_stmt->type_size;
          if (node->else_stmt && node->else_stmt->type_size > _ts)
              _ts = node->else_stmt->type_size;
          node->type_size = _ts; }
        break;
    }

    case AST_MEMBER: {
        /* s.member 或 p->member */
        int member_off = node->ival;
        if (node->op == TOK_DOT) {
            /* s.member：加载结构的基地址 + 成员偏移 */
            int is_local = 0;
            if (node->left && node->left->kind == AST_VAR) {
                int i;
                for (i = local_count - 1; i >= 0; i--) {
                    if (strcmp(locals[i].name, node->left->name) == 0 &&
                        locals[i].scope_depth <= scope_depth) {
                        int total_off = locals[i].offset + member_off;
                        if (node->type_size == 8)
                            load_rax_from_rbp(total_off);
                        else
                            load_eax_from_rbp(total_off);
                        is_local = 1;
                        break;
                    }
                }
            }
            if (!is_local) {
                const char *gname = (node->left && node->left->kind == AST_VAR) ? node->left->name : NULL;
                if (gname) {
                    /* lea rax, [rip+disp32] 取全局变量地址 */
                    int si = -1;
                    int i;
                    for (i = 0; i < sym_count; i++) {
                        if (syms[i].name && strcmp(syms[i].name, gname) == 0)
                            { si = i; break; }
                    }
                    if (si < 0 && sym_count < MAX_SYMS) {
                        si = sym_count;
                        CgenSym *s = &syms[sym_count++];
                        s->name = gname; s->offset = 0; s->size = 0;
                        s->is_global = 1; s->is_func = 0;
                        s->shndx = 3; s->sym_idx = -1;
                    }
                    if (si >= 0) {
                        e1(0x48); e1(0x8D); e1(0x05);  /* lea rax, [rip+disp32] */
                        int ro = code_size; e4(0);
                        if (rel_count < MAX_RELS) {
                            Elf64_Rela *r = &rels[rel_count++];
                            r->r_offset = ro;
                            r->r_info = ELF64_R_INFO(si + 1, R_X86_64_PC32);
                            r->r_addend = -4;
                        }
                    }
                } else {
                    cgen_addr(node->left);
                }
                /* 加上成员偏移 */
                if (member_off != 0) {
                    push_rax();
                    mov_eax_imm(member_off);
                    pop_rcx();
                    e1(0x48); e1(0x01); e1(0xC8);  /* add rax, rcx (64-bit — 地址运算) */
                }
                /* 从地址加载值：按 type_size 选择宽度 */
                if (node->type_size == 8) {
                    e1(0x48); e1(0x8B); e1(0x00);  /* mov rax, [rax] */
                } else {
                    e1(0x8B); e1(0x00);             /* mov eax, [rax] */
                }
            }
        } else {
            /* p->member：解引用指针 + 偏移 */
            cgen_expr(node->left);
            if (member_off != 0) {
                push_rax();
                mov_eax_imm(member_off);
                pop_rcx();
                e1(0x48); e1(0x01); e1(0xC8);  /* add rax, rcx (64-bit) */
            }
            /* 从地址加载值 */
            if (node->type_size == 8) {
                e1(0x48); e1(0x8B); e1(0x00);  /* mov rax, [rax] */
            } else {
                e1(0x8B); e1(0x00);             /* mov eax, [rax] */
            }
        }
        break;
    }

    default:
        break;
    }
}
