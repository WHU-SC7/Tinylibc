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

unsigned char code_buf[CODE_BUF_SIZE];
int code_size;

CgenSym syms[MAX_SYMS];
int sym_count;

Elf64_Rela rels[MAX_RELS];
int rel_count;

char strtab[STRTAB_SIZE];
int strtab_len;

/* ─── 字符串字面量池 ─── */

unsigned char strpool_buf[STRPOOL_SIZE];
int strpool_size;

StrInfo str_infos[MAX_STRINGS];
int str_info_count;

/* ─── 局部变量表 ─── */

LocalVar locals[MAX_LOCALS];
int local_count;
int frame_size;

/* 可变参数函数：寄存器保存区在栈中的偏移 */
int reg_save_offset;  /* 从 rbp 向下的偏移（负值） */
int func_nparams;      /* 当前函数的命名参数个数（供 va_start 使用） */

/* ─── 作用域深度（用于变量阴影解析） ─── */
int scope_depth;

/* ─── 标签和回填 ─── */

#define MAX_LABELS 1024
#define MAX_FIXUPS 2048

static int label_ids[MAX_LABELS];
static int label_offsets[MAX_LABELS];
static int label_count;

static int fixup_label[MAX_FIXUPS];
static int fixup_offset[MAX_FIXUPS];
static int fixup_count;

/* ─── break/continue 目标标签（用于循环和 switch） ─── */
static int break_target_label;    /* break 跳转目标，-1 表示无目标 */
static int continue_target_label; /* continue 跳转目标，-1 表示无目标 */

static int new_label(void);

/* ─── goto 标签名 → label_id 映射 ─── */
#define MAX_GOTO_LABELS 256
static const char *goto_label_names[MAX_GOTO_LABELS];
static int goto_label_ids[MAX_GOTO_LABELS];
static int goto_label_count;

static int get_or_create_goto_label(const char *name) {
    int i;
    for (i = 0; i < goto_label_count; i++)
        if (strcmp(goto_label_names[i], name) == 0)
            return goto_label_ids[i];
    if (goto_label_count < MAX_GOTO_LABELS) {
        int id = new_label();
        goto_label_names[goto_label_count] = name;
        goto_label_ids[goto_label_count] = id;
        goto_label_count++;
        return id;
    }
    return -1;
}

static void reset_labels(void) {
    label_count = 0;
    fixup_count = 0;
    goto_label_count = 0;
}

static int new_label(void) {
    static int counter = 0;
    return counter++;
}

