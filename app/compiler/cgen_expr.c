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

#include "toyc.h"

/* 纯 32 位整数浮点字面量解析（定义在 lex.c） */
extern void parse_float_literal(const char *s, int len,
                                unsigned int *out_lo, unsigned int *out_hi);

/* ─── push/pop ─── */

static void push_rax(void) { e1(0x50); }  /* push rax */
static void pop_rax(void) { e1(0x58); }   /* pop rax */
static void pop_rcx(void) { e1(0x59); }   /* pop rcx */
static void push_rcx(void) { e1(0x51); }  /* push rcx */

/* ─── 加载常量到 eax ─── */

static void mov_eax_imm(int v) { e1(0xB8); e4(v); }
static void mov_rax_imm64(long v) {
    e1(0x48); e1(0xB8);
    e1(v & 0xFF); e1((v>>8) & 0xFF); e1((v>>16) & 0xFF); e1((v>>24) & 0xFF);
    e1((v>>32) & 0xFF); e1((v>>40) & 0xFF); e1((v>>48) & 0xFF); e1((v>>56) & 0xFF);
}

/* ─── 加载/存储局部变量 [rbp+disp] ─── */
/* 自动选择 disp8 或 disp32（用于大数组，如 elf_write 的 256KB static 缓冲） */

static void lea_from_rbp(int offset) {
    e1(0x48); e1(0x8D);
    if (disp8_fits(offset)) { e1(0x45); e1(offset & 0xFF); }
    else { e1(0x85); e4(offset); }
}

/* mov eax, [rbp+off] */

/* mov rax, [rbp+off] — 64-bit 加载 */

/* mov [rbp+off], eax — 32-bit 存储 */

/* mov [rbp+off], rax — 64-bit 存储 */

/* ─── SSE 浮点辅助（始终启用） ─── */

/* load_double_bits_halves() 在 cgen_float_hack.c 中实现 */
extern void load_double_bits_halves(unsigned int hi, unsigned int lo);

/* movsd xmm0, [rbp+off] / movsd [rbp+off], xmm0 — 自动选择 disp8/disp32 */
static void load_double_from_rbp(int off) {
    if (disp8_fits(off)) {
        e1(0xF2); e1(0x0F); e1(0x10); e1(0x45); e1(off & 0xFF);
    } else {
        e1(0xF2); e1(0x0F); e1(0x10); e1(0x85); e4(off);
    }
}

static void store_double_to_rbp(int off) {
    if (disp8_fits(off)) {
        e1(0xF2); e1(0x0F); e1(0x11); e1(0x45); e1(off & 0xFF);
    } else {
        e1(0xF2); e1(0x0F); e1(0x11); e1(0x85); e4(off);
    }
}

static void push_xmm0(void) {
    e1(0x48); e1(0x83); e1(0xEC); e1(0x08);
    e1(0xF2); e1(0x0F); e1(0x11); e1(0x04); e1(0x24);
}

static void pop_xmm1(void) {
    e1(0xF2); e1(0x0F); e1(0x10); e1(0x0C); e1(0x24);
    e1(0x48); e1(0x83); e1(0xC4); e1(0x08);
}

static void pop_xmm0(void) {
    e1(0xF2); e1(0x0F); e1(0x10); e1(0x04); e1(0x24);
    e1(0x48); e1(0x83); e1(0xC4); e1(0x08);
}

static void cvti2d(void) {
    e1(0xF2); e1(0x0F); e1(0x2A); e1(0xC0);
}

static void cvti2f(void) {
    e1(0xF3); e1(0x0F); e1(0x2A); e1(0xC0);  /* cvtsi2ss xmm0, eax */
}

static void save_xmm0_to_xmm1(void) {
    e1(0x66); e1(0x0F); e1(0x28); e1(0xC8);
}

static void restore_xmm1_to_xmm0(void) {
    e1(0x66); e1(0x0F); e1(0x28); e1(0xC1);
}

static void negate_double(void) {
    e1(0x48); e1(0xB8);
    e1(0x00); e1(0x00); e1(0x00); e1(0x00);
    e1(0x00); e1(0x00); e1(0x00); e1(0x80);
    e1(0x66); e1(0x48); e1(0x0F); e1(0x6E); e1(0xC8);
    e1(0x66); e1(0x0F); e1(0x57); e1(0xC1);
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

/* 无符号除法/取模 */
static void binop_divu(void) {
    e1(0x91); e1(0x31); e1(0xD2); e1(0xF7); e1(0xF1);  /* xchg; xor edx,edx; div ecx */
}
static void binop_modu(void) { binop_divu(); e1(0x89); e1(0xD0); }  /* mov eax, edx */
static void binop_and(void) { e1(0x21); e1(0xC8); }  /* and eax, ecx */
static void binop_or(void)  { e1(0x09); e1(0xC8); }  /* or eax, ecx */
static void binop_xor(void) { e1(0x31); e1(0xC8); }  /* xor eax, ecx */
static void binop_shl(void) { e1(0x91); e1(0xD3); e1(0xE0); }  /* xchg eax,ecx; shl eax, cl */
static void binop_shr(void) { e1(0x91); e1(0xD3); e1(0xE8); }  /* xchg eax,ecx; shr eax, cl (unsigned) */
static void binop_sar(void) { e1(0x91); e1(0xD3); e1(0xF8); }  /* xchg eax,ecx; sar eax, cl (signed) */
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

/* 无符号 64-bit 除法/取模 */
static void binop_divu64(void) {
    e1(0x48); e1(0x91);           /* xchg rax, rcx */
    e1(0x48); e1(0x31); e1(0xD2); /* xor rdx, rdx */
    e1(0x48); e1(0xF7); e1(0xF1); /* div rcx */
}
static void binop_modu64(void) { binop_divu64(); e1(0x48); e1(0x89); e1(0xD0); }  /* mov rax, rdx */
static void binop_and64(void) { e1(0x48); e1(0x21); e1(0xC8); }  /* and rax, rcx */
static void binop_or64(void)  { e1(0x48); e1(0x09); e1(0xC8); }  /* or rax, rcx */
static void binop_xor64(void) { e1(0x48); e1(0x31); e1(0xC8); }  /* xor rax, rcx */
static void binop_shl64(void) { e1(0x48); e1(0x91); e1(0x48); e1(0xD3); e1(0xE0); }  /* xchg; shl rax, cl */
static void binop_shr64(void) { e1(0x48); e1(0x91); e1(0x48); e1(0xD3); e1(0xE8); }  /* xchg; shr rax, cl (unsigned) */
static void binop_sar64(void) { e1(0x48); e1(0x91); e1(0x48); e1(0xD3); e1(0xF8); }  /* xchg; sar rax, cl (signed) */
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
        if (sym_count >= MAX_SYMS) {
            __write(2, "toyc: too many symbols\n", 22);
            __exit(1); }
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
    if (rel_count >= MAX_RELS) {
        __write(2, "toyc: too many relocations\n", 26);
        __exit(1); }
    Elf64_Rela *r = &rels[rel_count++];
    int call_off = code_size;
    r->r_offset = call_off + 1;
    r->r_info = ELF64_R_INFO(sym_idx + 1, R_X86_64_PLT32);
    r->r_addend = -4;

    /* e8 00 00 00 00: call rel32（占位重定位） */
    e1(0xE8); e1(0x11); e1(0x22); e1(0x33); e1(0x44);
}

/* ─── 左值地址计算（用于 ++/--） ─── */

