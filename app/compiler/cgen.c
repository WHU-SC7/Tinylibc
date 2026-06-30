/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * cgen.c — x86_64 代码生成（Phase 2）
 *
 * 遍历 AST 节点生成机器码。表达式求值委托给 cgen_expr.c。
 * 管理函数帧、局部变量、控制流跳转标签。
 *
 * 函数帧布局：
 *   调用者 rbp
 *   返回地址
 *   [rbp+0]  保存的 rbp
 *   [rbp-4]  局部变量 1
 *   [rbp-8]  局部变量 2
 *   ...
 *   [rbp-N]  (rsp 在函数执行期间指向这里)
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

/* ─── 局部变量表 ─── */

LocalVar locals[MAX_LOCALS];
int local_count;
int frame_size;

/* 可变参数函数：寄存器保存区在栈中的偏移 */
int reg_save_offset;  /* 从 rbp 向下的偏移（负值） */

/* ─── 标签和回填 ─── */

#define MAX_LABELS 256
#define MAX_FIXUPS 512

static int label_ids[MAX_LABELS];
static int label_offsets[MAX_LABELS];
static int label_count;

static int fixup_label[MAX_FIXUPS];
static int fixup_offset[MAX_FIXUPS];
static int fixup_count;

static void reset_labels(void) {
    label_count = 0;
    fixup_count = 0;
}

static int new_label(void) {
    static int counter = 0;
    return counter++;
}

static void set_label(int id) {
    label_ids[label_count] = id;
    label_offsets[label_count] = code_size;
    label_count++;
}

static int find_label_offset(int id) {
    int i;
    for (i = 0; i < label_count; i++)
        if (label_ids[i] == id) return label_offsets[i];
    return -1;
}

/* ─── 字节发射 ─── */

static void emit1(int b) { code_buf[code_size++] = b & 0xFF; }
static void emit4(int v) {
    emit1(v); emit1(v>>8); emit1(v>>16); emit1(v>>24);
}

/* ─── 跳转指令（rel32，5 或 6 字节） ─── */

static void emit_jmp(int label_id) {
    int off = find_label_offset(label_id);
    if (off >= 0) {
        /* 向后跳转：已知偏移 */
        int disp = off - (code_size + 5);
        emit1(0xE9); emit4(disp);
    } else {
        /* 向前跳转：记录回填 */
        fixup_label[fixup_count] = label_id;
        fixup_offset[fixup_count] = code_size;
        fixup_count++;
        emit1(0xE9); emit4(0);  /* jmp rel32 占位 */
    }
}

static void emit_jcc(int cc, int label_id) {
    /* jcc rel32: 0F 8x xx xx xx xx */
    int off = find_label_offset(label_id);
    if (off >= 0) {
        int disp = off - (code_size + 6);
        emit1(0x0F); emit1(cc); emit4(disp);
    } else {
        fixup_label[fixup_count] = label_id;
        fixup_offset[fixup_count] = code_size;
        fixup_count++;
        emit1(0x0F); emit1(cc); emit4(0);  /* 占位 */
    }
}

static void apply_fixups(void) {
    int i;
    for (i = 0; i < fixup_count; i++) {
        int label_off = find_label_offset(fixup_label[i]);
        if (label_off < 0) continue;

        int jump_off = fixup_offset[i];
        int instr_len;
        if ((code_buf[jump_off] == 0xE9)) {
            instr_len = 5;  /* jmp rel32 */
        } else {
            instr_len = 6;  /* 0F jcc rel32 */
        }
        int disp = label_off - (jump_off + instr_len);
        code_buf[jump_off + instr_len - 4]     = disp & 0xFF;
        code_buf[jump_off + instr_len - 3]     = (disp >> 8) & 0xFF;
        code_buf[jump_off + instr_len - 2]     = (disp >> 16) & 0xFF;
        code_buf[jump_off + instr_len - 1]     = (disp >> 24) & 0xFF;
    }
}

/* ─── 帧管理 ─── */

static void emit_prologue(void) {
    emit1(0x55);              /* push rbp */
    emit1(0x48); emit1(0x89); emit1(0xE5);  /* mov rbp, rsp */

    if (frame_size > 0) {
        /* sub rsp, frame_size (对齐到 16) */
        int aligned = (frame_size + 15) & ~15;
        if (aligned <= 127) {
            emit1(0x48); emit1(0x83); emit1(0xEC); emit1(aligned);
        } else {
            emit1(0x48); emit1(0x81); emit1(0xEC); emit4(aligned);
        }
    }
}