static void set_label(int id) {
    if (label_count >= MAX_LABELS) {
        __write(2, "tcc: label overflow\n", 20);
        __exit(1);
    }
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
        if (fixup_count >= MAX_FIXUPS) {
            __write(2, "tcc: fixup overflow\n", 20);
            __exit(1);
        }
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
        if (fixup_count >= MAX_FIXUPS) {
            __write(2, "tcc: fixup overflow\n", 20);
            __exit(1);
        }
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

/* 全局变量的元素大小（供数组下标运算确定偏移量） */
int global_elem_size[MAX_SYMS];

static int add_sym(const char *name, int offset, int size,
                   int is_global, int is_func) {
    /*
     * 先查同名 UNDEF 符号（由 emit_call 前向引用创建），
     * 若存在则原地更新为 DEFINED，这样 emit_call 创建的
     * 重定位条目指向的符号索引不变，链接器即可解析。
     */
    if (name) {
        int i;
        for (i = 0; i < sym_count; i++) {
            if (syms[i].name && syms[i].shndx == 0 &&
                strcmp(syms[i].name, name) == 0) {
                /* 更新 UNDEF → DEFINED */
                syms[i].offset = offset;
                syms[i].size   = size;
                syms[i].is_func = is_func;
                syms[i].shndx  = 1;   /* .text */
                return i;
            }
        }
    }
    if (sym_count >= MAX_SYMS) return -1;
    CgenSym *s = &syms[sym_count++];
    s->name = name;
    s->offset = offset;
    s->size = size;
    s->is_global = is_global;
    s->is_func = is_func;
    s->shndx = 1;  /* 默认 .text */
    s->sym_idx = -1;
    return sym_count - 1;
}

/* ─── 前向声明 ─── */

static void cgen_stmt(AstNode *stmt);
static AstNode *global_init_prog;  /* 用于全局变量初始化 */

/* ─── 为函数收集局部变量 ─── */

static void collect_locals(AstNode *node) {
    if (!node) return;

    switch (node->kind) {
    case AST_FUNC_DEF:
        local_count = 0;
        frame_size = 0;
        scope_depth = 0;
        collect_locals(node->body);
        break;
    case AST_BLOCK:
        scope_depth++;
        for (AstNode *s = node->stmts; s; s = s->next)
            collect_locals(s);
        scope_depth--;
        break;
    case AST_VAR_DECL:
        if (local_count < MAX_LOCALS && node->name) {
            int sz = node->ival > 0 ? node->ival : 4;
            frame_size += sz;
            locals[local_count].name = node->name;
            locals[local_count].offset = -frame_size;
            locals[local_count].size = sz;
            locals[local_count].struct_tag = NULL;
            locals[local_count].is_float = node->is_float;
            locals[local_count].element_size = node->elem_size;
            locals[local_count].base_elem_size = node->base_elem_size;
            locals[local_count].scope_depth = scope_depth;
            local_count++;
        }
        break;
    case AST_IF:
        collect_locals(node->then_stmt);
        collect_locals(node->else_stmt);
        break;
    case AST_WHILE:
        collect_locals(node->loop_body);
        break;
    case AST_FOR:
        collect_locals(node->loop_init);
        collect_locals(node->loop_body);
        break;
    case AST_DO_WHILE:
        collect_locals(node->loop_body);
        break;
    case AST_SWITCH:
        for (AstNode *s = node->stmts; s; s = s->next)
            collect_locals(s);
        break;
    case AST_RETURN:
        break;
    case AST_EXPR_STMT:
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

    int saved_break = break_target_label;
    int saved_continue = continue_target_label;
    break_target_label = end_label;
    continue_target_label = start_label;

    set_label(start_label);

    cgen_expr(stmt->loop_cond);
    emit1(0x85); emit1(0xC0);  /* test eax, eax */
    emit_jcc(0x84, end_label);  /* je end_label */

    cgen_stmt(stmt->loop_body);

    emit_jmp(start_label);

    set_label(end_label);
    break_target_label = saved_break;
    continue_target_label = saved_continue;
}

static void cgen_for(AstNode *stmt) {
    int start_label = new_label();
    int end_label = new_label();
    int step_label = new_label();

    int saved_break = break_target_label;
    int saved_continue = continue_target_label;
    break_target_label = end_label;
    continue_target_label = step_label;

    /* init */
    if (stmt->loop_init) {
        if (stmt->loop_init->kind == AST_VAR_DECL) {
            /* 变量声明已在 collect_locals 中处理 */
            if (stmt->loop_init->expr) {
                cgen_expr(stmt->loop_init->expr);
                int i;
                for (i = local_count - 1; i >= 0; i--) {
                    if (strcmp(locals[i].name, stmt->loop_init->name) == 0 &&
                        locals[i].scope_depth <= scope_depth) {
                        if (locals[i].is_float) {
                            emit1(0xF2); emit1(0x0F); emit1(0x11);
                            emit1(0x45); emit1(locals[i].offset & 0xFF);
                        } else if (locals[i].size == 8) {
                            emit1(0x48); emit1(0x89); emit1(0x45); emit1(locals[i].offset & 0xFF);
                        } else {
                            emit1(0x89); emit1(0x45); emit1(locals[i].offset & 0xFF);
                        }
                        break;
                    }
                }
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
    break_target_label = saved_break;
    continue_target_label = saved_continue;
}

static void cgen_block(AstNode *block) {
    scope_depth++;
    for (AstNode *s = block->stmts; s; s = s->next)
        cgen_stmt(s);
    scope_depth--;
}

/* ─── switch/case/default ─── */

#define MAX_CASES 256

typedef struct { int value; int label; } CaseEntry;

static void cgen_switch(AstNode *stmt) {
    CaseEntry cases[MAX_CASES];
    int case_count = 0;
    int default_label = -1;

    /* Phase 1: 遍历体收集 case 信息 */
    for (AstNode *s = stmt->stmts; s; s = s->next) {
        if (s->kind == AST_CASE) {
            if (case_count >= MAX_CASES) break;
            cases[case_count].value = s->ival;
            cases[case_count].label = new_label();
            s->op = cases[case_count].label;  /* 存入 op 字段供 Phase 4 使用 */
            case_count++;
        } else if (s->kind == AST_DEFAULT) {
            default_label = new_label();
            s->op = default_label;
        }
    }

    /* Phase 2: 生成条件表达式 */
    cgen_expr(stmt->cond);          /* eax = 条件值 */
    emit1(0x50);                    /* push rax (保存到栈) */

    /* Phase 3: 生成跳转到分发表 */
    int dispatch_label = new_label();
    emit_jmp(dispatch_label);

    /* Phase 4: 生成 case 体 */
    int saved_break = break_target_label;
    break_target_label = new_label();

    for (AstNode *s = stmt->stmts; s; s = s->next) {
        if (s->kind == AST_CASE) {
            set_label(s->op);
        } else if (s->kind == AST_DEFAULT) {
            set_label(s->op);
        } else {
            cgen_stmt(s);
        }
    }

    /* Phase 5: 分发表 */
    set_label(dispatch_label);
    emit1(0x58);                    /* pop rax (恢复条件值) */

    int i;
    for (i = 0; i < case_count; i++) {
        emit1(0x3D);                /* cmp eax, imm32 */
        emit4(cases[i].value);
        emit_jcc(0x84, cases[i].label);  /* je case_label */
    }

    /* 未匹配时跳到 default 或结束 */
    if (default_label >= 0)
        emit_jmp(default_label);

    set_label(break_target_label);
    break_target_label = saved_break;
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
    case AST_DO_WHILE: {
        int start_label = new_label();
        int end_label = new_label();
        int saved_break = break_target_label;
        int saved_continue = continue_target_label;
        break_target_label = end_label;
        continue_target_label = start_label;
        set_label(start_label);
        cgen_stmt(stmt->loop_body);
        /* 条件为真（非零）时继续循环 */
        if (stmt->loop_cond) {
            cgen_expr(stmt->loop_cond);
            emit1(0x85); emit1(0xC0);  /* test eax, eax */
            emit_jcc(0x85, start_label);  /* jne start_label */
        }
        set_label(end_label);
        break_target_label = saved_break;
        continue_target_label = saved_continue;
        break;
    }
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
    case AST_SWITCH:
        cgen_switch(stmt);
        break;
    case AST_CASE:
    case AST_DEFAULT:
        /* case/default 仅作为标签，由 cgen_switch 统一处理 */
        break;
    case AST_BREAK:
        if (break_target_label >= 0)
            emit_jmp(break_target_label);
        break;
    case AST_CONTINUE:
        if (continue_target_label >= 0)
            emit_jmp(continue_target_label);
        break;
    case AST_GOTO:
        if (stmt->name) {
            int lid = get_or_create_goto_label(stmt->name);
            if (lid >= 0) emit_jmp(lid);
        }
        break;
    case AST_LABEL:
        if (stmt->name) {
            int lid = get_or_create_goto_label(stmt->name);
            if (lid >= 0) set_label(lid);
        }
        break;
    case AST_VAR_DECL:
        /* 初始化 */
        if (stmt->expr) {
            if (stmt->expr->kind == AST_ASSIGN) {
                /* 复合初始化器 {a,b,c}：链中的每个 AST_ASSIGN 自行完成存储 */
                AstNode *e;
                for (e = stmt->expr; e; e = e->next)
                    cgen_expr(e);
            } else {
                cgen_expr(stmt->expr);
                int i;
                for (i = local_count - 1; i >= 0; i--) {
                    if (strcmp(locals[i].name, stmt->name) == 0 &&
                        locals[i].scope_depth <= scope_depth) {
                        if (locals[i].is_float) {
                            emit1(0xF2); emit1(0x0F); emit1(0x11);
                            emit1(0x45); emit1(locals[i].offset & 0xFF);
                        } else if (locals[i].size == 8) {
                            emit1(0x48); emit1(0x89); emit1(0x45); emit1(locals[i].offset & 0xFF);
                        } else {
                            emit1(0x89); emit1(0x45); emit1(locals[i].offset & 0xFF);
                        }
                        break;
                    }
                }
            }
        }
        break;
    default:
        break;
    }
}

/* ─── 函数代码生成 ─── */

static void cgen_func_def(AstNode *func) {
    int is_variadic = func->is_variadic;

    reset_labels();
    scope_depth = 0;
    collect_locals(func);
    scope_depth = 0;

    /* 可变参数函数：在帧底追加 48 字节寄存器保存区（局部变量之后） */
    if (is_variadic) frame_size += 48;

    int func_start = code_size;
    if (is_variadic) {
        reg_save_offset = -(frame_size);
    } else reg_save_offset = 0;

    func_nparams = func->ival;

    emit_prologue();

    /* 如果是 main 函数，发射全局变量初始化代码 */
    if (func->name && strcmp(func->name, "main") == 0 && global_init_prog) {
        AstNode *gn;
        for (gn = global_init_prog->body; gn; gn = gn->next) {
            if (gn->kind == AST_VAR_DECL && gn->expr) {
                cgen_expr(gn->expr);
                int si = -1;
                int i;
                for (i = 0; i < sym_count; i++) {
                    if (syms[i].name && gn->name &&
                        strcmp(syms[i].name, gn->name) == 0) { si = i; break; }
                }
                if (si >= 0) {
                    int vsize = gn->ival > 0 ? gn->ival : 4;
                    if (vsize == 8) {
                        e1(0x48); e1(0x89); e1(0x05);
                    } else {
                        e1(0x89); e1(0x05);
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
        }
    }

    /* 保存参数寄存器到局部变量槽（int 和 float 分开计数）。
     * 只处理 func_nparams 个参数，因为 func->params 链已包含 body 语句。 */
    {
        int int_reg = 0;
        int float_reg = 0;
        AstNode *p;
        int pi;
        for (p = func->params, pi = 0; p && pi < func_nparams; p = p->next, pi++) {
            if (p->kind == AST_VAR_DECL && p->name) {
                int i;
                for (i = 0; i < local_count; i++) {
                    if (strcmp(locals[i].name, p->name) == 0) {
                        if (locals[i].is_float) {
                            /* 保存 xmm 寄存器: movsd [rbp+off], xmmN */
                            if (float_reg < 8) {
                                e1(0xF2); e1(0x0F); e1(0x11);
                                e1(0x45 | ((float_reg & 7) << 3));
                                e1(locals[i].offset & 0xFF);
                            }
                            float_reg++;
                        } else {
                            int use64 = (locals[i].size == 8);
                            if (int_reg < 6) {
                                switch (int_reg) {
                                case 0:
                                    if (use64) { e1(0x48); e1(0x89); e1(0x7D); e1(locals[i].offset & 0xFF); }
                                    else { e1(0x89); e1(0x7D); e1(locals[i].offset & 0xFF); }
                                    break;
                                case 1:
                                    if (use64) { e1(0x48); e1(0x89); e1(0x75); e1(locals[i].offset & 0xFF); }
                                    else { e1(0x89); e1(0x75); e1(locals[i].offset & 0xFF); }
                                    break;
                                case 2:
                                    if (use64) { e1(0x48); e1(0x89); e1(0x55); e1(locals[i].offset & 0xFF); }
                                    else { e1(0x89); e1(0x55); e1(locals[i].offset & 0xFF); }
                                    break;
                                case 3:
                                    if (use64) { e1(0x48); e1(0x89); e1(0x4D); e1(locals[i].offset & 0xFF); }
                                    else { e1(0x89); e1(0x4D); e1(locals[i].offset & 0xFF); }
                                    break;
                                case 4:
                                    if (use64) { e1(0x4C); e1(0x89); e1(0x45); e1(locals[i].offset & 0xFF); }
                                    else { e1(0x44); e1(0x89); e1(0x45); e1(locals[i].offset & 0xFF); }
                                    break;
                                case 5:
                                    if (use64) { e1(0x4C); e1(0x89); e1(0x4D); e1(locals[i].offset & 0xFF); }
                                    else { e1(0x44); e1(0x89); e1(0x4D); e1(locals[i].offset & 0xFF); }
                                    break;
                                }
                            } else {
                                /* 7th+ 参数来自栈：[rbp + 0x10 + (int_reg-6)*8] */
                                int sd = 0x10 + (int_reg - 6) * 8;
                                if (use64) {
                                    e1(0x48); e1(0x8B); e1(0x45); e1(sd & 0xFF);       /* mov rax, [rbp+sd] */
                                    e1(0x48); e1(0x89); e1(0x45); e1(locals[i].offset & 0xFF); /* mov [rbp+off], rax */
                                } else {
                                    e1(0x8B); e1(0x45); e1(sd & 0xFF);       /* mov eax, [rbp+sd] */
                                    e1(0x89); e1(0x45); e1(locals[i].offset & 0xFF); /* mov [rbp+off], eax */
                                }
                            }
                            int_reg++;
                        }
                        break;
                    }
                }
            }
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

    /* 如果函数体末尾没有 ret，加隐式 return */
    if (code_size <= 0 || code_buf[code_size - 1] != 0xC3) {
        emit_epilogue();
    }

    int func_end = code_size;

    apply_fixups();

    add_sym(func->name ? func->name : "", func_start,
            func_end - func_start, !func->is_static, 1);
}

/* ─── 程序入口 ─── */

void cgen_init(void) {
    code_size = 0;
    sym_count = 0;
    rel_count = 0;
    local_count = 0;
    frame_size = 0;
    reg_save_offset = 0;
    func_nparams = 0;
    scope_depth = 0;
    strtab_len = 0;
    strtab[strtab_len++] = '\0';
    strpool_size = 0;
    str_info_count = 0;
    elf_bss_size = 0;
    for (int _i = 0; _i < MAX_SYMS; _i++)
        global_elem_size[_i] = 0;
}

void cgen_program(AstNode *prog) {
    if (!prog || prog->kind != AST_PROGRAM) return;

    global_init_prog = prog;

    /* Phase 1: 收集全局变量到 .bss */
    int bss_offset = 0;
    for (AstNode *node = prog->body; node; node = node->next) {
        if (node->kind == AST_VAR_DECL) {
            int vsize = node->ival > 0 ? node->ival : 4;
            /* 对齐到 8 字节 */
            bss_offset = (bss_offset + 7) & ~7;
            if (sym_count < MAX_SYMS) {
                int si = sym_count;
                CgenSym *s = &syms[si];
                s->name = node->name;
                s->offset = bss_offset;
                s->size = vsize;
                s->is_global = !node->is_static;
                s->is_func = 0;
                s->shndx = 3;  /* .bss section */
                s->sym_idx = -1;
                global_elem_size[si] = (vsize > 8) ? node->elem_size : 0;
                sym_count++;
            }
            bss_offset += vsize;
        }
    }
    elf_bss_size = bss_offset;

    /* Phase 2: 生成函数代码 */
    for (AstNode *node = prog->body; node; node = node->next) {
        if (node->kind == AST_FUNC_DEF)
            cgen_func_def(node);
    }

    /* 在全部函数代码之后追加字符串池 */
    if (strpool_size > 0) {
        if (code_size + strpool_size > CODE_BUF_SIZE) {
            __write(2, "tcc: code buffer overflow\n", 26);
            __exit(1);
        }
        int string_start = code_size;

        int i;
        for (i = 0; i < strpool_size; i++)
            code_buf[code_size++] = strpool_buf[i];

        /* 修正每个字符串符号的 offset（符号已在 cgen_expr 中创建） */
        for (i = 0; i < str_info_count; i++) {
            int si = str_infos[i].sym_index;
            if (si >= 0 && si < sym_count) {
                syms[si].offset = string_start + str_infos[i].pool_offset;
            }
        }
    }
}