/* 计算左值的内存地址到 rax 中，支持 AST_VAR、AST_MEMBER、*ptr */
void cgen_addr(AstNode *node) {
    if (!node) return;

    switch (node->kind) {
    case AST_VAR: {
        /* 局部变量：lea rax, [rbp+off] */
        int i;
        SEARCH_LOCAL(i, node->name);
        if (i >= 0) {
                lea_from_rbp(locals[i].offset);
                return;
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
                int ro = code_size; e1(0x55); e1(0x66); e1(0x77); e1(0x88);
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
            /* a[i] 的地址：计算 base + index * elem_size
             * 先算索引再算基地址，避免 push 覆盖函数返回的隐藏缓冲区 */
            cgen_expr(node->right);      /* 索引 → eax */
            push_rax();                  /* 暂存索引（安全：函数调用尚未发生 */
            cgen_expr(node->left);       /* 数组基地址 → rax */
            pop_rcx();                    /* rcx = 索引, rax = 基地址 */
            /* 交换：rax ← 索引（供扩展/移位），rcx ← 基地址（供最终加法） */
            e1(0x48); e1(0x89); e1(0xC6);  /* mov rsi, rax */
            e1(0x48); e1(0x89); e1(0xC8);  /* mov rax, rcx */
            e1(0x48); e1(0x89); e1(0xF1);  /* mov rcx, rsi */

            int elem_size = 1;
            int idx_is64 = (node->right && node->right->type_size == 8);
            if (node->left && node->left->kind == AST_VAR) {
                int i;
                SEARCH_LOCAL(i, node->left->name);
                if (i >= 0) {
                        if (locals[i].element_size > 0)
                            elem_size = locals[i].element_size;

                }
                if (i < 0) {
                    for (i = 0; i < sym_count; i++) {
                        if (syms[i].name && strcmp(syms[i].name, node->left->name) == 0) {
                            if (i < MAX_SYMS && global_elem_size[i] > 0)
                                elem_size = global_elem_size[i];
                            else if (i < MAX_SYMS && global_ptr_elem_size[i] > 0)
                                elem_size = global_ptr_elem_size[i];
                            break;
                        }
                    }
                }
            }
            /* fallback: 结构体成员数组 s.arr[i] — 使用 AST_MEMBER 上的 elem_size */
            if (elem_size == 1 && node->left && node->left->kind == AST_MEMBER && node->left->elem_size > 0) {
                elem_size = node->left->elem_size;
            }
            /* 通用 fallback：非 AST_MEMBER 的左表达式（如 (p+N) 指针算术结果）也可能有 elem_size */
            if (elem_size == 1 && node->left && node->left->elem_size > 0) {
                elem_size = node->left->elem_size;
            }

            /* 符号扩展有符号 32 位索引到 64 位（指针算术需要完整 64-bit 偏移） */
            if (!idx_is64 && node->right && !node->right->is_unsigned) {
                e1(0x48); e1(0x63); e1(0xC0);  /* cdqe = movsxd rax, eax */
                idx_is64 = 1;
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
                    e1(0xB8); e4(elem_size);   /* mov eax, elem_size (zero-extends to rax) */
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
            SEARCH_LOCAL(i, node->left->name);
            if (i >= 0) {
                    int addr = locals[i].offset + moff;
                    lea_from_rbp(addr);
                    return;
            }
        }
        /* p->member: 计算指针值，加上成员偏移 */
        if (node->op == TOK_ARROW) {
            cgen_expr(node->left);       /* rax = 指针值 */
            if (moff != 0) {
                push_rax();
                mov_eax_imm(moff);
                if (moff < 0) { e1(0x48); e1(0x63); e1(0xC0); }  /* movsxd rax, eax */
                pop_rcx();
                e1(0x48); e1(0x01); e1(0xC8);  /* add rax, rcx */
            }
            return;
        }
        /* 非 AST_VAR 的 .member — 用 cgen_addr 取基地址（支持 a[i].member 等复合） */
        cgen_addr(node->left);
        if (moff != 0) {
            e1(0x48); e1(0x89); e1(0xC1);  /* mov rcx, rax */
            mov_eax_imm(moff);
            if (moff < 0) { e1(0x48); e1(0x63); e1(0xC0); }  /* movsxd rax, eax */
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
        if (node->is_float) {
            unsigned int lo, hi;
            if (node->name == NULL) {
                /* __builtin_huge_val / __builtin_huge_valf：使用预计算位模式 */
                hi = (unsigned int)(node->ival >> 32);
                lo = (unsigned int)(node->ival);
            } else {
                /* 在代码生成时用纯 32 位整数运算解析浮点字面量，
                 * 避免 toyc 的 double 算术 bug 和 struct 字段偏移 bug。 */
                parse_float_literal(node->name, (int)node->ival, &lo, &hi);
            }
            load_double_bits_halves(hi, lo);
            /* 对 float (32-bit) 字面量：将 double 位模式转换为 float */
            if (node->type_size == 4) {
                e1(0xF2); e1(0x0F); e1(0x5A); e1(0xC0);  /* cvtsd2ss xmm0, xmm0 */
            }
        } else
            if (node->ival >= -2147483648L && node->ival <= 2147483647L) {
                mov_eax_imm((int)node->ival);
                if (node->ival < 0) { e1(0x48); e1(0x63); e1(0xC0); }  /* movsxd rax, eax: 符号扩展 32→64 */
            } else {
                mov_rax_imm64(node->ival);
            }
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
            __write(2, "toyc: string pool overflow\n", 26);
            __exit(1);
        }
        int i;
        for (i = 0; i < len; i++)
            strpool_buf[strpool_size++] = str[i];

        /* 在 syms[] 中创建 LOCAL 符号（必须早于后续 GLOBAL 创建，确保 ELF 顺序正确） */
        if (str_info_count >= MAX_STRINGS) {
            __write(2, "toyc: string info overflow\n", 26);
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
        if (sym_count >= MAX_SYMS) {
            __write(2, "toyc: too many symbols\n", 22);
            __exit(1); }
        int sym_idx = sym_count++;
        syms[sym_idx].name = str_infos[si].name;
        syms[sym_idx].offset = 0;
        syms[sym_idx].size = len;
        syms[sym_idx].is_global = 0;
        syms[sym_idx].is_func = 0;
        syms[sym_idx].shndx = 1;  /* .text */
        syms[sym_idx].sym_idx = -1;

        /* 记录字符串信息供后续偏移修正 */
        str_infos[si].pool_offset = pool_off;
        str_infos[si].len = len;
        str_infos[si].sym_index = sym_idx;
        str_info_count++;

        /* 发射 lea rax, [rip + disp32] */
        e1(0x48); e1(0x8D); e1(0x05);   /* lea rax, [rip + disp32] */
        int reloc_off = code_size;
        e1(0x55); e1(0x66); e1(0x77); e1(0x88);  /* disp32 占位（链接器覆盖） */

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
        /* 查找局部变量偏移 — 从后往前搜索，用 scope_depth + scope_chain 过滤 */
        int i;
        SEARCH_LOCAL(i, node->name);
        if (i >= 0) {
                /* 数组/大结构体优先于浮点判断：float arr[] 应先退化为指针 */
                if (locals[i].is_array || (locals[i].size > 8 && !locals[i].is_param)) {
                    /* 数组/大结构体（非参数）：退化为指针（lea rax, [rbp+off]） */
                    lea_from_rbp(locals[i].offset);
                    node->type_size = 8;
                    node->is_float = 0;  /* 指针不是浮点值 */
                } else if (locals[i].size > 8 && locals[i].is_param) {
                    /* 大结构体参数：局部变量保存的是指向调用方结构体的指针，加载该指针 */
                    load_rax_from_rbp(locals[i].offset);
                    node->type_size = 8;
                    node->is_float = 0;
                } else if (locals[i].is_float) {
                    node->is_float = locals[i].is_float;
                    node->type_size = locals[i].is_float;
                    /* 无论 float/double 都用 movsd 加载（栈槽统一 8 字节） */
                    load_double_from_rbp(locals[i].offset);
                } else if (node->is_float) {
                    /* 转型：int 变量被标记为 float（如 (double)i） */
                    load_eax_from_rbp(locals[i].offset);
                    cvti2d();
                } else {
                    int orig_ts = node->type_size;  /* 保存转型设置的类型大小（如有） */
                    node->type_size = locals[i].size;
                    node->is_unsigned = locals[i].is_unsigned;
                    if (locals[i].size == 8)
                        load_rax_from_rbp(locals[i].offset);
                    else if (locals[i].size == 1) {
                        if (locals[i].is_unsigned) {
                            /* movzbl — zero-extend byte load */
                            if (disp8_fits(locals[i].offset))
                                { e1(0x0F); e1(0xB6); e1(0x45); e1(locals[i].offset & 0xFF); }
                            else
                                { e1(0x0F); e1(0xB6); e1(0x85); e4(locals[i].offset); }
                        } else {
                            /* movsbl — sign-extend byte load */
                            if (disp8_fits(locals[i].offset))
                                { e1(0x0F); e1(0xBE); e1(0x45); e1(locals[i].offset & 0xFF); }
                            else
                                { e1(0x0F); e1(0xBE); e1(0x85); e4(locals[i].offset); }
                        }
                    } else if (locals[i].size == 2) {
                        if (locals[i].is_unsigned) {
                            /* movzwl — zero-extend word load */
                            if (disp8_fits(locals[i].offset))
                                { e1(0x0F); e1(0xB7); e1(0x45); e1(locals[i].offset & 0xFF); }
                            else
                                { e1(0x0F); e1(0xB7); e1(0x85); e4(locals[i].offset); }
                        } else {
                            /* movswl — sign-extend word load */
                            if (disp8_fits(locals[i].offset))
                                { e1(0x0F); e1(0xBF); e1(0x45); e1(locals[i].offset & 0xFF); }
                            else
                                { e1(0x0F); e1(0xBF); e1(0x85); e4(locals[i].offset); }
                        }
                    }
                    else
                        load_eax_from_rbp(locals[i].offset);
                    /* 有符号窄变量被转型为更宽类型时补符号扩展（如 (long)int_var）
                     * 对于有符号类型，32-bit 加载仅零扩展到 64-bit，但 cast 要求符号扩展。
                     * 例：int n = -42; (long)n → 需从 0xFFFFFFD6 扩展为 0xFFFFFFFFFFFFFFD6 */
                    if (orig_ts > locals[i].size && orig_ts == 8 && !locals[i].is_unsigned) {
                        e1(0x48); e1(0x63); e1(0xC0);  /* movsxd rax, eax */
                        node->type_size = orig_ts;     /* 恢复类型宽度，反映实际值宽度 */
                    }
                }
                return;
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
                /* 从 pvar 表查询数组元素大小（extern 数组如 elf_code_buf
                 * 在 parse 阶段已注册正确大小但未创建 AST_VAR_DECL，
                 * 此处补上 global_elem_size 使 is_global_arr 检测生效）。 */
                if (si < MAX_SYMS) {
                    int _psz = pvar_lookup_size(node->name);
                    int _pesz = pvar_lookup_elem_size(node->name);
                    if (_psz > 8 && _pesz > 0) {
                        global_elem_size[si] = _pesz;
                        syms[si].size = _psz;
                    }
                }
            }
            if (si >= 0) {
                /* 数组检测：从 AST 构建时记录的 is_array 标志判断 */
                int is_global_arr = (si < MAX_SYMS && global_is_array[si]);
                if (is_global_arr) {
                    /* 数组→指针衰减（lea rax, [rip + disp32]），优先于 is_float 检查 */
                    e1(0x48); e1(0x8D); e1(0x05);
                    node->type_size = 8;
                    /* 传播数组元素大小、浮点类型和符号性供下标/解引用使用 */
                    if (si < MAX_SYMS) {
                        if (global_elem_size[si] > 0)
                            node->elem_size = global_elem_size[si];
                        if (global_elem_float[si])
                            node->elem_is_float = global_elem_float[si];
                        if (global_elem_unsigned[si])
                            node->elem_is_unsigned = 1;
                    }
                    int ro = code_size;
                    e1(0x55); e1(0x66); e1(0x77); e1(0x88);
                    if (rel_count < MAX_RELS) {
                        Elf64_Rela *r = &rels[rel_count++];
                        r->r_offset = ro;
                        r->r_info = ELF64_R_INFO(si + 1, R_X86_64_PC32);
                        r->r_addend = -4;
                    }
                    break;
                }
                /* 浮点全局变量（非数组）：用 movss/movsd 加载到 xmm0 */
                if (node->is_float) {
                    if (node->is_float == 4) {
                        e1(0xF3); e1(0x0F); e1(0x10); e1(0x05);  /* movss xmm0, [rip+disp32] */
                    } else {
                        e1(0xF2); e1(0x0F); e1(0x10); e1(0x05);  /* movsd xmm0, [rip+disp32] */
                    }
                    int ro = code_size;
                    e1(0x55); e1(0x66); e1(0x77); e1(0x88);
                    if (rel_count < MAX_RELS) {
                        Elf64_Rela *r = &rels[rel_count++];
                        r->r_offset = ro;
                        r->r_info = ELF64_R_INFO(si + 1, R_X86_64_PC32);
                        r->r_addend = -4;
                    }
                    break;
                }
                /* 全局变量：使用符号表记录的大小确定加载宽度 */
                int gsz = syms[si].size > 0 ? syms[si].size :
                          (node->type_size > 0 ? node->type_size : 4);
                /* 数组检测（复用外层声明的 is_global_arr） */
                is_global_arr = (si < MAX_SYMS && global_is_array[si]);
                if (is_global_arr || gsz > 8) {
                    /* 数组/大结构体：数组→指针衰减（lea rax, [rip + disp32]） */
                    e1(0x48); e1(0x8D); e1(0x05);
                    node->type_size = 8;
                    /* 传播数组元素大小供下标/解引用使用 */
                    if (si < MAX_SYMS && global_elem_size[si] > 0)
                        node->elem_size = global_elem_size[si];
                } else if (gsz == 8) {
                    e1(0x48); e1(0x8B); e1(0x05);  /* mov rax, [rip + disp32] */
                    node->type_size = 8;  /* 全局指针变量：传播 type_size=8 供指针算术检测 */
                    /* 传播指针变量的元素大小供下标使用（int*→4, long*→8） */
                    if (si < MAX_SYMS && global_ptr_elem_size[si] > 0)
                        node->elem_size = global_ptr_elem_size[si];
                } else {
                    e1(0x8B); e1(0x05);             /* mov eax, [rip + disp32] */
                }
                int ro = code_size;
                e1(0x55); e1(0x66); e1(0x77); e1(0x88);
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
            cgen_expr(node->right);      /* 索引 → eax（先算索引，避免 push 覆盖隐藏缓冲区） */
            push_rax();                  /* 暂存索引（安全：函数调用尚未发生） */
            cgen_expr(node->left);       /* 指针 → rax（函数调用在此发生，隐藏缓冲区在 RSP 下方） */
            pop_rcx();                    /* rcx = 索引, rax = 基地址 */
            /* 交换：rax ← 索引（供符号扩展/移位），rcx ← 基地址（供最终加法） */
            e1(0x48); e1(0x89); e1(0xC6);  /* mov rsi, rax */
            e1(0x48); e1(0x89); e1(0xC8);  /* mov rax, rcx */
            e1(0x48); e1(0x89); e1(0xF1);  /* mov rcx, rsi */

            /* 确定元素大小和符号性（默认 1 = char*，默认 signed） */
            int elem_size = 1;
            int elem_unsigned = 0;
            int elem_float = 0;      /* 0=非浮点, 4=float, 8=double */
            int idx_is64 = (node->right && node->right->type_size == 8);
            if (node->left && node->left->kind == AST_VAR) {
                int i;
                SEARCH_LOCAL(i, node->left->name);
                if (i >= 0) {
                        if (locals[i].element_size > 0)
                            elem_size = locals[i].element_size;
                        elem_unsigned = locals[i].elem_is_unsigned;
                        /* float 数组：检查 is_float（数组的 is_float 表示元素类型） */
                        if (locals[i].is_array && locals[i].is_float)
                            elem_float = locals[i].is_float;

                }
                if (i < 0) {
                    for (i = 0; i < sym_count; i++) {
                        if (syms[i].name && strcmp(syms[i].name, node->left->name) == 0) {
                            if (i < MAX_SYMS) {
                                if (global_elem_size[i] > 0) {
                                    elem_size = global_elem_size[i];
                                    elem_unsigned = global_elem_unsigned[i];
                                    elem_float = global_elem_float[i];
                                } else if (global_ptr_elem_size[i] > 0) {
                                    elem_size = global_ptr_elem_size[i];
                                }
                            }
                            break;
                        }
                    }
                }
            } else if (node->left && node->left->kind == AST_BINOP &&
                       node->left->op == TOK_LBRACKET &&
                       node->left->left && node->left->left->kind == AST_VAR) {
                /* 多维数组外层下标：使用 base_elem_size（内层元素大小） */
                int i;
                SEARCH_LOCAL(i, node->left->left->name);
                if (i >= 0) {
                        if (locals[i].base_elem_size > 0)
                            elem_size = locals[i].base_elem_size;
                        else if (locals[i].element_size > 0)
                            elem_size = locals[i].element_size;
                        elem_unsigned = locals[i].elem_is_unsigned;
                        if (locals[i].is_array && locals[i].is_float)
                            elem_float = locals[i].is_float;
                }
                if (i < 0) {
                    for (i = 0; i < sym_count; i++) {
                        if (syms[i].name && strcmp(syms[i].name, node->left->left->name) == 0) {
                            if (i < MAX_SYMS) {
                                if (global_base_elem_size[i] > 0)
                                    elem_size = global_base_elem_size[i];
                                elem_unsigned = global_elem_unsigned[i];
                                if (global_elem_float[i] > 0)
                                    elem_float = global_elem_float[i];
                            }
                            break;
                        }
                    }
                }
            }
            /* fallback: 结构体成员数组 s.arr[i] — 使用 AST_MEMBER 上的 elem_size */
            if (elem_size == 1 && node->left && node->left->kind == AST_MEMBER && node->left->elem_size > 0) {
                elem_size = node->left->elem_size;
                elem_unsigned = node->left->is_unsigned;
            }
            /* 通用 fallback：非 AST_MEMBER 的左表达式（如 (p+N) 指针算术结果）也可能有 elem_size。
             * 但排除嵌套 LBRACKET（如 strs[i][j] 的外层，inner 的 elem_size 是被加载值的大小，
             * 并非指针指向元素的大小）。
             * 注意：不传播 is_unsigned——AST_VAR 节点在解析阶段未正确设置 is_unsigned
             *（尤其全局数组），应依赖 elem_size 查找时同步的 elem_unsigned 查找。 */
            if (elem_size == 1 && node->left &&
                !(node->left->kind == AST_BINOP && node->left->op == TOK_LBRACKET) &&
                node->left->elem_size > 0) {
                elem_size = node->left->elem_size;
            }

            /* 符号扩展有符号 32 位索引到 64 位（指针算术需要完整 64-bit 偏移） */
            if (!idx_is64 && node->right && !node->right->is_unsigned) {
                e1(0x48); e1(0x63); e1(0xC0);  /* cdqe = movsxd rax, eax */
                idx_is64 = 1;
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
                    e1(0xB8); e4(elem_size);   /* mov eax, elem_size (zero-extends to rax) */
                    e1(0x48); e1(0x0F); e1(0xAF); e1(0x04); e1(0x24);  /* imul rax, [rsp] */
                } else {
                    e1(0xB8); e4(elem_size);           /* mov eax, elem_size */
                    e1(0x0F); e1(0xAF); e1(0x04); e1(0x24);  /* imul eax, [rsp] */
                }
                e1(0x48); e1(0x83); e1(0xC4); e1(0x08);  /* add rsp, 8 (discard saved index) */
            }

            /* ptr + offset → rax */
            e1(0x48); e1(0x01); e1(0xC8);  /* add rax, rcx */

            /* 判断是否子数组（多维数组内层下标 → 退化为指针，不加载）
             * 通过 base_elem_size 区分：int arr[3][4] 中 arr[i] 有
             * element_size=16 base_elem_size=4（子数组），而 int *p 有 element_size=4 base_elem_size=0 */
            int is_subarray = 0;
            if (node->left && node->left->kind == AST_VAR && elem_size >= 4) {
                int idx;
                SEARCH_LOCAL(idx, node->left->name);
                if (idx >= 0) {
                        if (locals[idx].is_array && locals[idx].base_elem_size > 0 &&
                            locals[idx].base_elem_size < elem_size && !locals[idx].elem_is_ptr)
                            is_subarray = 1;

                }
                if (idx < 0) {
                    for (idx = 0; idx < sym_count; idx++) {
                        if (syms[idx].name && strcmp(syms[idx].name, node->left->name) == 0) {
                            if (idx < MAX_SYMS && global_base_elem_size[idx] > 0 && global_base_elem_size[idx] < elem_size &&
                                !global_elem_is_ptr_arr[idx])
                                is_subarray = 1;
                            break;
                        }
                    }
                }
            }

            /* 按元素大小、浮点性和符号性加载结果（子数组/大结构体不加载，退化为指针） */
            if (is_subarray || elem_size > 8) {
                /* 子数组/大结构体：不加载，rax 中已是指针 */
                node->type_size = 8;
            } else if (elem_float) {
                /* float/double 数组元素：加载到 xmm0 */
                if (elem_float == 4) {
                    e1(0xF3); e1(0x0F); e1(0x10); e1(0x00);  /* movss xmm0, [rax] */
                } else {
                    e1(0xF2); e1(0x0F); e1(0x10); e1(0x00);  /* movsd xmm0, [rax] */
                }
                node->is_float = elem_float;
                node->type_size = elem_size;
            } else if (elem_size >= 8) {
                e1(0x48); e1(0x8B); e1(0x00);    /* mov rax, [rax] */
                node->type_size = elem_size;
            } else if (elem_size == 4) {
                e1(0x8B); e1(0x00);                   /* mov eax, [rax] */
                node->type_size = elem_size;
            } else if (elem_size == 2) {
                if (elem_unsigned) {
                    e1(0x0F); e1(0xB7); e1(0x00);  /* movzwl (%rax), %eax — 零扩展字加载 */
                } else {
                    e1(0x0F); e1(0xBF); e1(0x00);  /* movswl (%rax), %eax — 符号扩展字加载 */
                }
                node->type_size = elem_size;
            } else {
                if (elem_unsigned) {
                    e1(0x0F); e1(0xB6); e1(0x00);  /* movzbl (%rax), %eax — 零扩展字节加载 */
                } else {
                    e1(0x0F); e1(0xBE); e1(0x00);  /* movsbl (%rax), %eax — 符号扩展字节加载 */
                }
                node->type_size = elem_size;
            }
            node->is_unsigned = elem_unsigned;
            /* 传播结果类型的"解引用宽度"供 *expr 加载时确定宽度。
             * 对于 a[i] 结果是指针的场景（如 int *ptrs[3] → ptrs[i] 是指针），
             * *ptrs[i] 的加载宽度是 base_elem_size（int → 4 字节）而非 elem_size
             *（指针本身 8 字节）。对子数组也一样。 */
            node->elem_size = elem_size;  /* default: the loaded value IS the data */
            if (node->left && node->left->kind == AST_VAR && node->left->name) {
                int _di; SEARCH_LOCAL(_di, node->left->name);
                if (_di >= 0 && (locals[_di].elem_is_ptr || is_subarray)
                    && locals[_di].base_elem_size > 0) {
                    node->elem_size = locals[_di].base_elem_size;
                }
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
                e1(0x0F); e1(0x85); e1(0x11); e1(0x22); e1(0x33); e1(0x44); /* jnz L_true */
            } else {
                /* a && b: a 为 false 则跳到 false_path */
                e1(0x0F); e1(0x84); e1(0x11); e1(0x22); e1(0x33); e1(0x44); /* jz L_false */
            }

            cgen_expr(node->right);
            e1(0x85); e1(0xC0);
            int j2_pos = code_size;

            if (is_or) {
                /* b 也为 true → 跳到 true_path */
                e1(0x0F); e1(0x85); e1(0x11); e1(0x22); e1(0x33); e1(0x44); /* jnz L_true */
            } else {
                /* b 为 false → 跳到 false_path */
                e1(0x0F); e1(0x84); e1(0x11); e1(0x22); e1(0x33); e1(0x44); /* jz L_false */
            }

            if (is_or) {
                /* a||b: 两个条件都假 → false_path */
                int false_pos = code_size;
                mov_eax_imm(0);
                int ep = code_size;
                e1(0xE9); e1(0x55); e1(0x66); e1(0x77); e1(0x88); /* jmp L_end */
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
                e1(0xE9); e1(0x55); e1(0x66); e1(0x77); e1(0x88); /* jmp L_end */
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

            /* 确定浮点操作类型（4=float, 8=double）：较大的类型优先级更高 */
            int float_type = 0;
            if (node->left && node->left->is_float == 8) float_type = 8;
            else if (node->right && node->right->is_float == 8) float_type = 8;
            else if (node->left && node->left->is_float == 4) float_type = 4;
            else if (node->right && node->right->is_float == 4) float_type = 4;
            int is_ss = (float_type == 4);  /* 单精度（ss）vs 双精度（sd） */

            /* 浮点比较：用 ucomiss/ucomisd，结果始终是 int (eax=0/1) */
            if (is_cmp) {
                cgen_expr(node->left);
                if (!left_f) { if (is_ss) cvti2f(); else cvti2d(); }
                save_xmm0_to_xmm1();      /* xmm1 = left */
                /* 保存 xmm1 到栈上：右表达式求值可能通过 negate_double()
                 * 用 movq xmm1,rax 冲掉 xmm1（如负数字面量 -3.13）。 */
                e1(0x48); e1(0x83); e1(0xEC); e1(0x08);  /* sub rsp, 8 */
                e1(0xF2); e1(0x0F); e1(0x11); e1(0x0C); e1(0x24);  /* movsd [rsp], xmm1 */
                cgen_expr(node->right);
                if (!right_f) { if (is_ss) cvti2f(); else cvti2d(); }
                e1(0xF2); e1(0x0F); e1(0x10); e1(0x0C); e1(0x24);  /* movsd xmm1, [rsp] */
                e1(0x48); e1(0x83); e1(0xC4); e1(0x08);  /* add rsp, 8 */
                /* ucomiss/ucomisd xmm1, xmm0 (xmm1 - xmm0) */
                if (is_ss) {
                    e1(0x0F); e1(0x2E); e1(0xC8);       /* ucomiss xmm1, xmm0 */
                } else {
                    e1(0x66); e1(0x0F); e1(0x2E); e1(0xC8);  /* ucomisd xmm1, xmm0 */
                }
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
            if (!left_f) { if (is_ss) cvti2f(); else cvti2d(); }
            push_xmm0();               /* 保存左操作数 */

            cgen_expr(node->right);
            if (!right_f) { if (is_ss) cvti2f(); else cvti2d(); }
            pop_xmm1();                /* xmm1 = left, xmm0 = right */

            /* xmm1 = xmm1 OP xmm0（F2=sd, F3=ss） */
            int op_prefix = is_ss ? 0xF3 : 0xF2;
            switch (node->op) {
            case TOK_PLUS:  e1(op_prefix); e1(0x0F); e1(0x58); e1(0xC8); break;
            case TOK_MINUS: e1(op_prefix); e1(0x0F); e1(0x5C); e1(0xC8); break;
            case TOK_STAR:  e1(op_prefix); e1(0x0F); e1(0x59); e1(0xC8); break;
            case TOK_SLASH: e1(op_prefix); e1(0x0F); e1(0x5E); e1(0xC8); break;
            default: break;
            }
            restore_xmm1_to_xmm0();    /* 结果→xmm0 */
            node->is_float = float_type;
            break;
        }
        }

        /* 普通整数二元运算：左→栈，右→eax，出栈→ecx，计算 */
        cgen_expr(node->left);
        /* 代码生成后重新检测浮点：某些表达式（如 *float_ptr）在 parse 时
         * 无法确定浮点类型，但 cgen_expr 之后 node->left->is_float 已正确设置。
         * 注意：若上方 float 分支已执行，它已 break，不会走到这里。 */
        if (node->left && node->left->is_float) {
            int _ss = (node->left->is_float == 4);
            int _cmp = (node->op == TOK_LESS || node->op == TOK_GREATER ||
                        node->op == TOK_LESS_EQ || node->op == TOK_GREATER_EQ ||
                        node->op == TOK_EQ_EQ || node->op == TOK_NOT_EQ);
            if (_cmp) {
                /* 浮点比较 */
                cgen_expr(node->left);
                if (!_ss) cvti2d();
                save_xmm0_to_xmm1();
                e1(0x48); e1(0x83); e1(0xEC); e1(0x08);
                e1(0xF2); e1(0x0F); e1(0x11); e1(0x0C); e1(0x24);
                cgen_expr(node->right);
                if (!(node->right && node->right->is_float)) {
                    if (_ss) cvti2f(); else cvti2d();
                }
                e1(0xF2); e1(0x0F); e1(0x10); e1(0x0C); e1(0x24);
                e1(0x48); e1(0x83); e1(0xC4); e1(0x08);
                if (_ss) {
                    e1(0x0F); e1(0x2E); e1(0xC8);  /* ucomiss */
                } else {
                    e1(0x66); e1(0x0F); e1(0x2E); e1(0xC8);  /* ucomisd */
                }
                if (node->op == TOK_LESS) { e1(0x0F); e1(0x92); e1(0xC0); }
                else if (node->op == TOK_LESS_EQ) { e1(0x0F); e1(0x96); e1(0xC0); }
                else if (node->op == TOK_GREATER) { e1(0x0F); e1(0x97); e1(0xC0); }
                else if (node->op == TOK_GREATER_EQ) { e1(0x0F); e1(0x93); e1(0xC0); }
                else if (node->op == TOK_EQ_EQ) { e1(0x0F); e1(0x94); e1(0xC0); }
                else if (node->op == TOK_NOT_EQ) { e1(0x0F); e1(0x95); e1(0xC0); }
                e1(0x0F); e1(0xB6); e1(0xC0);  /* movzx eax, al */
                node->is_float = 0;
            } else {
                push_xmm0();
                cgen_expr(node->right);
                if (!(node->right && node->right->is_float)) {
                    if (_ss) cvti2f(); else cvti2d();
                }
                pop_xmm1();
                if (node->op == TOK_PLUS) {
                    if (_ss) { e1(0xF3); e1(0x0F); e1(0x58); e1(0xC8); }
                    else { e1(0xF2); e1(0x0F); e1(0x58); e1(0xC8); }
                } else if (node->op == TOK_MINUS) {
                    if (_ss) { e1(0xF3); e1(0x0F); e1(0x5C); e1(0xC8); }
                    else { e1(0xF2); e1(0x0F); e1(0x5C); e1(0xC8); }
                } else if (node->op == TOK_STAR) {
                    if (_ss) { e1(0xF3); e1(0x0F); e1(0x59); e1(0xC8); }
                    else { e1(0xF2); e1(0x0F); e1(0x59); e1(0xC8); }
                } else if (node->op == TOK_SLASH) {
                    if (_ss) { e1(0xF3); e1(0x0F); e1(0x5E); e1(0xC8); }
                    else { e1(0xF2); e1(0x0F); e1(0x5E); e1(0xC8); }
                }
                e1(0x66); e1(0x0F); e1(0x28); e1(0xC1);  /* movapd xmm0, xmm1 */
                node->is_float = node->left->is_float;
            }
            break;
        }
        push_rax();
        cgen_expr(node->right);
        pop_rcx();  /* rcx = left, rax = right */

        /* 指针算术缩放：ptr + int 或 int + ptr 时，整数操作数乘以元素大小 */
        if ((node->op == TOK_PLUS || node->op == TOK_MINUS) &&
            ((node->left && node->left->type_size == 8 &&
              node->right && !node->right->is_float) ||
             (node->right && node->right->type_size == 8 &&
              node->left && !node->left->is_float))) {
            /* 找到指针操作数：先尝试左侧（type_size==8 的通常是指针），
             * 若两侧都是 type_size==8（如 ptr + long），需通过 element_size
             * 区分指针与纯 64 位整数。若左侧不是指针，再尝试右侧。 */
            int ptelem = 1;
            AstNode *ptr_node = NULL;
            int right_is_ptr = 0;

            /* ── 尝试左操作数 ── */
            if (node->left && node->left->type_size == 8) {
                if (node->left->kind == AST_VAR && node->left->name) {
                    int vi;
                    SEARCH_LOCAL(vi, node->left->name);
                    if (vi >= 0) {
                            if (locals[vi].element_size > 0) ptelem = locals[vi].element_size;

                    }
                    if (vi < 0) {
                        for (vi = 0; vi < sym_count; vi++) {
                            if (syms[vi].name && strcmp(syms[vi].name, node->left->name) == 0) {
                                if (vi < MAX_SYMS && global_elem_size[vi] > 0)
                                    ptelem = global_elem_size[vi];
                                else if (vi < MAX_SYMS && global_ptr_elem_size[vi] > 0)
                                    ptelem = global_ptr_elem_size[vi];
                                break;
                            }
                        }
                    }
                }
                /* Cast 可能改变了 elem_size（如 (char*)int_ptr），优先使用 AST 节点值 */
                if (node->left->elem_size > 0 &&
                    !(node->left->kind == AST_BINOP && node->left->op == TOK_LBRACKET))
                    ptelem = node->left->elem_size;
                if (ptelem > 1) {
                    ptr_node = node->left;
                    right_is_ptr = 0;
                }
            }

            /* ── 左侧不是指针 → 尝试右操作数 ── */
            if (!ptr_node && node->right && node->right->type_size == 8) {
                ptelem = 1;
                if (node->right->kind == AST_VAR && node->right->name) {
                    int vi;
                    SEARCH_LOCAL(vi, node->right->name);
                    if (vi >= 0) {
                            if (locals[vi].element_size > 0) ptelem = locals[vi].element_size;

                    }
                    if (vi < 0) {
                        for (vi = 0; vi < sym_count; vi++) {
                            if (syms[vi].name && strcmp(syms[vi].name, node->right->name) == 0) {
                                if (vi < MAX_SYMS && global_elem_size[vi] > 0)
                                    ptelem = global_elem_size[vi];
                                else if (vi < MAX_SYMS && global_ptr_elem_size[vi] > 0)
                                    ptelem = global_ptr_elem_size[vi];
                                break;
                            }
                        }
                    }
                }
                /* Cast 可能改变了 elem_size，优先使用 AST 节点值 */
                if (node->right->elem_size > 0 &&
                    !(node->right->kind == AST_BINOP && node->right->op == TOK_LBRACKET))
                    ptelem = node->right->elem_size;
                if (ptelem > 1) {
                    ptr_node = node->right;
                    right_is_ptr = 1;
                }
            }

            /* ptr - ptr：检查另一操作数是否也是指针（ptr - ptr 不应缩放整数，
             * 直接做普通 64-bit 减法，后续指针减法代码除以 elem_size 即可） */
            if (node->op == TOK_MINUS && ptr_node) {
                AstNode *other = right_is_ptr ? node->left : node->right;
                if (other && other->type_size == 8) {
                    int other_pe = 0;
                    if (other->kind == AST_VAR && other->name) {
                        int vi;
                        SEARCH_LOCAL(vi, other->name);
                        if (vi >= 0) {
                                if (locals[vi].element_size > 0) other_pe = locals[vi].element_size;

                        }
                        if (vi < 0) {
                            for (vi = 0; vi < sym_count; vi++) {
                                if (syms[vi].name && strcmp(syms[vi].name, other->name) == 0) {
                                    if (vi < MAX_SYMS && global_elem_size[vi] > 0)
                                        other_pe = global_elem_size[vi];
                                    else if (vi < MAX_SYMS && global_ptr_elem_size[vi] > 0)
                                        other_pe = global_ptr_elem_size[vi];
                                    break;
                                }
                            }
                        }
                    }
                    if (other_pe == 0 && other->elem_size > 0)
                        other_pe = other->elem_size;
                    if (other_pe > 0) {
                        ptr_node = NULL;  /* 两侧都是指针 → 不缩放 */
                        ptelem = 1;
                    }
                }
            }

            if (!ptr_node) ptelem = 1;  /* 两侧都不是指针 → 纯整数加法，不缩放 */

            /* 将元素大小回写到指针节点，供 *(p+N) 解引用时确定加载宽度 */
            if (ptr_node && ptelem > 0)
                ptr_node->elem_size = ptelem;
            if (ptelem > 1) {
                /* 确定整数偏移的宽度（64-bit 时需用 64-bit 移位/imul）：
                 * 整数操作数是 ptr_node 另一侧的那个操作数 */
                AstNode *int_node = right_is_ptr ? node->left : node->right;
                int int64_offset = (int_node && int_node->type_size == 8);
                /* 缩放整数操作数（此后由下方 binop_add64/binop_sub... 完成加法） */
                if (right_is_ptr) {
                    /* left 是整数（在 rcx 中），right 是指针（在 rax 中） */
                    /* xchg 使指针在 rcx，整数在 rax：之后 binop 做 add rax,rcx 得到 ptr+scaled_int */
                    e1(0x48); e1(0x91);           /* xchg rax, rcx — rax=int, rcx=ptr */
                    /* 符号扩展有符号 32 位偏移到 64 位 */
                    if (!int64_offset && int_node && !int_node->is_unsigned) {
                        e1(0x48); e1(0x63); e1(0xC0);  /* cdqe = movsxd rax, eax */
                        int64_offset = 1;
                    }
                    if (ptelem == 2)      {
                        if (int64_offset) { e1(0x48); e1(0xC1); e1(0xE0); e1(0x01); }  /* shl rax, 1 */
                        else { e1(0xC1); e1(0xE0); e1(0x01); }                          /* shl eax, 1 */
                    } else if (ptelem == 4) {
                        if (int64_offset) { e1(0x48); e1(0xC1); e1(0xE0); e1(0x02); }  /* shl rax, 2 */
                        else { e1(0xC1); e1(0xE0); e1(0x02); }                          /* shl eax, 2 */
                    } else if (ptelem == 8) { e1(0x48); e1(0xC1); e1(0xE0); e1(0x03); }  /* shl rax, 3 */
                    else                  { e1(0x50); e1(0xB8); e4(ptelem);       /* push rax; mov eax, ptelem */
                                            if (int64_offset) { e1(0x48); e1(0x0F); e1(0xAF); e1(0x04); e1(0x24); }  /* imul rax, [rsp] */
                                            else { e1(0x0F); e1(0xAF); e1(0x04); e1(0x24); }                           /* imul eax, [rsp] */
                                            e1(0x48); e1(0x83); e1(0xC4); e1(0x08); } /* add rsp, 8 */
                    /* rcx=ptr, rax=scaled_int → 下方 binop_add64 做 add rax,rcx 得到 ptr+scaled_int */
                } else {
                    /* right 是整数（在 rax 中），left 是指针（在 rcx 中） */
                    /* 符号扩展有符号 32 位偏移到 64 位 */
                    if (!int64_offset && int_node && !int_node->is_unsigned) {
                        e1(0x48); e1(0x63); e1(0xC0);  /* cdqe = movsxd rax, eax */
                        int64_offset = 1;
                    }
                    if (ptelem == 2)      {
                        if (int64_offset) { e1(0x48); e1(0xC1); e1(0xE0); e1(0x01); }  /* shl rax, 1 */
                        else { e1(0xC1); e1(0xE0); e1(0x01); }                          /* shl eax, 1 */
                    } else if (ptelem == 4) {
                        if (int64_offset) { e1(0x48); e1(0xC1); e1(0xE0); e1(0x02); }  /* shl rax, 2 */
                        else { e1(0xC1); e1(0xE0); e1(0x02); }                          /* shl eax, 2 */
                    } else if (ptelem == 8) { e1(0x48); e1(0xC1); e1(0xE0); e1(0x03); }  /* shl rax, 3 */
                    else                  { push_rax(); mov_eax_imm(ptelem);
                                            if (int64_offset) { e1(0x48); e1(0x0F); e1(0xAF); e1(0x04); e1(0x24); }  /* imul rax, [rsp] */
                                            else { e1(0x0F); e1(0xAF); e1(0x04); e1(0x24); }                           /* imul eax, [rsp] */
                                            e1(0x48); e1(0x83); e1(0xC4); e1(0x08); }
                    /* rcx=ptr, rax=scaled_int → 下方 binop_add64 做 add rax,rcx 得到 ptr+scaled_int */
                }
            }
        }

        /* 判断是否需要 64-bit 运算（任一操作数为 64 位） */
        /* 判断有符号性：若任一操作数为 unsigned，按无符号语义处理 */
        int is_unsigned_binop = (node->left && node->left->is_unsigned) ||
                                (node->right && node->right->is_unsigned);

        if (node->left && node->left->type_size == 8)
            { switch (node->op) {
            case TOK_PLUS:  binop_add64(); break;
            case TOK_MINUS: binop_sub_swapped64(); break;
            case TOK_STAR:  binop_mul64(); break;
            case TOK_SLASH:
                if (is_unsigned_binop) binop_divu64(); else binop_div64(); break;
            case TOK_PERCENT:
                if (is_unsigned_binop) binop_modu64(); else binop_mod64(); break;
            case TOK_LESS:
                if (is_unsigned_binop) binop_cmp64(0x92); else binop_cmp64(0x9C); break;
            case TOK_GREATER:
                if (is_unsigned_binop) binop_cmp64(0x97); else binop_cmp64(0x9F); break;
            case TOK_LESS_EQ:
                if (is_unsigned_binop) binop_cmp64(0x96); else binop_cmp64(0x9E); break;
            case TOK_GREATER_EQ:
                if (is_unsigned_binop) binop_cmp64(0x93); else binop_cmp64(0x9D); break;
            case TOK_EQ_EQ:      binop_cmp64(0x94); break;
            case TOK_NOT_EQ:     binop_cmp64(0x95); break;
            case TOK_LESS_LESS:       binop_shl64(); break;
            case TOK_GREATER_GREATER:
                if (is_unsigned_binop) binop_shr64(); else binop_sar64(); break;
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
            case TOK_SLASH:
                if (is_unsigned_binop) binop_divu64(); else binop_div64(); break;
            case TOK_PERCENT:
                if (is_unsigned_binop) binop_modu64(); else binop_mod64(); break;
            case TOK_LESS:
                if (is_unsigned_binop) binop_cmp64(0x92); else binop_cmp64(0x9C); break;
            case TOK_GREATER:
                if (is_unsigned_binop) binop_cmp64(0x97); else binop_cmp64(0x9F); break;
            case TOK_LESS_EQ:
                if (is_unsigned_binop) binop_cmp64(0x96); else binop_cmp64(0x9E); break;
            case TOK_GREATER_EQ:
                if (is_unsigned_binop) binop_cmp64(0x93); else binop_cmp64(0x9D); break;
            case TOK_EQ_EQ:      binop_cmp64(0x94); break;
            case TOK_NOT_EQ:     binop_cmp64(0x95); break;
            case TOK_LESS_LESS:       binop_shl64(); break;
            case TOK_GREATER_GREATER:
                if (is_unsigned_binop) binop_shr64(); else binop_sar64(); break;
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
            case TOK_SLASH:
                if (is_unsigned_binop) binop_divu(); else binop_div(); break;
            case TOK_PERCENT:
                if (is_unsigned_binop) binop_modu(); else binop_mod(); break;
            case TOK_LESS:
                if (is_unsigned_binop) binop_cmp(0x92); else binop_cmp(0x9C); break;
            case TOK_GREATER:
                if (is_unsigned_binop) binop_cmp(0x97); else binop_cmp(0x9F); break;
            case TOK_LESS_EQ:
                if (is_unsigned_binop) binop_cmp(0x96); else binop_cmp(0x9E); break;
            case TOK_GREATER_EQ:
                if (is_unsigned_binop) binop_cmp(0x93); else binop_cmp(0x9D); break;
            case TOK_EQ_EQ:      binop_cmp(0x94); break;
            case TOK_NOT_EQ:     binop_cmp(0x95); break;
            case TOK_LESS_LESS:       binop_shl(); break;
            case TOK_GREATER_GREATER:
                if (is_unsigned_binop) binop_shr(); else binop_sar(); break;
            case TOK_AMPERSAND: binop_and(); break;
            case TOK_PIPE:      binop_or();  break;
            case TOK_CARET:     binop_xor(); break;
            default: break;
            } }

        /* 传播无符号性和类型大小到二元运算结果 */
        if (node->left && node->right) {
            if (is_unsigned_binop) {
                node->is_unsigned = 1;
            }
            /* 传播 64-bit 类型（结果类型取操作数中较大的） */
            if (node->left->type_size == 8 || node->right->type_size == 8) {
                if (node->op != TOK_LESS && node->op != TOK_GREATER &&
                    node->op != TOK_LESS_EQ && node->op != TOK_GREATER_EQ &&
                    node->op != TOK_EQ_EQ && node->op != TOK_NOT_EQ)
                    node->type_size = 8;
            }
        }

        /* 指针算术结果传播 elem_size（供后续 DEREF 解引用宽度使用） */
        if ((node->op == TOK_PLUS || node->op == TOK_MINUS) &&
            node->left && node->right &&
            !node->left->is_float && !node->right->is_float &&
            (node->left->type_size == 8 || node->right->type_size == 8)) {
            /* 找出真正的指针操作数（可能两侧都是 type_size==8，如 ptr + long）
             * 并从局部/全局变量表获取其 element_size */
            int ptr_elem = 0;
            if (node->left->type_size == 8) {
                if (node->left->kind == AST_VAR && node->left->name) {
                    int vi; SEARCH_LOCAL(vi, node->left->name);
                    if (vi >= 0) ptr_elem = locals[vi].element_size;
                    if (vi < 0) {
                        for (vi = 0; vi < sym_count; vi++) {
                            if (syms[vi].name && strcmp(syms[vi].name, node->left->name) == 0) {
                                if (vi < MAX_SYMS && global_elem_size[vi] > 0) ptr_elem = global_elem_size[vi];
                                else if (vi < MAX_SYMS && global_ptr_elem_size[vi] > 0) ptr_elem = global_ptr_elem_size[vi];
                                break;
                            }
                        }
                    }
                }
                /* Cast 可能改变了 elem_size，优先使用 AST 节点值 */
                if (node->left->elem_size > 0)
                    ptr_elem = node->left->elem_size;
                else if (ptr_elem == 0) ptr_elem = node->left->elem_size;
            }
            if (ptr_elem == 0 && node->right->type_size == 8) {
                if (node->right->kind == AST_VAR && node->right->name) {
                    int vi; SEARCH_LOCAL(vi, node->right->name);
                    if (vi >= 0) ptr_elem = locals[vi].element_size;
                    if (vi < 0) {
                        for (vi = 0; vi < sym_count; vi++) {
                            if (syms[vi].name && strcmp(syms[vi].name, node->right->name) == 0) {
                                if (vi < MAX_SYMS && global_elem_size[vi] > 0) ptr_elem = global_elem_size[vi];
                                else if (vi < MAX_SYMS && global_ptr_elem_size[vi] > 0) ptr_elem = global_ptr_elem_size[vi];
                                break;
                            }
                        }
                    }
                }
                /* Cast 可能改变了 elem_size，优先使用 AST 节点值 */
                if (node->right->elem_size > 0)
                    ptr_elem = node->right->elem_size;
                else if (ptr_elem == 0) ptr_elem = node->right->elem_size;
            }
            node->elem_size = ptr_elem > 0 ? ptr_elem : 1;
        }

        /* 指针减法：q-p 结果需要除以元素大小（以元素个数为单位的差值） */
        if (node->op == TOK_MINUS &&
            node->left && node->left->type_size == 8 &&
            node->right && node->right->type_size == 8) {
            /* 确认右操作数确实是指针（ptr - ptr），而非 ptr - long：
             * 若右操作数不是指针（element_size == 0），则这是 ptr - int64
             * 形式的指针算术，已在上面做了缩放，不应再除以 elem_size。 */
            int right_is_ptr = 0;
            if (node->right->kind == AST_VAR && node->right->name) {
                int vi;
                SEARCH_LOCAL(vi, node->right->name);
                if (vi >= 0) { if (locals[vi].element_size > 0) right_is_ptr = 1; }
                if (vi < 0) {
                    for (vi = 0; vi < sym_count; vi++) {
                        if (syms[vi].name && strcmp(syms[vi].name, node->right->name) == 0) {
                            if (vi < MAX_SYMS && (global_elem_size[vi] > 0 || global_ptr_elem_size[vi] > 0))
                                { right_is_ptr = 1; break; }
                        }
                    }
                }
            }
            if (!right_is_ptr && node->right->elem_size > 0)
                right_is_ptr = 1;
            if (right_is_ptr) {
                /* 查找指针元素大小 */
                int ptelem = 1;
                if (node->left->kind == AST_VAR && node->left->name) {
                    int vi;
                    SEARCH_LOCAL(vi, node->left->name);
                    if (vi >= 0) {
                            if (locals[vi].element_size > 0) ptelem = locals[vi].element_size;

                    }
                    /* 全局变量 fallback */
                    if (vi < 0) {
                        for (vi = 0; vi < sym_count; vi++) {
                            if (syms[vi].name && strcmp(syms[vi].name, node->left->name) == 0) {
                                if (vi < MAX_SYMS && global_elem_size[vi] > 0)
                                    ptelem = global_elem_size[vi];
                                else if (vi < MAX_SYMS && global_ptr_elem_size[vi] > 0)
                                    ptelem = global_ptr_elem_size[vi];
                                break;
                            }
                        }
                    }
                }
                /* 优先使用节点上的 elem_size（反映转型后的实际指向类型，如 (char*)int_ptr） */
                if (node->left->elem_size > 0) {
                    ptelem = node->left->elem_size;
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
        }
        /* Signed char result truncation for cast expressions.
         * When (char)expr sets type_size=1 but the value is still 32-bit,
         * truncate to signed char range and sign-extend. */
        if (node->type_size == 1 && !node->is_unsigned &&
            node->op != TOK_EQ_EQ && node->op != TOK_NOT_EQ &&
            node->op != TOK_LESS && node->op != TOK_GREATER &&
            node->op != TOK_LESS_EQ && node->op != TOK_GREATER_EQ) {
            e1(0x0F); e1(0xBE); e1(0xC0);  /* movsbl %al, %eax */
        }
        break;
    }

    case AST_UNARY: {
        /* &expr: 直接取地址，不经过子表达式求值（避免 struct 数组退化导致的重复 LEA） */
        if (node->op == TOK_AMPERSAND) {
            node->type_size = 8;
            if (node->expr && node->expr->kind == AST_VAR) {
                int found = 0;
                int i;
                SEARCH_LOCAL(i, node->expr->name);
                if (i >= 0) {
                        lea_from_rbp(locals[i].offset);
                        found = 1;

                }
                if (!found && node->expr->name) {
                    int sym_idx = -1;
                    for (i = 0; i < sym_count; i++) {
                        if (syms[i].name && strcmp(syms[i].name, node->expr->name) == 0)
                            { sym_idx = i; break; }
                    }
                    if (sym_idx < 0 && sym_count < MAX_SYMS) {
                        sym_idx = sym_count;
                        CgenSym *s = &syms[sym_count++];
                        s->name = node->expr->name; s->offset = 0; s->size = 0;
                        s->is_global = 1; s->is_func = 0;
                        s->shndx = 0; s->sym_idx = -1;
                    }
                    int reloc_off = code_size;
                    e1(0xB8); e1(0x55); e1(0x66); e1(0x77); e1(0x88);
                    if (sym_idx >= 0 && rel_count < MAX_RELS) {
                        Elf64_Rela *r = &rels[rel_count++];
                        r->r_offset = reloc_off + 1;
                        r->r_info = ELF64_R_INFO(sym_idx + 1, R_X86_64_32);
                        r->r_addend = 0;
                    }
                }
            } else if (node->expr) {
                cgen_addr(node->expr);
            }
            break;
        }
        /* 保存子表达式的类型信息。对 (double *)(base + off) 模式，
         * 解析器在 cast 时设置了 expr->elem_size=8，但随后的 BINOP
         * 指针算术代码生成（line 1342-1388）会覆盖 elem_size 为基指针的
         * element_size（例如 char*→1），使 TOK_STAR 解引用时读不到正确
         * 的加载宽度。此处保存 cast 设置的原始值。 */
        int _saved_expr_elem_size  = node->expr ? node->expr->elem_size : 0;
        int _saved_expr_elem_float = node->expr ? node->expr->elem_is_float : 0;
        int _saved_expr_unsigned   = node->expr ? node->expr->is_unsigned : 0;
        cgen_expr(node->expr);  /* 子表达式结果在 eax 或 xmm0 */
        switch (node->op) {
        case TOK_MINUS:
            if (node->expr && node->expr->is_float) {
                if (node->expr->is_float == 4) {
                    /* float 求反：xorps with 0x80000000 sign bit */
                    e1(0xB8); e4(0x80000000);             /* mov eax, 0x80000000 */
                    e1(0x66); e1(0x0F); e1(0x6E); e1(0xC8);  /* movd xmm1, eax */
                    e1(0x0F); e1(0x57); e1(0xC1);            /* xorps xmm0, xmm1 */
                } else {
                    negate_double();
                }
                node->is_float = node->expr->is_float;
            } else if (node->expr && node->expr->type_size == 8) {
                unop_neg64();
            } else {
                unop_neg64();  /* 始终用 64 位求反：32 位 neg eax 会零扩展高 32 位 */
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
                if (node->expr->is_float == 4) {
                    /* !float_val: xorps xmm1,xmm1; ucomiss xmm1,xmm0; sete al */
                    e1(0x0F); e1(0x57); e1(0xC9);            /* xorps xmm1, xmm1 */
                    e1(0x0F); e1(0x2E); e1(0xC8);            /* ucomiss xmm1, xmm0 */
                } else {
                    /* !double_val: xorpd xmm1,xmm1; ucomisd xmm1,xmm0 */
                    e1(0x66); e1(0x0F); e1(0x57); e1(0xC9);  /* xorpd xmm1, xmm1 */
                    e1(0x66); e1(0x0F); e1(0x2E); e1(0xC8);  /* ucomisd xmm1, xmm0 */
                }
                e1(0x0F); e1(0x94); e1(0xC0);            /* sete al */
                e1(0x0F); e1(0xB6); e1(0xC0);            /* movzx eax, al */
            } else if (node->expr && node->expr->type_size == 8) {
                /* !ptr: test rax, rax; sete al; movzx eax, al */
                e1(0x48); e1(0x85); e1(0xC0);  /* test rax, rax */
                e1(0x0F); e1(0x94); e1(0xC0);           /* sete al */
                e1(0x0F); e1(0xB6); e1(0xC0);           /* movzx eax, al */
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
             * 用 cgen_addr 统一处理 local/global/struct-member/ptr (通过指针) */
            int sz = (node->expr && node->expr->type_size == 8) ? 8 : 4;
            int expr_is_float = node->expr ? node->expr->is_float : 0;

            if (expr_is_float) {
                /* 浮点自增/自减：使用 SSE addss/addsd / subss/subsd */
                int is_ss = (expr_is_float == 4);

                cgen_addr(node->expr);       /* rax = 目标地址 */
                push_rax();
                pop_rcx();                   /* rcx = 地址 */

                if (node->is_postfix) {
                    /* 保存旧值到栈 */
                    if (is_ss) {
                        e1(0xF3); e1(0x0F); e1(0x10); e1(0x01);  /* movss xmm0, [rcx] */
                    } else {
                        e1(0xF2); e1(0x0F); e1(0x10); e1(0x01);  /* movsd xmm0, [rcx] */
                    }
                    push_xmm0();              /* 保存旧 xmm0 到栈 */
                }

                /* 加载值到 xmm0 */
                if (is_ss) {
                    e1(0xF3); e1(0x0F); e1(0x10); e1(0x01);  /* movss xmm0, [rcx] */
                } else {
                    e1(0xF2); e1(0x0F); e1(0x10); e1(0x01);  /* movsd xmm0, [rcx] */
                }

                /* 创建常数 1.0 在 xmm1: cvtsi2ss/sd xmm1, eax (eax=1) */
                e1(0xB8); e1(0x01); e1(0x00); e1(0x00); e1(0x00);  /* mov eax, 1 */
                e1(0xF2 | (is_ss ? 1 : 0)); e1(0x0F); e1(0x2A); e1(0xC8);  /* cvtsi2ss/sd xmm1, eax */

                /* xmm0 = xmm0 +/- 1.0 */
                int op_prefix = is_ss ? 0xF3 : 0xF2;
                if (node->op == TOK_PLUS_PLUS) {
                    e1(op_prefix); e1(0x0F); e1(0x58); e1(0xC1);  /* addss/addsd xmm0, xmm1 */
                } else {
                    e1(op_prefix); e1(0x0F); e1(0x5C); e1(0xC1);  /* subss/subsd xmm0, xmm1 */
                }

                /* 写回 */
                if (is_ss) {
                    e1(0xF3); e1(0x0F); e1(0x11); e1(0x01);  /* movss [rcx], xmm0 */
                } else {
                    e1(0xF2); e1(0x0F); e1(0x11); e1(0x01);  /* movsd [rcx], xmm0 */
                }

                if (node->is_postfix) {
                    pop_xmm0();  /* xmm0 = 旧值 */
                }

                node->type_size = expr_is_float;
                node->is_float = expr_is_float;
                break;
            }

            /* 整数自增/自减路径 */
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
                /* 推测被指向的类型大小、浮点性和符号性。
                 * 在局部变量表中查找指针变量的 element_size：
                 * int * → 4, char * → 1, long * / double * → 8 */
                int deref_size = 1;  /* 默认 char* 解引用 */
                int elem_unsigned = 0;  /* 默认 signed */
                int elem_float = 0;     /* 0=非浮点, 4=float, 8=double */
                if (node->expr->kind == AST_VAR && node->expr->name) {
                    int vi;
                    SEARCH_LOCAL(vi, node->expr->name);
                    if (vi >= 0) {
                            if (locals[vi].element_size > 0)
                                deref_size = locals[vi].element_size;
                            elem_unsigned = locals[vi].elem_is_unsigned;
                            elem_float = locals[vi].elem_is_float;

                    }
                }
                /* ptr + int 表达式（如 fp+1）：检查基变量的 elem_is_float */
                if (node->expr->kind == AST_BINOP && node->expr->left &&
                    node->expr->left->kind == AST_VAR && node->expr->left->name) {
                    int vi;
                    SEARCH_LOCAL(vi, node->expr->left->name);
                    if (vi >= 0) {
                            elem_float = locals[vi].elem_is_float;
                            if (locals[vi].element_size > 0)
                                deref_size = locals[vi].element_size;
                    }
                }
                /* cast override: 优先使用代码生成前保存的 AST 节点值。
                 * 对 (double*)(base+off) 模式，BINOP 指针算术代码生成会覆盖
                 * elem_size（例：base 为 char* 时 element_size=1 → 覆盖了
                 * cast 设的 8），因此不能在此处读 node->expr->elem_size。
                 * 用 cgen_expr 之前保存的 _saved_expr_elem_size。 */
                if (_saved_expr_elem_size > 0) {
                    deref_size = _saved_expr_elem_size;
                    elem_unsigned = _saved_expr_unsigned;
                }
                /* fallback: 用代码生成后的 node->expr->elem_size。
                 * 对 *ptrs[0]（ptr 数组）模式，BINOP 的 elem_size 被
                 * base_elem_size 传播代码（line 843）设为正确解引用宽度。 */
                else if (node->expr->elem_size > 0) {
                    deref_size = node->expr->elem_size;
                    elem_unsigned = node->expr->is_unsigned;
                }
                /* 使用保存的 elem_is_float（解析器对指针类型转换设置，
                 * BINOP 代码生成不覆盖 elem_is_float）。 */
                if (_saved_expr_elem_float > 0)
                    elem_float = _saved_expr_elem_float;
                if (elem_float) {
                    /* float/double 指针解引用 */
                    if (elem_float == 4) {
                        e1(0xF3); e1(0x0F); e1(0x10); e1(0x00);  /* movss xmm0, [rax] */
                    } else {
                        e1(0xF2); e1(0x0F); e1(0x10); e1(0x00);  /* movsd xmm0, [rax] */
                    }
                } else if (deref_size == 1) {
                    if (elem_unsigned) {
                        e1(0x0F); e1(0xB6); e1(0x00);  /* movzbl (%rax), %eax — 零扩展字节加载 */
                    } else {
                        e1(0x0F); e1(0xBE); e1(0x00);  /* movsbl (%rax), %eax — 符号扩展字节加载 */
                    }
                } else if (deref_size == 8) {
                    /* mov rax, [rax] — 64-bit 加载 */
                    e1(0x48); e1(0x8B); e1(0x00);
                } else if (deref_size == 2) {
                    if (elem_unsigned) {
                        e1(0x0F); e1(0xB7); e1(0x00);  /* movzwl (%rax), %eax — 零扩展字加载 */
                    } else {
                        e1(0x0F); e1(0xBF); e1(0x00);  /* movswl (%rax), %eax — 符号扩展字加载 */
                    }
                } else {
                    /* mov (%rax), %eax — 32-bit 加载（有符号无符号相同） */
                    e1(0x8B); e1(0x00);
                }
                /* 传播元数据 */
                node->is_unsigned = elem_unsigned;
                if (elem_float) node->is_float = elem_float;
                node->type_size = deref_size;
            }
            break;
        default:
            /* 类型转换：包装节点，解析器在 parse_unary 中创建了
             * AST_UNARY (op=0) 作为包装器，expr 指向内层表达式。 */
            if (node->is_float && node->expr && !node->expr->is_float) {
                /* int→float/double：内层整数在 eax 中 */
                if (node->is_float == 4)
                    cvti2f();  /* cvtsi2ss xmm0, eax */
                else
                    cvti2d();  /* cvtsi2sd xmm0, eax */
            } else if (!node->is_float && node->expr && node->expr->is_float &&
                       node->type_size > 0 && node->type_size < 8) {
                /* float/double→int/char/short */
                if (node->expr->is_float == 4) {
                    e1(0xF3); e1(0x0F); e1(0x2C); e1(0xC0);  /* cvttss2si eax, xmm0 */
                } else {
                    e1(0xF2); e1(0x0F); e1(0x2C); e1(0xC0);  /* cvttsd2si eax, xmm0 */
                }
                node->is_float = 0;
                if (node->type_size == 1) {
                    if (node->is_unsigned)
                        { e1(0x0F); e1(0xB6); e1(0xC0); }  /* movzbl %al, %eax */
                    else
                        { e1(0x0F); e1(0xBE); e1(0xC0); }  /* movsbl %al, %eax */
                } else if (node->type_size == 2) {
                    if (node->is_unsigned)
                        { e1(0x0F); e1(0xB7); e1(0xC0); }  /* movzwl %ax, %eax */
                    else
                        { e1(0x0F); e1(0xBF); e1(0xC0); }  /* movswl %ax, %eax */
                }
            } else if (!node->is_float && node->expr && node->expr->is_float &&
                       node->type_size == 8) {
                /* double→long (64-bit)：REX.W cvttsd2si rax, xmm0 */
                if (node->expr->is_float == 4) {
                    e1(0xF3); e1(0x48); e1(0x0F); e1(0x2C); e1(0xC0);  /* cvttss2si rax, xmm0 */
                } else {
                    e1(0xF2); e1(0x48); e1(0x0F); e1(0x2C); e1(0xC0);  /* cvttsd2si rax, xmm0 */
                }
                node->is_float = 0;
            } else if (node->is_float && node->expr && node->expr->is_float &&
                       node->is_float != node->expr->is_float) {
                /* float↔double 互转 */
                if (node->is_float == 4) {
                    e1(0xF2); e1(0x0F); e1(0x5A); e1(0xC0);  /* cvtsd2ss xmm0, xmm0 */
                } else {
                    e1(0xF3); e1(0x0F); e1(0x5A); e1(0xC0);  /* cvtss2sd xmm0, xmm0 */
                }
            }
            break;
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
            int elem_float = 0;  /* 数组元素类型是否为 float/double */
            AstNode *arr_base = node->left->left;
            if (arr_base && arr_base->kind == AST_VAR) {
                int i;
                SEARCH_LOCAL(i, arr_base->name);
                if (i >= 0) {
                        if (locals[i].element_size > 0)
                            elem_size = locals[i].element_size;
                        if (locals[i].is_array && locals[i].is_float)
                            elem_float = locals[i].is_float;

                }
                if (i < 0) {
                    for (i = 0; i < sym_count; i++) {
                        if (syms[i].name && strcmp(syms[i].name, arr_base->name) == 0) {
                            if (i < MAX_SYMS && global_elem_size[i] > 0) {
                                elem_size = global_elem_size[i];
                                elem_float = global_elem_float[i];
                            } else if (i < MAX_SYMS && global_ptr_elem_size[i] > 0)
                                elem_size = global_ptr_elem_size[i];
                            break;
                        }
                    }
                }
            } else if (arr_base && arr_base->kind == AST_BINOP &&
                       arr_base->op == TOK_LBRACKET &&
                       arr_base->left && arr_base->left->kind == AST_VAR) {
                /* 多维数组外层下标：使用 base_elem_size */
                int i;
                SEARCH_LOCAL(i, arr_base->left->name);
                if (i >= 0) {
                        if (locals[i].base_elem_size > 0)
                            elem_size = locals[i].base_elem_size;
                        else if (locals[i].element_size > 0)
                            elem_size = locals[i].element_size;
                        if (locals[i].is_array && locals[i].is_float)
                            elem_float = locals[i].is_float;
                } else {
                    /* 全局变量 double subscript */
                    for (i = 0; i < sym_count; i++) {
                        if (syms[i].name && strcmp(syms[i].name, arr_base->left->name) == 0) {
                            if (i < MAX_SYMS && global_base_elem_size[i] > 0) {
                                elem_size = global_base_elem_size[i];
                                elem_float = global_elem_float[i];
                            }
                            break;
                        }
                    }
                }
            }
            /* fallback: 结构体成员数组 s.arr[i] — 使用 AST_MEMBER 上的 elem_size */
            if (elem_size == 1 && arr_base &&
                !(arr_base->kind == AST_BINOP && arr_base->op == TOK_LBRACKET) &&
                arr_base->elem_size > 0) {
                elem_size = arr_base->elem_size;
            }
            /* fallback: 从 arr_base AST 节点传播 is_float（如结构体成员数组 s.arr[i]） */
            if (elem_float == 0 && arr_base && arr_base->is_float)
                elem_float = arr_base->is_float;
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
                    e1(0xB8); e4(elem_size);
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

            /* 存储到目标地址 — 用 elem_size 决定存储宽度（数组元素可能是 long） */
            pop_rcx();  /* rcx = 目标地址 */
            /* 存储宽度由元素大小决定（char=1, short=2, int=4, long=8），
             * 不能使用 RHS 的类型大小：char[i] = long_expr 应只存 1 字节，
             * 否则会覆写相邻内存。 */
            int store_sz = elem_size > 0 ? elem_size : 4;
            /* int→float 转换：数组元素是 float/double 但 RHS 是 int */
            if (elem_float && !rhs_float) {
                if (elem_float == 4) cvti2f(); else cvti2d();
                rhs_float = elem_float;
            }
            if (rhs_float) {
                /* float/double 数组元素存储：根据元素宽度选择 movss/movsd。
                 * store_sz == 4 → float 数组，用 movss（4 字节）
                 * store_sz >= 8 → double 数组，用 movsd（8 字节） */
                if (store_sz == 4) {
                    e1(0xF3); e1(0x0F); e1(0x11); e1(0x01);  /* movss [rcx], xmm0 */
                } else {
                    e1(0xF2); e1(0x0F); e1(0x11); e1(0x01);  /* movsd [rcx], xmm0 */
                }
            } else if (store_sz > 8) {
                /* 大结构体赋值：RAX=源地址(隐藏缓冲区), RCX=目标地址 */
                e1(0x48); e1(0x89); e1(0xC6);  /* mov rsi, rax */
                e1(0x48); e1(0x89); e1(0xCF);  /* mov rdi, rcx */
                e1(0xB9); e4(store_sz);         /* mov ecx, store_sz */
                e1(0xF3); e1(0xA4);            /* rep movsb */
            } else if (store_sz >= 8) {
                if (!rhs_float && node->right && node->right->type_size < 8 && !node->right->is_unsigned)
                    { e1(0x48); e1(0x63); e1(0xC0); }  /* movsxd rax, eax */
                e1(0x48); e1(0x89); e1(0x01);  /* mov [rcx], rax */
            } else if (elem_size == 1) {
                e1(0x88); e1(0x01);             /* mov [rcx], al */
            } else if (elem_size == 2) {
                e1(0x66); e1(0x89); e1(0x01);   /* mov [rcx], ax */
            } else {
                e1(0x89); e1(0x01);             /* mov [rcx], eax */
            }
            node->type_size = store_sz;
            break;
        }

        /* 指针解引用赋值 *ptr = expr */
        if (node->left && node->left->kind == AST_UNARY &&
            node->left->op == TOK_STAR) {
            cgen_expr(node->left->expr);       /* 指针 → rax */
            push_rax();
            cgen_expr(node->right);
            pop_rcx();                          /* rcx = 目标地址 */
            /* 确定被指向类型的大小（同 TOK_STAR 加载逻辑） */
            int deref_sz = 1;
            if (node->left->expr && node->left->expr->kind == AST_VAR &&
                node->left->expr->name) {
                int vi;
                SEARCH_LOCAL(vi, node->left->expr->name);
                if (vi >= 0) {
                        if (locals[vi].element_size > 0)
                            deref_sz = locals[vi].element_size;

                }
                /* 检查 AST 节点上的强转注解（如 *(long *)p = value）。
                 * 解析器在 (type *) 强转时会在 inner 节点上设置 elem_size，
                 * 该值覆盖变量的声明元素大小。 */
                if (node->left->expr->elem_size > 0)
                    deref_sz = node->left->expr->elem_size;
            } else {
                /* fallback: 用 RHS 类型推断 */
                deref_sz = node->right ? node->right->type_size : 4;
                if (deref_sz == 0) deref_sz = 4;
            }
            /* 检查是否指向 float/double：优先从局部变量表获取 elem_is_float */
            int ptr_elem_float = 0;
            if (node->left->expr && node->left->expr->kind == AST_VAR) {
                int vi;
                SEARCH_LOCAL(vi, node->left->expr->name);
                if (vi >= 0) ptr_elem_float = locals[vi].elem_is_float;
            }
            /* fallback: AST 节点上的 elem_is_float（如 (float*) 强转） */
            if (ptr_elem_float == 0 && node->left->expr && node->left->expr->elem_is_float > 0)
                ptr_elem_float = node->left->expr->elem_is_float;
            if (ptr_elem_float) {
                if (!rhs_float) {
                    if (ptr_elem_float == 4) cvti2f(); else cvti2d();
                }
                if (ptr_elem_float == 4) {
                    e1(0xF3); e1(0x0F); e1(0x11); e1(0x01);  /* movss [rcx], xmm0 */
                } else {
                    e1(0xF2); e1(0x0F); e1(0x11); e1(0x01);  /* movsd [rcx], xmm0 */
                }
            } else if (rhs_float) {
                e1(0xF2); e1(0x0F); e1(0x11); e1(0x01);  /* movsd [rcx], xmm0 */
            } else if (deref_sz >= 8) {
                if (!rhs_float && node->right && node->right->type_size < 8 && !node->right->is_unsigned)
                    { e1(0x48); e1(0x63); e1(0xC0); }  /* movsxd rax, eax */
                e1(0x48); e1(0x89); e1(0x01);  /* mov [rcx], rax */
            } else if (deref_sz == 1) {
                e1(0x88); e1(0x01);             /* mov [rcx], al */
            } else if (deref_sz == 2) {
                e1(0x66); e1(0x89); e1(0x01);   /* mov [rcx], ax */
            } else {
                e1(0x89); e1(0x01);             /* mov [rcx], eax */
            }
            break;
        }

        /* 大结构体赋值给 >8 字节局部变量：用 cgen_addr 获取 RHS 的源地址，
         * 避免 cgen_expr 对 *ptr 解引用时的加载语义错误。与 cgen.c AST_VAR_DECL
         * 中的修复同理。 */
        if (node->left && node->left->kind == AST_VAR) {
            int _vi;
            SEARCH_LOCAL(_vi, node->left->name);
            if (_vi >= 0 && locals[_vi].size > 8) {
                cgen_addr(node->right);            /* rax = 源地址 */
                e1(0x48); e1(0x89); e1(0xC6);     /* mov rsi, rax */
                if (disp8_fits(locals[_vi].offset))
                    { e1(0x48); e1(0x8D); e1(0x7D); e1(locals[_vi].offset & 0xFF); }
                else
                    { e1(0x48); e1(0x8D); e1(0xBD); e4(locals[_vi].offset); }
                e1(0xB9); e4(locals[_vi].size);     /* mov ecx, size */
                e1(0xF3); e1(0xA4);                /* rep movsb */
                node->type_size = locals[_vi].size;
                break;
            }
        }

        cgen_expr(node->right);
        if (node->left && node->left->kind == AST_VAR) {
            const char *vname = node->left->name;
            int i;
            SEARCH_LOCAL(i, vname);
            if (i >= 0) {
                    if (locals[i].is_float) {
                        /* 右操作数可能是 int，需要转换 */
                        if (!rhs_float) {
                            if (locals[i].is_float == 4) cvti2f(); else cvti2d();
                        }
                        store_double_to_rbp(locals[i].offset);
                    } else {
                        /* 右操作数可能是 float/double，需要转换 */
                        if (rhs_float) {
                            if (locals[i].size == 8) {
                                /* long (64-bit)：用 REX.W */
                                if (node->right && node->right->is_float == 4) {
                                    e1(0xF3); e1(0x48); e1(0x0F); e1(0x2C); e1(0xC0);  /* cvttss2si rax, xmm0 */
                                } else {
                                    e1(0xF2); e1(0x48); e1(0x0F); e1(0x2C); e1(0xC0);  /* cvttsd2si rax, xmm0 */
                                }
                            } else {
                                if (node->right && node->right->is_float == 4) {
                                    e1(0xF3); e1(0x0F); e1(0x2C); e1(0xC0);  /* cvttss2si eax, xmm0 */
                                } else {
                                    e1(0xF2); e1(0x0F); e1(0x2C); e1(0xC0);  /* cvttsd2si eax, xmm0 */
                                }
                            }
                        }
                        /* int → long：有符号整型赋值给 64 位变量时符号扩展 */
                        if (locals[i].size == 8 && !rhs_float &&
                            node->right && node->right->type_size < 8) {
                            int do_sext = 0;
                            if (node->right->kind == AST_VAR && !node->right->is_unsigned) {
                                do_sext = 1;  /* 有符号 int 变量 → long */
                            } else if (node->right->kind == AST_CONSTANT &&
                                       node->right->ival >= -2147483648L &&
                                       node->right->ival <= 2147483647L &&
                                       !node->right->is_unsigned) {
                                do_sext = 1;  /* 有符号 32 位常量 → long */
                            } else if (!node->right->is_unsigned) {
                                do_sext = 1;  /* 其他有符号 int 表达式 → long */
                            }
                            if (do_sext)
                                { e1(0x48); e1(0x63); e1(0xC0); }  /* movsxd rax, eax */
                        }
                        if (locals[i].size > 8) {
                            /* 结构体直接赋值：cgen_expr(right) 对 > 8 字节变量
                             * 返回指针（lea rax, [rbp+src_off]）。
                             * 从 [RAX] 拷贝 locals[i].size 字节到目标变量。 */
                            e1(0x48); e1(0x89); e1(0xC6);  /* mov rsi, rax */
                            if (disp8_fits(locals[i].offset))
                                { e1(0x48); e1(0x8D); e1(0x7D); e1(locals[i].offset & 0xFF); }
                            else
                                { e1(0x48); e1(0x8D); e1(0xBD); e4(locals[i].offset); }
                            e1(0xB9); e4(locals[i].size);  /* mov ecx, size */
                            e1(0xF3); e1(0xA4);            /* rep movsb */
                        } else if (locals[i].size == 8) {
                            store_rax_to_rbp(locals[i].offset);
                        } else if (locals[i].size == 1) {
                            /* char: mov [rbp+off], al */
                            if (disp8_fits(locals[i].offset))
                                { e1(0x88); e1(0x45); e1(locals[i].offset & 0xFF); }
                            else
                                { e1(0x88); e1(0x85); e4(locals[i].offset); }
                        } else if (locals[i].size == 2) {
                            /* short: mov [rbp+off], ax */
                            e1(0x66);
                            if (disp8_fits(locals[i].offset))
                                { e1(0x89); e1(0x45); e1(locals[i].offset & 0xFF); }
                            else
                                { e1(0x89); e1(0x85); e4(locals[i].offset); }
                        } else {
                            store_eax_to_rbp(locals[i].offset);
                        }
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
                    /* 浮点全局变量赋值 */
                    if (node->left && node->left->is_float) {
                        if (!rhs_float) {
                            if (node->left->is_float == 4) cvti2f(); else cvti2d();
                        }
                        if (node->left->is_float == 4) {
                            e1(0xF3); e1(0x0F); e1(0x11); e1(0x05);  /* movss [rip+disp32], xmm0 */
                        } else {
                            e1(0xF2); e1(0x0F); e1(0x11); e1(0x05);  /* movsd [rip+disp32], xmm0 */
                        }
                        int ro = code_size;
                        e1(0x55); e1(0x66); e1(0x77); e1(0x88);
                        if (rel_count < MAX_RELS) {
                            Elf64_Rela *r = &rels[rel_count++];
                            r->r_offset = ro;
                            r->r_info = ELF64_R_INFO(si + 1, R_X86_64_PC32);
                            r->r_addend = -4;
                        }
                        break;
                    }
                    /* 使用变量的声明大小决定存储宽度 */
                    int var_size = syms[si].size;
                    int store_width = (rhs_size == 8 ||
                                 (node->right && node->right->type_size == 8) ||
                                 var_size == 8) ? 8 : var_size;
                    if (store_width == 0) store_width = 4;
                    if (store_width >= 8) {
                        if (!rhs_float && node->right && node->right->type_size < 8 && !node->right->is_unsigned)
                            { e1(0x48); e1(0x63); e1(0xC0); }  /* movsxd rax, eax */
                        e1(0x48); e1(0x89); e1(0x05);  /* mov [rip+disp32], rax */
                    } else if (store_width == 1) {
                        e1(0x88); e1(0x05);             /* mov [rip+disp32], al */
                    } else if (store_width == 2) {
                        e1(0x66); e1(0x89); e1(0x05);   /* mov [rip+disp32], ax */
                    } else {
                        e1(0x89); e1(0x05);             /* mov [rip+disp32], eax */
                    }
                    int ro = code_size;
                    e1(0x55); e1(0x66); e1(0x77); e1(0x88);
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
            if (rsize == 0) rsize = 4;

            push_rax();              /* 保存 RHS 值（>8 字节 struct 时 RAX 是源地址指针） */

            cgen_addr(node->left);   /* rax = 成员地址（覆写 eax） */

            e1(0x48); e1(0x89); e1(0xC1);  /* mov rcx, rax (rcx = 目标地址) */
            pop_rax();               /* rax = RHS 值（或源地址指针） */

            if (node->left->is_float) {
                /* float/double 成员赋值 */
                if (!rhs_float) {
                    if (node->left->is_float == 4) cvti2f(); else cvti2d();
                }
                if (node->left->is_float == 4) {
                    e1(0xF3); e1(0x0F); e1(0x11); e1(0x01);  /* movss [rcx], xmm0 */
                } else {
                    e1(0xF2); e1(0x0F); e1(0x11); e1(0x01);  /* movsd [rcx], xmm0 */
                }
            } else if (rsize > 8) {
                /* 大结构体成员赋值：RAX=源地址, RCX=目标地址
                 * mov rsi, rax; mov rdi, rcx; mov ecx, rsize; rep movsb */
                e1(0x48); e1(0x89); e1(0xC6);  /* mov rsi, rax */
                e1(0x48); e1(0x89); e1(0xCF);  /* mov rdi, rcx */
                e1(0xB9); e4(rsize);           /* mov ecx, rsize */
                e1(0xF3); e1(0xA4);            /* rep movsb */
            } else if (rsize >= 8) {
                if (!rhs_float && node->right && node->right->type_size < 8 && !node->right->is_unsigned)
                    { e1(0x48); e1(0x63); e1(0xC0); }  /* movsxd rax, eax */
                e1(0x48); e1(0x89); e1(0x01);  /* mov [rcx], rax */
            } else if (rsize == 1) {
                e1(0x88); e1(0x01);             /* mov [rcx], al */
            } else if (rsize == 2) {
                e1(0x66); e1(0x89); e1(0x01);   /* mov [rcx], ax */
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
                    SEARCH_LOCAL(vi, node->args->name);
                    if (vi >= 0) {
                        lea_from_rbp(locals[vi].offset);
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
                if (disp8_fits(reg_save_offset))
                    { e1(0x48); e1(0x8D); e1(0x45); e1(reg_save_offset & 0xFF); }
                else
                    { e1(0x48); e1(0x8D); e1(0x85); e4(reg_save_offset); }
                e1(0x48); e1(0x89); e1(0x41); e1(0x10);
                break;
            }
            if (strcmp(node->name, "__builtin_va_arg") == 0 && node->args) {
                /* 获取类型大小（第二个参数），默认 4 */
                int type_size = 4;
                int is_float_arg = 0;
                if (node->args->next && node->args->next->kind == AST_CONSTANT) {
                    type_size = node->args->next->ival;
                    is_float_arg = node->args->next->is_float;
                    /* 回退：若解析期 is_float 未正确传播，使用调用节点的启发式结果 */
                    if (!is_float_arg && node->is_float && node->is_float == type_size)
                        is_float_arg = node->is_float;
                }
                /* 回退：对 __builtin_va_arg(ap, double) 中 type_size=8
                 * 且调用节点的启发式结果 is_float=8 的情况强制使用浮点路径 */
                if (!is_float_arg && type_size == 8 && node->is_float == 8)
                    is_float_arg = 8;
                /* 默认参数提升：小于 4 升到 4，大于 8 截到 8 */
                if (type_size < 4) type_size = 4;
                if (type_size > 8) type_size = 8;

                cgen_expr(node->args);              /* rax = &ap */
                e1(0x48); e1(0x89); e1(0xC7);       /* mov rdi, rax — 保存 &ap */

                if (is_float_arg) {
                    /* 浮点路径：使用 fp_offset（ap+4），从 48 开始，最大 48+8*8=112 */
                    e1(0x8B); e1(0x47); e1(0x04);   /* mov eax, [rdi+4] — fp_offset */
                    e1(0x83); e1(0xF8); e1(0x70);   /* cmp eax, 112 */
                    int _f_jge_ofs;
                    { int _pos = code_size; e1(0x7D); _f_jge_ofs = code_size; e1(0); }

                    /* 寄存器路径（fp_offset < 112）：从 reg_save_area[fp_offset] 加载到 xmm0 */
                    e1(0x48); e1(0x8B); e1(0x4F); e1(0x10); /* mov rcx, [rdi+0x10] — reg_save_area */
                    /* eax 是 fp_offset（零扩展到了 rax），直接用作地址偏移 */
                    e1(0xF2); e1(0x0F); e1(0x10); e1(0x04); e1(0x01); /* movsd xmm0, [rcx+rax] */
                    e1(0x83); e1(0x47); e1(0x04); e1(0x08); /* add dword [rdi+4], 8 — fp_offset += 8 */
                    int _f_jmp_ofs;
                    { int _pos = code_size; e1(0xEB); _f_jmp_ofs = code_size; e1(0); }

                    /* Overflow 路径 */
                    code_buf[_f_jge_ofs] = code_size - _f_jge_ofs - 1;
                    e1(0x48); e1(0x8B); e1(0x47); e1(0x08); /* mov rax, [rdi+0x08] — overflow_arg_area */
                    e1(0xF2); e1(0x0F); e1(0x10); e1(0x00); /* movsd xmm0, [rax] */
                    e1(0x48); e1(0x83); e1(0x47); e1(0x08); e1(0x08); /* add qword [rdi+0x08], 8 */
                    code_buf[_f_jmp_ofs] = code_size - _f_jmp_ofs - 1;
                } else {
                    /* 整数路径：使用 gp_offset */
                    e1(0x8B); e1(0x0F);                 /* mov ecx, [rdi] — gp_offset */
                    e1(0x83); e1(0xF9); e1(0x30);       /* cmp ecx, 48 */
                    int _jge_ofs;
                    { int _pos = code_size; e1(0x7D); _jge_ofs = code_size; e1(0); }

                    /* --- 寄存器路径（gp_offset < 48）--- */
                    e1(0x48); e1(0x8B); e1(0x47); e1(0x10); /* mov rax, [rdi+0x10] — reg_save_area */
                    e1(0x48); e1(0x8B); e1(0x04); e1(0x08); /* mov rax, [rax+rcx]（64 位） */
                    e1(0x83); e1(0x07); e1(8);          /* add dword [rdi], 8 — gp_offset += 8 */
                    int _jmp_ofs;
                    { int _pos = code_size; e1(0xEB); _jmp_ofs = code_size; e1(0); }

                    /* --- Overflow 路径（gp_offset >= 48）--- */
                    code_buf[_jge_ofs] = code_size - _jge_ofs - 1;
                    e1(0x48); e1(0x8B); e1(0x47); e1(0x08); /* mov rax, [rdi+0x08] — overflow_arg_area */
                    e1(0x48); e1(0x8B); e1(0x00);        /* mov rax, [rax]（64 位） */
                    e1(0x48); e1(0x83); e1(0x47); e1(0x08); e1(0x08);
                    code_buf[_jmp_ofs] = code_size - _jmp_ofs - 1;
                }
                /* 先传播 is_unsigned，再设置 type_size（确保两者都正确，
                 * 交换顺序排查 toyc 编译自身的结构体成员赋值 bug） */
                if (node->args->next && node->args->next->kind == AST_CONSTANT)
                    node->is_unsigned = node->args->next->is_unsigned;
                node->type_size = type_size;
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
            SEARCH_LOCAL(i, node->name);
            if (i >= 0) {
                    is_fptr = 1;
                    fptr_offset = locals[i].offset;

            }
        }

        int indirect_call = 0;  /* 需要压栈函数指针 → 间接调用 */
        if (is_fptr) {
            load_rax_from_rbp(fptr_offset);
            push_rax();
            indirect_call = 1;
        } else if (node->call_target && node->call_target->kind != AST_VAR) {
            /* 复杂表达式调用（如 ops[0](...)）：先求值表达式，压栈函数指针 */
            cgen_expr(node->call_target);
            push_rax();
            indirect_call = 1;
        }

        /* 检查被调函数是否返回大结构体（>8 字节），需要传递隐藏指针 */
        int has_hidden_ret = 0;
        int hidden_ret_size = 0;
        int hidden_alloc_size = 0;
        if (node->name && !is_fptr) {
            int rsz = get_func_ret_size(node->name);
            /* fallback: 解析期记录的原型返回类型（跨 TU 函数，非本文件定义的函数） */
            if (rsz == 0) {
                int pi;
                for (pi = 0; pi < parsed_func_ret_count; pi++) {
                    if (parsed_func_ret_names[pi] &&
                        strcmp(parsed_func_ret_names[pi], node->name) == 0) {
                        rsz = parsed_func_ret_sizes[pi];
                        break;
                    }
                }
            }
            if (rsz > 8) { has_hidden_ret = 1; hidden_ret_size = rsz; }
        }

        /* 求值参数：float 用 push_xmm0，int 用 push_rax
         *
         * 参数在栈上的布局决定了 pop 循环能否正确取出寄存器参数。
         * 当 argc ≤ 6 时单次循环即可。当 argc > 6 时：
         *
         *   必须 先 push 栈参数（索引 6+，它们本该在栈上供被调者访问），
         *   再 push 寄存器参数（索引 0-5，它们在栈顶方便 pop 到寄存器）。
         *
         * 错误示例（修复前）：先 push 0-5 再 push 6+，
         *   栈参数在栈顶，pop 循环先拿到栈参数，寄存器参数全部错位 1 位。
         */
        arg = node->args;
        int idx;
        /* Phase 1: push 栈参数（6+）先入栈 → 沉到栈底
         *
         * 必须逆序压入（最右边的栈参数先入栈 → 最深）。
         * 例子：sum8(a0..a5,a6=17,a7=19)
         *   正序 push: a6(17) → a7(19)  → [rbp+0x10]=19 ✗
         *   逆序 push: a7(19) → a6(17)  → [rbp+0x10]=17 ✓
         *
         * x86-64 ABI: 第一个栈参数 a6 在 [rbp+0x10]，
         * 第二个 a7 在 [rbp+0x18]，以此类推。 */
        {
            int nstack = argc > 6 ? argc - 6 : 0;
            if (nstack > 0) {
                AstNode *stack_nodes[12];
                AstNode *walker = node->args;
                int ni;
                for (ni = 0; walker && ni < 6; ni++) walker = walker->next;
                for (ni = 6; walker && ni < argc && (ni - 6) < 12; ni++) {
                    stack_nodes[ni - 6] = walker;
                    walker = walker->next;
                }
                for (int si = nstack - 1; si >= 0; si--) {
                    cgen_expr(stack_nodes[si]);
                    if (arg_is_float[6 + si])
                        push_xmm0();
                    else
                        push_rax();
                }
            }
        }
        /* Phase 3: 大结构体返回值 — 先分配返回空间并压入隐藏指针（沉到栈底，最后 pop 到 RDI） */
        if (has_hidden_ret) {
            int rsz = hidden_ret_size;
            hidden_alloc_size = (rsz + 15) & -16;
            if (hidden_alloc_size <= 127) {
                e1(0x48); e1(0x83); e1(0xEC); e1(hidden_alloc_size);
            } else {
                e1(0x48); e1(0x81); e1(0xEC); e4(hidden_alloc_size);
            }
            e1(0x48); e1(0x8D); e1(0x04); e1(0x24);  /* lea rax, [rsp] */
            push_rax();  /* hidden ptr, will be popped last into RDI */
        }

        /* Phase 2: push 寄存器参数（0-5）后入栈 → 在栈顶，pop 循环直接弹出 */
        arg = node->args;
        for (idx = 0; arg && idx < argc && idx < 6; idx++) {
            cgen_expr(arg);
            if (arg_is_float[idx])
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

        {
            int total = argc + has_hidden_ret;
            /* 预计算浮点参数总数（XMM 寄存器从 0 开始独立计数，符合 SysV ABI） */
            int total_floats = 0;
            { for (int _ri = 0; _ri < argc; _ri++) if (arg_is_float[_ri]) total_floats++; }
            /* 预计算各参数的 GP 寄存器编号（独立于浮点参数位置，匹配被调方 int_reg 计数）。
             * 非浮点参数顺序递增 GP 编号：第 1 个 GP 参数→RDI(rt=0)，第 2 个→RSI(rt=1)... */
            int gp_reg_for_arg[16];
            { int _gp = has_hidden_ret ? 1 : 0;  /* hidden ptr occupies RDI */
              for (int _ri = 0; _ri < argc && _ri < 16; _ri++) {
                  if (arg_is_float[_ri]) gp_reg_for_arg[_ri] = -1;
                  else { gp_reg_for_arg[_ri] = _gp; _gp++; }
              }
            }
            int float_reg = total_floats;  /* 从后向前分配（栈顶 = 最后参数 = 最高 XMM 编号） */
            for (int ai = total - 1; ai >= 0; ai--) {
                /* With hidden pointer: pushed first (bottom of stack), popped last into RDI */
                if (has_hidden_ret && ai == 0) {
                    pop_rax();
                    e1(0x48); e1(0x89); e1(0xC7);  /* mov rdi, rax */
                    continue;
                }
                /* Real argument index (without hidden) */
                int ri = has_hidden_ret ? ai - 1 : ai;
                if (arg_is_float[ri]) {
                    float_reg--;
                    if (float_reg >= 0) {
                        e1(0xF2); e1(0x0F); e1(0x10);
                        e1(0x04 | ((float_reg & 7) << 3)); e1(0x24);  /* movsd xmm[float_reg], [rsp] */
                        e1(0x48); e1(0x83); e1(0xC4); e1(0x08); /* add rsp, 8 */
                    }  /* else: 9th+ float arg stays on stack */
                } else {
                    int rt = gp_reg_for_arg[ri];
                    if (rt >= 6) continue;  /* 7th+ GP arg stays on stack */
                    pop_rax();
                    int use64 = 1;
                    switch (rt) {
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
                    case 3:
                        /* RCX already in hand from pop_rax? No need for special pop_rcx
                         * since we already popped into RAX uniformly */
                        /* But pop_rcx optimization saves a mov. Use it when target is RCX
                         * without clobbering other state. For simplicity, stay uniform. */
                        e1(0x48); e1(0x89); e1(0xC1);  /* mov rcx, rax */
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
        }

        if (indirect_call) {
            if (argc > 3) {
                /* rcx 已被 arg4 占用 → 用 r10 存函数指针 */
                pop_rax();
                e1(0x49); e1(0x89); e1(0xC2);  /* mov r10, rax */
                e1(0x41); e1(0xFF); e1(0xD2);  /* call *r10 */
            } else {
                pop_rcx();
                e1(0xFF); e1(0xD1); /* call *rcx */
            }
        } else {
            emit_call(node->name);
        }
        /* 清理栈上传参（7th+ 参数被调用者未清理） */
        if (argc > 6) {
            int stack_args = argc - 6;
            e1(0x48); e1(0x83); e1(0xC4); e1(stack_args * 8);  /* add rsp, N */
        }
        /* 大结构体返回值：清理隐藏指针分配的空间 */
        if (hidden_alloc_size > 0) {
            if (hidden_alloc_size <= 127) {
                e1(0x48); e1(0x83); e1(0xC4); e1(hidden_alloc_size);
            } else {
                e1(0x48); e1(0x81); e1(0xC4); e4(hidden_alloc_size);
            }
        }
        /* 符号扩展：函数返回 signed int/char/short 时，将 EAX 符号扩展到 RAX，
         * 保证后续 int→long 赋值、传参等所有路径的正确性。
         * 注：避免在 !has_hidden_ret 之前扩展（大结构体返回的指针不需符号扩展）。 */
        if (!node->is_float && !has_hidden_ret && node->name && !is_fptr) {
            int rsz = get_func_ret_size(node->name);
            if (rsz == 0) {
                int pi;
                for (pi = 0; pi < parsed_func_ret_count; pi++) {
                    if (parsed_func_ret_names[pi] &&
                        strcmp(parsed_func_ret_names[pi], node->name) == 0) {
                        rsz = parsed_func_ret_sizes[pi];
                        break;
                    }
                }
            }
            /* rsz=0 表示未知（隐式/extern 函数），保守假设返回指针大小不做扩展。
             * 已知返回 signed int/char/short 时符号扩展，unsigned 时零扩展（无需操作）。 */
            if (rsz > 0 && rsz < 8) {
                int _unsigned_ret = 0;
                { int _pi;
                  for (_pi = 0; _pi < parsed_func_ret_count; _pi++) {
                      if (parsed_func_ret_names[_pi] &&
                          strcmp(parsed_func_ret_names[_pi], node->name) == 0) {
                          if (parsed_func_ret_unsigned[_pi])
                              _unsigned_ret = 1;
                          break;
                      }
                  }
                }
                if (!_unsigned_ret)
                    { e1(0x48); e1(0x63); e1(0xC0); }  /* movsxd rax, eax */
                /* 无符号返回值已在 RAX 中零扩展（x86-64 32 位操作自动清高 32 位） */
            }
        }
        /* 若调用返回 double，标记节点 */
        if (node->is_float)
            ;  /* 结果已在 xmm0 中 */
        else if (has_hidden_ret)
            ;  /* 大结构体：保留原始 type_size（外层赋值代码据此执行 rep movsb 全量拷贝） */
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
        e1(0x0F); e1(0x84); e1(0x11); e1(0x22); e1(0x33); e1(0x44);

        /* then 分支 */
        cgen_expr(node->then_stmt);
        if (is_f) push_xmm0(); else push_rax();

        /* jmp end_label（5 字节占位） */
        int jmp_pos = code_size;
        e1(0xE9); e1(0x55); e1(0x66); e1(0x77); e1(0x88);

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
                SEARCH_LOCAL(i, node->left->name);
                if (i >= 0) {
                        int total_off = locals[i].offset + member_off;
                        if (node->is_float) {
                            if (node->is_float == 4) {
                                e1(0xF3); e1(0x0F); e1(0x10);  /* movss xmm0, [rbp+off] */
                                e1(0x45); e1(total_off & 0xFF);
                            } else {
                                load_double_from_rbp(total_off);
                            }
                        } else if (node->type_size > 8 || node->is_array) {
                            /* 数组成员（含 <=8 字节）：退化为指针 */
                            lea_from_rbp(total_off);
                            node->type_size = 8;
                        } else if (node->type_size == 8)
                            load_rax_from_rbp(total_off);
                        else if (node->type_size == 1) {
                            /* char 成员加载：按符号性选择 movsbl/movzbl */
                            if (node->is_unsigned) {
                                if (disp8_fits(total_off))
                                    { e1(0x0F); e1(0xB6); e1(0x45); e1(total_off & 0xFF); }
                                else
                                    { e1(0x0F); e1(0xB6); e1(0x85); e4(total_off); }
                            } else {
                                if (disp8_fits(total_off))
                                    { e1(0x0F); e1(0xBE); e1(0x45); e1(total_off & 0xFF); }
                                else
                                    { e1(0x0F); e1(0xBE); e1(0x85); e4(total_off); }
                            }
                        } else if (node->type_size == 2) {
                            /* short 成员加载：按符号性选择 movswl/movzwl */
                            if (node->is_unsigned) {
                                if (disp8_fits(total_off))
                                    { e1(0x0F); e1(0xB7); e1(0x45); e1(total_off & 0xFF); }
                                else
                                    { e1(0x0F); e1(0xB7); e1(0x85); e4(total_off); }
                            } else {
                                if (disp8_fits(total_off))
                                    { e1(0x0F); e1(0xBF); e1(0x45); e1(total_off & 0xFF); }
                                else
                                    { e1(0x0F); e1(0xBF); e1(0x85); e4(total_off); }
                            }
                        } else
                            load_eax_from_rbp(total_off);
                        is_local = 1;

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
                        int ro = code_size; e1(0x55); e1(0x66); e1(0x77); e1(0x88);
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
                    e1(0x48); e1(0x89); e1(0xC1);  /* mov rcx, rax */
                    mov_eax_imm(member_off);
                    if (member_off < 0) { e1(0x48); e1(0x63); e1(0xC0); }  /* movsxd */
                    e1(0x48); e1(0x01); e1(0xC8);  /* add rax, rcx (64-bit — 地址运算) */
                }
                /* 从地址加载值（数组/大结构体成员不加载，退化为指针） */
                if (node->is_float) {
                    if (node->is_float == 4) {
                        e1(0xF3); e1(0x0F); e1(0x10); e1(0x00);  /* movss xmm0, [rax] */
                    } else {
                        e1(0xF2); e1(0x0F); e1(0x10); e1(0x00);  /* movsd xmm0, [rax] */
                    }
                } else if (node->type_size > 8 || node->is_array) {
                    node->type_size = 8;  /* 退化为指针 */
                } else if (node->type_size == 8) {
                    e1(0x48); e1(0x8B); e1(0x00);  /* mov rax, [rax] */
                } else if (node->type_size == 1) {
                    if (node->is_unsigned)
                        { e1(0x0F); e1(0xB6); e1(0x00); }  /* movzbl (%rax), %eax */
                    else
                        { e1(0x0F); e1(0xBE); e1(0x00); }  /* movsbl (%rax), %eax */
                } else if (node->type_size == 2) {
                    if (node->is_unsigned)
                        { e1(0x0F); e1(0xB7); e1(0x00); }  /* movzwl (%rax), %eax */
                    else
                        { e1(0x0F); e1(0xBF); e1(0x00); }  /* movswl (%rax), %eax */
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
                if (member_off < 0) { e1(0x48); e1(0x63); e1(0xC0); }  /* movsxd */
                pop_rcx();
                e1(0x48); e1(0x01); e1(0xC8);  /* add rax, rcx (64-bit) */
            }
            /* 从地址加载值（数组/大结构体成员不加载，退化为指针） */
            if (node->is_float) {
                if (node->is_float == 4) {
                    e1(0xF3); e1(0x0F); e1(0x10); e1(0x00);  /* movss xmm0, [rax] */
                } else {
                    e1(0xF2); e1(0x0F); e1(0x10); e1(0x00);  /* movsd xmm0, [rax] */
                }
            } else if (node->type_size > 8 || node->is_array) {
                node->type_size = 8;  /* 退化为指针 */
            } else if (node->type_size == 8) {
                e1(0x48); e1(0x8B); e1(0x00);  /* mov rax, [rax] */
            } else if (node->type_size == 1) {
                if (node->is_unsigned)
                    { e1(0x0F); e1(0xB6); e1(0x00); }  /* movzbl (%rax), %eax */
                else
                    { e1(0x0F); e1(0xBE); e1(0x00); }  /* movsbl (%rax), %eax */
            } else if (node->type_size == 2) {
                if (node->is_unsigned)
                    { e1(0x0F); e1(0xB7); e1(0x00); }  /* movzwl (%rax), %eax */
                else
                    { e1(0x0F); e1(0xBF); e1(0x00); }  /* movswl (%rax), %eax */
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