static void emit_epilogue(void) {
    emit1(0x48); emit1(0x89); emit1(0xEC);  /* mov rsp, rbp */
    emit1(0x5D);              /* pop rbp */
    emit1(0xC3);              /* ret */
}

/* ─── 符号辅助 ─── */

static int add_sym(const char *name, int offset, int size,
                   int is_global, int is_func) {
    if (sym_count >= MAX_SYMS) return -1;
    CgenSym *s = &syms[sym_count++];
    s->name = name;
    s->offset = offset;
    s->size = size;
    s->is_global = is_global;
    s->is_func = is_func;
    s->sym_idx = -1;
    return sym_count - 1;
}

/* ─── 前向声明 ─── */

static void cgen_stmt(AstNode *stmt);

/* ─── 为函数收集局部变量 ─── */

static void collect_locals(AstNode *node) {
    if (!node) return;

    switch (node->kind) {
    case AST_FUNC_DEF:
        local_count = 0;
        frame_size = 0;
        collect_locals(node->body);
        break;
    case AST_BLOCK:
        for (AstNode *s = node->stmts; s; s = s->next)
            collect_locals(s);
        break;
    case AST_VAR_DECL:
        if (local_count < MAX_LOCALS && node->name) {
            int sz = node->ival > 0 ? node->ival : 4;
            frame_size += sz;
            locals[local_count].name = node->name;
            locals[local_count].offset = -frame_size;
            locals[local_count].size = sz;
            locals[local_count].struct_tag = NULL;
            local_count++;
        }
        break;
    default:
        break;
    }
}

/* ─── 语句代码生成 ─── */

static void cgen_return(AstNode *stmt) {
    if (stmt->expr) cgen_expr(stmt->expr);
    emit_epilogue();
}

static void cgen_if(AstNode *stmt) {
    /* cond 求值 → eax，cmp eax,0 → je else_label */
    cgen_expr(stmt->cond);
    emit1(0x85); emit1(0xC0);  /* test eax, eax */

    int else_label = new_label();
    int end_label = new_label();

    emit_jcc(0x84, else_label);  /* je else_label (jz) */

    /* then 分支 */
    cgen_stmt(stmt->then_stmt);

    if (stmt->else_stmt) {
        emit_jmp(end_label);
    }

    set_label(else_label);

    if (stmt->else_stmt) {
        cgen_stmt(stmt->else_stmt);
        set_label(end_label);
    }
}

static void cgen_while(AstNode *stmt) {
    int start_label = new_label();
    int end_label = new_label();

    set_label(start_label);

    cgen_expr(stmt->loop_cond);
    emit1(0x85); emit1(0xC0);  /* test eax, eax */
    emit_jcc(0x84, end_label);  /* je end_label */

    cgen_stmt(stmt->loop_body);

    emit_jmp(start_label);

    set_label(end_label);
}

static void cgen_for(AstNode *stmt) {
    int start_label = new_label();
    int end_label = new_label();
    int step_label = new_label();

    /* init */
    if (stmt->loop_init) {
        if (stmt->loop_init->kind == AST_VAR_DECL) {
            /* 变量声明已在 collect_locals 中处理 */
            if (stmt->loop_init->expr) {
                cgen_expr(stmt->loop_init->expr);
                int i;
                for (i = 0; i < local_count; i++)
                    if (strcmp(locals[i].name, stmt->loop_init->name) == 0)
                        { emit1(0x89); emit1(0x45); emit1(locals[i].offset & 0xFF); break; }
            }
        } else {
            cgen_expr(stmt->loop_init->expr);
        }
    }

    set_label(start_label);

    /* condition */
    if (stmt->loop_cond) {
        cgen_expr(stmt->loop_cond);
        emit1(0x85); emit1(0xC0);
        emit_jcc(0x84, end_label);
    }

    /* body */
    cgen_stmt(stmt->loop_body);

    /* step */
    set_label(step_label);
    if (stmt->loop_step)
        cgen_expr(stmt->loop_step);

    emit_jmp(start_label);
    set_label(end_label);
}

static void cgen_block(AstNode *block) {
    for (AstNode *s = block->stmts; s; s = s->next)
        cgen_stmt(s);
}

/* ─── 语句分派 ─── */

static void cgen_stmt(AstNode *stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
    case AST_RETURN:
        cgen_return(stmt);
        break;
    case AST_IF:
        cgen_if(stmt);
        break;
    case AST_WHILE:
        cgen_while(stmt);
        break;
    case AST_FOR:
        cgen_for(stmt);
        break;
    case AST_BLOCK:
        cgen_block(stmt);
        break;
    case AST_EXPR_STMT:
        if (stmt->expr) cgen_expr(stmt->expr);
        break;
    case AST_NULL_STMT:
        break;
    case AST_ASM:
        if (stmt->asm_template)
            cgen_asm(stmt);
        break;
    case AST_VAR_DECL:
        /* 初始化 */
        if (stmt->expr) {
            cgen_expr(stmt->expr);
            int i;
            for (i = 0; i < local_count; i++)
                if (strcmp(locals[i].name, stmt->name) == 0)
                    { emit1(0x89); emit1(0x45); emit1(locals[i].offset & 0xFF); break; }
        }
        break;
    default:
        break;
    }
}

/* ─── 函数代码生成 ─── */

static void cgen_func_def(AstNode *func) {
    int is_variadic = func->is_static;

    reset_labels();
    collect_locals(func);

    /* 可变参数函数：在帧底追加 48 字节寄存器保存区（局部变量之后） */
    if (is_variadic) frame_size += 48;

    int func_start = code_size;
    if (is_variadic) {
        reg_save_offset = -(frame_size);
    } else reg_save_offset = 0;

    emit_prologue();

    /* 保存参数寄存器到局部变量槽 */
    {
        int arg_reg = 0;
        int nparams = func->ival;
        AstNode *p;
        for (p = func->params; p && arg_reg < 6 && arg_reg < nparams; p = p->next) {
            if (p->kind == AST_VAR_DECL && p->name) {
                int i;
                for (i = 0; i < local_count; i++) {
                    if (strcmp(locals[i].name, p->name) == 0) {
                        switch (arg_reg) {
                        case 0: e1(0x89); e1(0x7D); e1(locals[i].offset & 0xFF); break;
                        case 1: e1(0x89); e1(0x75); e1(locals[i].offset & 0xFF); break;
                        case 2: e1(0x89); e1(0x55); e1(locals[i].offset & 0xFF); break;
                        case 3: e1(0x89); e1(0x4D); e1(locals[i].offset & 0xFF); break;
                        case 4: e1(0x44); e1(0x89); e1(0x45); e1(locals[i].offset & 0xFF); break;
                        case 5: e1(0x44); e1(0x89); e1(0x4D); e1(locals[i].offset & 0xFF); break;
                        }
                        break;
                    }
                }
            }
            arg_reg++;
        }
    }

    /* 可变参数函数：保存 6 个 GP 寄存器到寄存器保存区 */
    if (is_variadic) {
        int sb = reg_save_offset;
        e1(0x48); e1(0x89); e1(0x7D); e1(sb & 0xFF);
        e1(0x48); e1(0x89); e1(0x75); e1((sb + 8) & 0xFF);
        e1(0x48); e1(0x89); e1(0x55); e1((sb + 16) & 0xFF);
        e1(0x48); e1(0x89); e1(0x4D); e1((sb + 24) & 0xFF);
        e1(0x4C); e1(0x89); e1(0x45); e1((sb + 32) & 0xFF);
        e1(0x4C); e1(0x89); e1(0x4D); e1((sb + 40) & 0xFF);
    }

    cgen_block(func->body);

    /* 如果函数体没有 return（空函数），加隐式 return */
    if (code_size == func_start + (frame_size > 0 ? (frame_size > 127 ? 11 : 7) : 4)) {
        emit_epilogue();
    }

    int func_end = code_size;

    apply_fixups();

    add_sym(func->name ? func->name : "", func_start,
            func_end - func_start, 1, 1);
}

/* ─── 程序入口 ─── */

void cgen_init(void) {
    code_size = 0;
    sym_count = 0;
    rel_count = 0;
    local_count = 0;
    frame_size = 0;
    reg_save_offset = 0;
    strtab_len = 0;
    strtab[strtab_len++] = '\0';
}

void cgen_program(AstNode *prog) {
    if (!prog || prog->kind != AST_PROGRAM) return;

    for (AstNode *func = prog->body; func; func = func->next) {
        if (func->kind == AST_FUNC_DEF)
            cgen_func_def(func);
    }
}
