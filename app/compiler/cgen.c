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

#include "toyc.h"

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
int scope_chain[MAX_SCOPE_IDS];
int scope_chain_count;
static int next_scope_id;

/* ─── 函数返回类型表（供 struct 按值返回使用） ─── */
const char *func_ret_names[MAX_FUNC_RET_TYPES];
int func_ret_sizes[MAX_FUNC_RET_TYPES];
int func_ret_count;

/* 解析期记录的函数返回类型表（由 parse.c 在原型处填充，cgen_expr.c 查询） */
const char *parsed_func_ret_names[MAX_FUNC_RET_TYPES];
int parsed_func_ret_sizes[MAX_FUNC_RET_TYPES];
int parsed_func_ret_float[MAX_FUNC_RET_TYPES];
int parsed_func_ret_unsigned[MAX_FUNC_RET_TYPES];
int parsed_func_ret_count;
static int current_func_ret_size;
static int current_func_is_unsigned;    /* 当前函数返回类型是否为 unsigned */
static const char *current_func_name;
static int current_hidden_ptr_offset;  /* hidden pointer 栈槽的 RBP 偏移 */

/* ─── 标签和回填 ─── */

#define MAX_LABELS 4096
#define MAX_FIXUPS 8192

static int label_ids[MAX_LABELS];
static int label_offsets[MAX_LABELS];
static int label_count;

static int fixup_label[MAX_FIXUPS];
static int fixup_offset[MAX_FIXUPS];
static int fixup_is_jmp[MAX_FIXUPS];  /* 1=jmp (E9), 0=jcc (0F 8x) */
static int fixup_count;

/* ─── break/continue 目标标签（用于循环和 switch） ─── */
static int break_target_label;    /* break 跳转目标，-1 表示无目标 */
static int continue_target_label; /* continue 跳转目标，-1 表示无目标 */

static int new_label(void);

/* ─── goto 标签名 → label_id 映射 ─── */
#define MAX_GOTO_LABELS 4096
static const char *goto_label_names[MAX_GOTO_LABELS];
static int goto_label_ids[MAX_GOTO_LABELS];
static int goto_label_count;

static int get_or_create_goto_label(const char *name) {
    int i;
    for (i = 0; i < goto_label_count; i++)
        if (strcmp(goto_label_names[i], name) == 0)
            return goto_label_ids[i];
    if (goto_label_count >= MAX_GOTO_LABELS) {
        __write(2, "toyc: too many goto labels\n", 26);
        __exit(1); }
    int id = new_label();
    goto_label_names[goto_label_count] = name;
    goto_label_ids[goto_label_count] = id;
    goto_label_count++;
    return id;
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
        __write(2, "toyc: label overflow\n", 20);
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
            __write(2, "toyc: fixup overflow\n", 20);
            __exit(1);
        }
        fixup_label[fixup_count] = label_id;
        fixup_is_jmp[fixup_count] = 1;
        emit1(0xE9); emit1(0x11); emit1(0x22); emit1(0x33); emit1(0x44);
        fixup_offset[fixup_count] = code_size - 5;
        fixup_count++;
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
            __write(2, "toyc: fixup overflow\n", 20);
            __exit(1);
        }
        fixup_label[fixup_count] = label_id;
        fixup_is_jmp[fixup_count] = 0;
        emit1(0x0F); emit1(cc); emit1(0x55); emit1(0x66); emit1(0x77); emit1(0x88);
        fixup_offset[fixup_count] = code_size - 6;
        fixup_count++;
    }
}

static void apply_fixups(void) {
    int i;
    for (i = 0; i < fixup_count; i++) {
        int label_off = find_label_offset(fixup_label[i]);
        if (label_off < 0) continue;

        int jump_off = fixup_offset[i];
        int is_jmp = fixup_is_jmp[i];
        int disp;
        if (is_jmp) {
            disp = label_off - (jump_off + 5);
            code_buf[jump_off + 1] = disp & 0xFF;
            code_buf[jump_off + 2] = (disp >> 8) & 0xFF;
            code_buf[jump_off + 3] = (disp >> 16) & 0xFF;
            code_buf[jump_off + 4] = (disp >> 24) & 0xFF;
        } else {
            disp = label_off - (jump_off + 6);
            code_buf[jump_off + 2] = disp & 0xFF;
            code_buf[jump_off + 3] = (disp >> 8) & 0xFF;
            code_buf[jump_off + 4] = (disp >> 16) & 0xFF;
            code_buf[jump_off + 5] = (disp >> 24) & 0xFF;
        }
    }
}

/* ─── 帧管理 ─── */

static void emit_prologue(void) {
    emit1(0x55);              /* push rbp */
    emit1(0x48); emit1(0x89); emit1(0xE5);  /* mov rbp, rsp */

    if (frame_size > 0) {
        /* sub rsp, frame_size (对齐到 16) */
        int aligned = (frame_size + 15) & -16;
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
int global_ptr_elem_size[MAX_SYMS];  /* 全局指针变量的元素大小（int*→4, long*→8） */
int global_base_elem_size[MAX_SYMS];
int global_elem_is_ptr_arr[MAX_SYMS];
int global_elem_unsigned[MAX_SYMS];
int global_elem_float[MAX_SYMS];
int global_is_array[MAX_SYMS];        /* 全局变量是否为数组 */

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
    if (sym_count >= MAX_SYMS) {
        __write(2, "toyc: too many symbols\n", 22);
        __exit(1); }
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
        scope_depth = 0; scope_chain_count = 0; next_scope_id = 0;
        current_func_name = node->name;
        collect_locals(node->body);
        break;
    case AST_BLOCK:
        scope_depth++;
        if (scope_chain_count >= MAX_SCOPE_IDS) {
            __write(2, "toyc: scope chain overflow\n", 26);
            __exit(1); }
        scope_chain[scope_chain_count++] = ++next_scope_id;
        for (AstNode *s = node->stmts; s; s = s->next)
            collect_locals(s);
        scope_chain_count--;
        scope_depth--;
        break;
    case AST_VAR_DECL:
        if (node->is_static && node->name) {
            /* 静态局部变量 → BSS（非栈上），编译期间持久化 */
            int vsize = node->ival > 0 ? node->ival : 4;
            elf_bss_size = (elf_bss_size + 7) & -8;
            if (sym_count >= MAX_SYMS) {
                __write(2, "toyc: too many symbols\n", 22);
                __exit(1); }
            {
                CgenSym *s = &syms[sym_count];
                s->name = node->name;
                s->offset = elf_bss_size;
                s->size = vsize;
                s->is_global = 0;  /* STB_LOCAL */
                s->is_func = 0;
                s->shndx = 5;  /* .bss (section index 5: 1-text 2-rela.text 3-data 4-rela.data 5-bss) */
                s->sym_idx = -1;
                global_elem_size[sym_count] = node->is_array && node->elem_size > 0 ? node->elem_size : 0;
                global_ptr_elem_size[sym_count] = (vsize == 8 && node->elem_size > 0) ? node->elem_size : 0;
                global_base_elem_size[sym_count] = node->base_elem_size;
                global_elem_is_ptr_arr[sym_count] = node->elem_is_ptr;
                global_elem_unsigned[sym_count] = node->elem_is_unsigned;
                global_elem_float[sym_count] = node->elem_is_float ? node->elem_is_float :
                    (node->is_array && node->is_float ? node->is_float : 0);
                global_is_array[sym_count] = node->is_array;
                sym_count++;
            }
            elf_bss_size += vsize;
        } else if (node->name) {
            if (local_count >= MAX_LOCALS) {
                __write(2, "toyc: too many local variables\n", 30);
                __exit(1); }
            int sz = node->ival > 0 ? node->ival : 4;
            /* Float 类型分配 8 字节栈槽（与 double 同宽，简化 XMM 内存操作） */
            if (node->is_float && sz < 8) sz = 8;
            frame_size += sz;
            locals[local_count].name = node->name;
            locals[local_count].offset = -frame_size;
            locals[local_count].size = sz;
            locals[local_count].struct_tag = NULL;
            locals[local_count].is_float = node->is_float;
            locals[local_count].is_unsigned = node->is_unsigned;
            locals[local_count].element_size = node->elem_size;
            locals[local_count].base_elem_size = node->base_elem_size;
            locals[local_count].elem_is_ptr = node->elem_is_ptr;
            locals[local_count].scope_depth = scope_depth;
            locals[local_count].scope_id = scope_chain_count > 0 ? scope_chain[scope_chain_count - 1] : 0;
            locals[local_count].elem_is_unsigned = node->elem_is_unsigned;
            locals[local_count].elem_is_float = node->elem_is_float;
            locals[local_count].is_param = 0;
            /* 数组判定：用解析器标记的 is_array（覆盖所有数组，包括
             * ≤8 字节的 int[2]、char[8]、short[4] 等）。回退 heuristic
             * 仅用于解析器未标记的罕见边界情况。 */
            locals[local_count].is_array = node->is_array ? 1 :
                ((node->type_size != 8 || node->ival > 8 || node->elem_is_ptr) && node->ival > 0 &&
                 node->elem_size > 0);
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
    if (stmt->expr) {
        cgen_expr(stmt->expr);
        /* 符号扩展：从 long 函数返回 signed int/char/short 表达式时，将 eax 扩展到 rax */
        if (current_func_ret_size == 8 && stmt->expr && !stmt->expr->is_float &&
            stmt->expr->type_size < 8 && !stmt->expr->is_unsigned)
            { emit1(0x48); emit1(0x63); emit1(0xC0); }  /* movsxd rax, eax */
        /* 窄返回类型截断：char (1) / short (2) 类型返回值需截断或扩展 */
        if (current_func_ret_size < 4 && stmt->expr && !stmt->expr->is_float) {
            if (current_func_ret_size == 1) {
                if (current_func_is_unsigned)
                    { e1(0x0F); e1(0xB6); e1(0xC0); }  /* movzbl %al, %eax — 零扩展 byte */
                else
                    { e1(0x0F); e1(0xBE); e1(0xC0); }  /* movsbl %al, %eax — 符号扩展 byte */
            } else if (current_func_ret_size == 2) {
                if (current_func_is_unsigned)
                    { e1(0x0F); e1(0xB7); e1(0xC0); }  /* movzwl %ax, %eax — 零扩展 word */
                else
                    { e1(0x0F); e1(0xBF); e1(0xC0); }  /* movswl %ax, %eax — 符号扩展 word */
            }
        }
        if (current_func_ret_size > 8) {
            /* 大结构体按值返回（hidden pointer ABI）
             *
             * x86-64 SysV: >16 字节的 struct 通过隐式第 0 个参数（RDI）
             * 传递隐藏指针。被调方通过该指针写入结构体数据，并将指针放入 RAX。
             *
             * RAX = 源地址（cgen_expr 对 >8 字节 struct 变量做 lea 得到）
             * 隐藏指针已在函数入口处保存到栈槽 current_hidden_ptr_offset
             *
             * mov rsi, rax                 ; 源地址
             * mov rdi, [rbp+hoff]          ; 目标地址（隐藏指针）
             * mov ecx, total_size          ; 字节数
             * rep movsb                    ; memcpy
             * mov rax, [rbp+hoff]          ; 返回指针
             */
            e1(0x48); e1(0x89); e1(0xC6);  /* mov rsi, rax */
            if (disp8_fits(current_hidden_ptr_offset))
                { e1(0x48); e1(0x8B); e1(0x7D); e1(current_hidden_ptr_offset & 0xFF); }
            else
                { e1(0x48); e1(0x8B); e1(0xBD); e4(current_hidden_ptr_offset); }
            e1(0xB9); e4(current_func_ret_size);  /* mov ecx, size */
            e1(0xF3); e1(0xA4);                   /* rep movsb */
            if (disp8_fits(current_hidden_ptr_offset))
                { e1(0x48); e1(0x8B); e1(0x45); e1(current_hidden_ptr_offset & 0xFF); }
            else
                { e1(0x48); e1(0x8B); e1(0x85); e4(current_hidden_ptr_offset); }
        }
    }
    emit_epilogue();
}

static void cgen_if(AstNode *stmt) {
    /* cond 求值 → rax，test rax/rax → je else_label */
    cgen_expr(stmt->cond);
    /* 注意：裸指针作为条件（if(ptr)）时 type_size==8，需用 test rax,rax
     * 避免截断高 32 位。比较/逻辑表达式结果通常是 32 位 int。 */
    if (stmt->cond && stmt->cond->type_size >= 8) {
        emit1(0x48); emit1(0x85); emit1(0xC0);  /* test rax, rax */
    } else {
        emit1(0x85); emit1(0xC0);  /* test eax, eax */
    }

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
    if (stmt->loop_cond && stmt->loop_cond->type_size >= 8) {
        emit1(0x48); emit1(0x85); emit1(0xC0);  /* test rax, rax */
    } else {
        emit1(0x85); emit1(0xC0);  /* test eax, eax */
    }
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
                SEARCH_LOCAL(i, stmt->loop_init->name);
                if (i >= 0) {
                        if (locals[i].is_float) {
                            /* int→float: for (float x = int_expr; ...) */
                            if (stmt->loop_init->expr && !stmt->loop_init->expr->is_float) {
                                if (locals[i].is_float == 4)
                                    { e1(0xF3); e1(0x0F); e1(0x2A); e1(0xC0); }
                                else
                                    { e1(0xF2); e1(0x0F); e1(0x2A); e1(0xC0); }
                            }
                            if (disp8_fits(locals[i].offset)) {
                                emit1(0xF2); emit1(0x0F); emit1(0x11);
                                emit1(0x45); emit1(locals[i].offset & 0xFF);
                            } else {
                                emit1(0xF2); emit1(0x0F); emit1(0x11);
                                emit1(0x85); e4(locals[i].offset);
                            }
                        } else if (locals[i].size > 8) {
                            /* 大结构体赋值 */
                            e1(0x48); e1(0x89); e1(0xC6);  /* mov rsi, rax */
                            if (disp8_fits(locals[i].offset))
                                { e1(0x48); e1(0x8D); e1(0x7D); e1(locals[i].offset & 0xFF); }
                            else
                                { e1(0x48); e1(0x8D); e1(0xBD); e4(locals[i].offset); }
                            e1(0xB9); e4(locals[i].size);  /* mov ecx, size */
                            e1(0xF3); e1(0xA4);            /* rep movsb */
                        } else if (locals[i].size == 8) {
                            /* int→long：来源于 4 字节表达式时需符号扩展 */
                            if (stmt->loop_init->expr && !stmt->loop_init->expr->is_float && stmt->loop_init->expr->type_size < 8) {
                                int do_sext = 0;
                                if (stmt->loop_init->expr->kind == AST_VAR && !stmt->loop_init->expr->is_unsigned) {
                                    do_sext = 1;
                                } else if (stmt->loop_init->expr->kind == AST_CONSTANT &&
                                           stmt->loop_init->expr->ival >= -2147483648L &&
                                           stmt->loop_init->expr->ival <= 2147483647L &&
                                           !stmt->loop_init->expr->is_unsigned) {
                                    do_sext = 1;
                                } else if (!stmt->loop_init->expr->is_unsigned) {
                                    do_sext = 1;  /* 其他有符号 int 表达式（BINOP/UNARY 等）→ long */
                                }
                                if (do_sext)
                                    { e1(0x48); e1(0x63); e1(0xC0); }
                            }
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
        } else {
            cgen_expr(stmt->loop_init->expr);
        }
    }

    set_label(start_label);

    /* condition */
    if (stmt->loop_cond) {
        cgen_expr(stmt->loop_cond);
        if (stmt->loop_cond->type_size >= 8) {
            emit1(0x48); emit1(0x85); emit1(0xC0);  /* test rax, rax */
        } else {
            emit1(0x85); emit1(0xC0);  /* test eax, eax */
        }
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
    if (scope_chain_count < MAX_SCOPE_IDS)
        scope_chain[scope_chain_count++] = ++next_scope_id;
    for (AstNode *s = block->stmts; s; s = s->next)
        cgen_stmt(s);
    scope_chain_count--;
    scope_depth--;
}

/* ─── switch/case/default ─── */

#define MAX_CASES 2048
#define CASE_ENTRY_SIZE (sizeof(int)*3)

typedef struct { int value; int value2; int label; } CaseEntry;

static void cgen_switch(AstNode *stmt) {
    CaseEntry cases[MAX_CASES];
    int case_count = 0;
    int default_label = -1;

    /* Phase 1: 遍历体收集 case 信息 */
    for (AstNode *s = stmt->stmts; s; s = s->next) {
        if (s->kind == AST_CASE) {
            if (case_count >= MAX_CASES) {
                __write(2, "toyc: too many case labels\n", 26);
                __exit(1); }
            cases[case_count].value = s->ival;
            cases[case_count].value2 = (s->right && s->right->kind == AST_CONSTANT)
                                       ? s->right->ival : s->ival;
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
        if (cases[i].value2 != cases[i].value) {
            /* case lo ... hi：范围匹配 */
            int skip_lbl = new_label();
            emit1(0x3D);                /* cmp eax, imm32 */
            emit4(cases[i].value);
            emit_jcc(0x8C, skip_lbl);   /* jl skip (eax < lo) */
            emit1(0x3D);                /* cmp eax, imm32 */
            emit4(cases[i].value2);
            emit_jcc(0x8F, skip_lbl);   /* jg skip (eax > hi) */
            emit_jmp(cases[i].label);   /* lo <= eax <= hi → jmp case_body */
            set_label(skip_lbl);        /* skip: 继续下一个 case */
        } else {
            emit1(0x3D);                /* cmp eax, imm32 */
            emit4(cases[i].value);
            emit_jcc(0x84, cases[i].label);  /* je case_label */
        }
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
            if (stmt->loop_cond->type_size >= 8) {
                emit1(0x48); emit1(0x85); emit1(0xC0);  /* test rax, rax */
            } else {
                emit1(0x85); emit1(0xC0);  /* test eax, eax */
            }
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
        if (stmt->asm_.asm_template)
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
                /* 大结构体初始化（>8 字节）：用 cgen_addr 获取 RHS 的源地址，
                 * 避免 cgen_expr 对 *ptr 解引用时的加载语义错误（elem_size>8
                 * 时 cgen_expr 做 32 位加载而非返回地址）。 */
                int _vi;
                SEARCH_LOCAL(_vi, stmt->name);
                if (_vi >= 0 && locals[_vi].size > 8) {
                    cgen_addr(stmt->expr);           /* rax = 源地址 */
                    e1(0x48); e1(0x89); e1(0xC6);  /* mov rsi, rax */
                    if (disp8_fits(locals[_vi].offset))
                        { e1(0x48); e1(0x8D); e1(0x7D); e1(locals[_vi].offset & 0xFF); }
                    else
                        { e1(0x48); e1(0x8D); e1(0xBD); e4(locals[_vi].offset); }
                    e1(0xB9); e4(locals[_vi].size);  /* mov ecx, size */
                    e1(0xF3); e1(0xA4);              /* rep movsb */
                } else {
                    cgen_expr(stmt->expr);
                    int i;
                    SEARCH_LOCAL(i, stmt->name);
                    if (i >= 0) {
                        if (locals[i].is_float) {
                            /* int→float: float var = int_expr */
                            if (stmt->expr && !stmt->expr->is_float) {
                                if (locals[i].is_float == 4)
                                    { e1(0xF3); e1(0x0F); e1(0x2A); e1(0xC0); }  /* cvtsi2ss */
                                else
                                    { e1(0xF2); e1(0x0F); e1(0x2A); e1(0xC0); }  /* cvtsi2sd */
                            }
                            if (disp8_fits(locals[i].offset)) {
                                emit1(0xF2); emit1(0x0F); emit1(0x11);
                                emit1(0x45); emit1(locals[i].offset & 0xFF);
                            } else {
                                emit1(0xF2); emit1(0x0F); emit1(0x11);
                                emit1(0x85); e4(locals[i].offset);
                            }
                        } else if (stmt->expr && stmt->expr->is_float) {
                            /* float→int 转换：int var = float_expr */
                            if (stmt->expr->is_float == 4)
                                { e1(0xF3); e1(0x0F); e1(0x2C); e1(0xC0); }  /* cvttss2si eax, xmm0 */
                            else
                                { e1(0xF2); e1(0x0F); e1(0x2C); e1(0xC0); }  /* cvttsd2si eax, xmm0 */
                            /* 回退到整数存储路径 */
                            if (locals[i].size == 1) {
                                if (disp8_fits(locals[i].offset))
                                    { e1(0x88); e1(0x45); e1(locals[i].offset & 0xFF); }
                                else
                                    { e1(0x88); e1(0x85); e4(locals[i].offset); }
                            } else if (locals[i].size == 2) {
                                e1(0x66);
                                if (disp8_fits(locals[i].offset))
                                    { e1(0x89); e1(0x45); e1(locals[i].offset & 0xFF); }
                                else
                                    { e1(0x89); e1(0x85); e4(locals[i].offset); }
                            } else {
                                store_eax_to_rbp(locals[i].offset);
                            }
                        } else if (locals[i].size > 8) {
                            /* 大结构体赋值：cgen_expr 返回指针（RAX），
                             * 从 [RAX] 拷贝 locals[i].size 字节到局部变量 */
                            e1(0x48); e1(0x89); e1(0xC6);  /* mov rsi, rax */
                            if (disp8_fits(locals[i].offset))
                                { e1(0x48); e1(0x8D); e1(0x7D); e1(locals[i].offset & 0xFF); }
                            else
                                { e1(0x48); e1(0x8D); e1(0xBD); e4(locals[i].offset); }
                            e1(0xB9); e4(locals[i].size);  /* mov ecx, size */
                            e1(0xF3); e1(0xA4);            /* rep movsb */
                        } else if (locals[i].size == 8) {
                            /* int → long：有符号整型赋值给 64 位变量时符号扩展 */
                            if (stmt->expr && !stmt->expr->is_float && stmt->expr->type_size < 8) {
                                int do_sext = 0;
                                if (stmt->expr->kind == AST_VAR && !stmt->expr->is_unsigned) {
                                    do_sext = 1;  /* 有符号 int 变量 → long */
                                } else if (stmt->expr->kind == AST_CONSTANT &&
                                           stmt->expr->ival >= -2147483648L &&
                                           stmt->expr->ival <= 2147483647L &&
                                           !stmt->expr->is_unsigned) {
                                    do_sext = 1;  /* 有符号 32 位常量 → long */
                                } else if (!stmt->expr->is_unsigned) {
                                    /* 排除 __builtin_va_arg：va_arg 始终返回 64 位完整值 */
                                    if (!(stmt->expr->kind == AST_CALL && stmt->expr->name &&
                                          strcmp(stmt->expr->name, "__builtin_va_arg") == 0))
                                        do_sext = 1;  /* 其他有符号 int 表达式（UNARY/CALL 等）→ long */
                                }
                                if (do_sext)
                                    { e1(0x48); e1(0x63); e1(0xC0); }  /* movsxd rax, eax */
                            }
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
    scope_depth = 0; scope_chain_count = 0; next_scope_id = 0;
    collect_locals(func);
    scope_depth = 0; scope_chain_count = 0; next_scope_id = 0;

    /* 记录当前函数的返回类型大小 */
    current_func_ret_size = func->type_size;
    current_func_is_unsigned = func->is_unsigned;
    current_hidden_ptr_offset = 0;

    /* 大结构体返回值（>8 字节）：为隐藏指针分配栈槽 */
    if (func->type_size > 8) {
        frame_size += 8;
        current_hidden_ptr_offset = -frame_size;
    }

    /* 可变参数函数：在帧底追加 GP (48) + XMM (64) 寄存器保存区（局部变量之后） */
    if (is_variadic) frame_size += 48 + 64;

    int func_start = code_size;
    if (is_variadic) {
        reg_save_offset = -(frame_size);
    } else reg_save_offset = 0;

    func_nparams = func->ival;

    emit_prologue();

    /* 保存隐藏指针参数（RDI）到大结构体返回值函数 */
    if (current_hidden_ptr_offset) {
        /* mov [rbp+current_hidden_ptr_offset], rdi */
        if (disp8_fits(current_hidden_ptr_offset))
            { e1(0x48); e1(0x89); e1(0x7D); e1(current_hidden_ptr_offset & 0xFF); }
        else
            { e1(0x48); e1(0x89); e1(0xBD); e4(current_hidden_ptr_offset); }
    }

    /* 如果是 main 函数，发射全局变量初始化代码 */
    if (func->name && strcmp(func->name, "main") == 0 && global_init_prog) {
        AstNode *gn;
        for (gn = global_init_prog->body; gn; gn = gn->next) {
            /* init_count > 0 表示花括号初始化器，数据已在 Phase 1
             * 通过 cgen_emit_data_init 发射到 .data 段。gn->expr 对这类
             * 变量不存在或为 dummy (ival=0)，运行时写入会覆盖数据段。 */
            if (gn->kind == AST_VAR_DECL && gn->expr && gn->init_count <= 0) {
                cgen_expr(gn->expr);
                int si = -1;
                int i;
                for (i = 0; i < sym_count; i++) {
                    if (syms[i].name && gn->name &&
                        strcmp(syms[i].name, gn->name) == 0) { si = i; break; }
                }
                if (si >= 0) {
                    int vsize = gn->ival > 0 ? gn->ival : 4;
                    if (gn->is_float) {
                        /* float/double 全局变量初始化：存储 xmm0 */
                        if (gn->is_float == 4) {
                            e1(0xF3); e1(0x0F); e1(0x11); e1(0x05);  /* movss [rip+disp32], xmm0 */
                        } else {
                            e1(0xF2); e1(0x0F); e1(0x11); e1(0x05);  /* movsd [rip+disp32], xmm0 */
                        }
                    } else if (vsize == 8) {
                        e1(0x48); e1(0x89); e1(0x05);
                    } else {
                        e1(0x89); e1(0x05);
                    }
                    int ro = code_size;
                    e1(0x55); e1(0x66); e1(0x77); e1(0x88);
                    if (rel_count >= MAX_RELS) {
                        __write(2, "toyc: too many relocations\n", 26);
                        __exit(1); }
                    Elf64_Rela *r = &rels[rel_count++];
                    r->r_offset = ro;
                    r->r_info = ELF64_R_INFO(si + 1, R_X86_64_PC32);
                    r->r_addend = -4;
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
        int hshift = current_hidden_ptr_offset ? 1 : 0;  /* hidden ptr occupies RDI */
        for (p = func->params, pi = 0; p && pi < func_nparams; p = p->next, pi++) {
            if (p->kind == AST_VAR_DECL && p->name) {
                int i;
                for (i = 0; i < local_count; i++) {
                    if (strcmp(locals[i].name, p->name) == 0) {
                        if (locals[i].is_float) {
                            /* 保存 xmm 寄存器: movsd [rbp+off], xmmN */
                            if (float_reg < 8) {
                                if (disp8_fits(locals[i].offset)) {
                                    e1(0xF2); e1(0x0F); e1(0x11);
                                    e1(0x45 | ((float_reg & 7) << 3));
                                    e1(locals[i].offset & 0xFF);
                                } else {
                                    e1(0xF2); e1(0x0F); e1(0x11);
                                    e1(0x85 | ((float_reg & 7) << 3));
                                    e4(locals[i].offset);
                                }
                            }
                            float_reg++;
                        } else {
                            int param_size = locals[i].size;
                            /* 大结构体参数（>8 字节）：寄存器保存的是指向调用方结构体的指针 */
                            int is_large_struct = (param_size > 8);
                            if (is_large_struct) {
                                param_size = 8;
                                locals[i].is_param = 1;
                            }
                            int use64 = (param_size == 8);
                            int r = int_reg + hshift;  /* physical register number (RDI=0) */
                            if (r < 6) {
                                /* 使用正确的数据宽度存储参数（防止 char/short 的 32-bit 存储覆写相邻变量） */
                                if (param_size == 1) {
                                    /* 8-bit store: mov [rbp+off], dil/sil/dl/cl/r8b/r9b */
                                    switch (r) {
                                    case 0: e1(0x40); e1(0x88); e1(0x7D); break;
                                    case 1: e1(0x40); e1(0x88); e1(0x75); break;
                                    case 2: e1(0x88); e1(0x55); break;
                                    case 3: e1(0x88); e1(0x4D); break;
                                    case 4: e1(0x41); e1(0x88); e1(0x45); break;
                                    case 5: e1(0x41); e1(0x88); e1(0x4D); break;
                                    }
                                    e1(locals[i].offset & 0xFF);
                                } else {
                                    if (param_size == 2) e1(0x66);  /* 16-bit prefix */
                                    /* REX 前缀：r0-r3 用 REX.W (0x48)，r4-r5 用 REX.WR (0x4C) */
                                    switch (r) {
                                    case 0: if (use64) e1(0x48); e1(0x89); e1(0x7D); break;
                                    case 1: if (use64) e1(0x48); e1(0x89); e1(0x75); break;
                                    case 2: if (use64) e1(0x48); e1(0x89); e1(0x55); break;
                                    case 3: if (use64) e1(0x48); e1(0x89); e1(0x4D); break;
                                    case 4: if (use64) e1(0x4C); else e1(0x44); e1(0x89); e1(0x45); break;
                                    case 5: if (use64) e1(0x4C); else e1(0x44); e1(0x89); e1(0x4D); break;
                                    }
                                    e1(locals[i].offset & 0xFF);
                                }
                            } else {
                                /* 7th+ 参数来自栈：[rbp + 0x10 + (r-6)*8] */
                                int sd = 0x10 + (r - 6) * 8;
                                if (param_size == 1) {
                                    e1(0x0F); e1(0xB6); e1(0x45); e1(sd & 0xFF); /* movzx eax, byte [rbp+sd] */
                                    emit_store_rbp8(locals[i].offset); /* mov [rbp+off], al */
                                } else if (param_size == 2) {
                                    e1(0x0F); e1(0xB7); e1(0x45); e1(sd & 0xFF); /* movzx eax, word [rbp+sd] */
                                    emit_store_rbp16(locals[i].offset); /* mov [rbp+off], ax */
                                } else if (use64) {
                                    load_rax_from_rbp(sd);       /* mov rax, [rbp+sd] */
                                    store_rax_to_rbp(locals[i].offset); /* mov [rbp+off], rax */
                                } else {
                                    e1(0x8B); e1(0x45); e1(sd & 0xFF);       /* mov eax, [rbp+sd] */
                                    store_eax_to_rbp(locals[i].offset); /* mov [rbp+off], eax */
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

    /* 可变参数函数：保存 6 个 GP + 8 个 XMM 寄存器到寄存器保存区 */
    if (is_variadic) {
        int sb = reg_save_offset;
        emit_store_rbp64(7, sb);/* rdi */
        emit_store_rbp64(6, sb + 8);/* rsi */
        emit_store_rbp64(2, sb + 16);/* rdx */
        emit_store_rbp64(1, sb + 24);/* rcx */
        /* r8 */ e1(0x4C); e1(0x89); if (disp8_fits(sb+32)) { e1(0x45); e1((sb+32)&0xFF); } else { e1(0x85); e4(sb+32); }
        /* r9 */ e1(0x4C); e1(0x89); if (disp8_fits(sb+40)) { e1(0x4D); e1((sb+40)&0xFF); } else { e1(0x8D); e4(sb+40); }
        /* XMM0-XMM7 保存区（GP 保存区之后，偏移 48-104） */
        { int xi; for (xi = 0; xi < 8; xi++) {
            int off = sb + 48 + xi * 8;
            e1(0xF2); e1(0x0F); e1(0x11);  /* movsd [rbp+off], xmmN */
            if (disp8_fits(off))
                { e1(0x45 | (xi << 3)); e1(off & 0xFF); }
            else
                { e1(0x85 | (xi << 3)); e4(off); }
        }}
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
    data_size = 0;
    data_rel_count = 0;
    local_count = 0;
    frame_size = 0;
    reg_save_offset = 0;
    func_nparams = 0;
    scope_depth = 0; scope_chain_count = 0; next_scope_id = 0;
    strtab_len = 0;
    strtab[strtab_len++] = '\0';
    strpool_size = 0;
    str_info_count = 0;
    elf_bss_size = 0;
    elf_data_size = 0;
    for (int _i = 0; _i < MAX_SYMS; _i++) {
        global_elem_size[_i] = 0;
        global_base_elem_size[_i] = 0;
        global_elem_is_ptr_arr[_i] = 0;
        global_elem_unsigned[_i] = 0;
        global_elem_float[_i] = 0;
    }
    func_ret_count = 0;
    current_func_ret_size = 0;
}

/* ─── 全局变量初始化器的 .data 段数据发射 ─── */
static void cgen_emit_data_init(AstNode *node) {
    if (!node || !node->init_items || node->init_count <= 0) return;

    int elem_size = node->elem_size;
    int items_per_elem = node->init_items_per_elem;
    if (items_per_elem <= 0) items_per_elem = node->init_count;

    /* 获取 struct_type 用于成员对齐 */
    StructType *st = node->struct_type;
    int mi = 0;        /* 当前成员索引 */
    int memb_sub = 0;  /* 数组成员内的子索引 */
    int memb_align = 1;/* 当前成员的对齐 */
    int has_struct = (st && st->member_count > 0);
    int has_widths = has_struct ? 0 : 0; /* 在成员展开匹配时设为 1 */

    int item_idx = 0;
    int elem_byte_count = 0;
    int i;

    for (i = 0; i < node->init_count; i++) {
        InitItem *it = &node->init_items[i];

        /* 如果正在处理 struct 成员，对齐到成员的自然对齐 */
        if (has_widths && has_struct && mi < st->member_count && memb_sub == 0) {
            Member *m = &st->members[mi];
            int align_sz = m->memb_is_array ? m->elem_size : m->size;
            memb_align = (align_sz >= 8) ? 8 : (align_sz >= 4) ? 4 : (align_sz >= 2) ? 2 : 1;
            /* 填充到对齐边界 */
            while (elem_byte_count & (memb_align - 1)) {
                data_buf[data_size++] = 0;
                elem_byte_count++;
            }
        }

        if (it->type == INIT_TYPE_STR) {
            /* ── 字符串：创建 .LC 符号，发射 8 字节 + R_X86_64_64 ── */
            int slen = 0;
            if (it->str) { while (it->str[slen]) slen++; }
            slen++; /* null terminator */

            /* 追加到字符串池 */
            int pool_off = strpool_size;
            if (strpool_size + slen > STRPOOL_SIZE) {
                __write(2, "toyc: strpool overflow (data init)\n", 34);
                __exit(1);
            }
            int si;
            for (si = 0; si < slen; si++)
                strpool_buf[strpool_size++] = it->str[si];

            /* 分配符号名 .LC%d */
            int name_idx = str_info_count;
            char name_buf[16];
            char *np = name_buf;
            *np++ = '.'; *np++ = 'L'; *np++ = 'C';
            if (name_idx >= 10000) *np++ = '0' + (name_idx / 10000) % 10;
            if (name_idx >= 1000)  *np++ = '0' + (name_idx / 1000) % 10;
            if (name_idx >= 100)   *np++ = '0' + (name_idx / 100) % 10;
            if (name_idx >= 10)    *np++ = '0' + (name_idx / 10) % 10;
            *np++ = '0' + name_idx % 10;
            *np = '\0';

            if (name_idx >= MAX_STRINGS) {
                __write(2, "toyc: max strings exceeded (data init)\n", 38);
                __exit(1);
            }
            int ni;
            for (ni = 0; ni < 16; ni++)
                str_infos[name_idx].name[ni] = name_buf[ni];

            /* 创建 LOCAL 符号 */
            int sym_idx = -1;
            if (sym_count >= MAX_SYMS) {
                __write(2, "toyc: too many symbols\n", 22);
                __exit(1); }
            sym_idx = sym_count++;
            syms[sym_idx].name = str_infos[name_idx].name;
            syms[sym_idx].offset = 0;    /* strpool fixup 会修正 */
            syms[sym_idx].size = slen;
            syms[sym_idx].is_global = 0;
            syms[sym_idx].is_func = 0;
            syms[sym_idx].shndx = 1;    /* strpool 在 .text 内 */
            syms[sym_idx].sym_idx = -1;

            str_infos[name_idx].pool_offset = pool_off;
            str_infos[name_idx].len = slen;
            str_infos[name_idx].sym_index = sym_idx;
            str_info_count++;

            /* 发射 8 字节占位 + R_X86_64_64 重定位 */
            int data_off = data_size;
            int bb;
            for (bb = 0; bb < 8; bb++)
                data_buf[data_size++] = 0;

            if (sym_idx >= 0) {
                if (data_rel_count >= ELF_MAX_RELS) {
                    __write(2, "toyc: too many data relocations\n", 31);
                    __exit(1); }
                Elf64_Rela *r = &data_rels[data_rel_count++];
                r->r_offset = data_off;
                r->r_info = ELF64_R_INFO(sym_idx + 1, R_X86_64_64);
                r->r_addend = 0;
            }

            elem_byte_count += 8;
            if (has_widths && has_struct) {
                /* STR 对应指针成员，总是 1 个 item，移到下一成员 */
                mi++;
                memb_sub = 0;
            }
        } else {
            /* ── 整数值：按 item 宽度或元素类型大小发射 LE ── */
            int iw = 4; /* 默认宽度 */
            if (has_widths && has_struct && mi < st->member_count) {
                Member *m = &st->members[mi];
                if (m->memb_is_array && m->elem_size > 0 && m->size > m->elem_size)
                    iw = m->elem_size;
                else if (m->size >= 8 && m->elem_size > 0)
                    iw = 8;  /* 指针 */
                else
                    iw = m->size;
            } else {
                /* 确定有效元素宽度：
                 * 多维数组（int arr[M][N]）的 elem_size 是行大小（N*4），
                 * 而非标量元素宽度。使用 base_elem_size（最内层元素大小）
                 * 以正确发射 4 字节 int 而非 8 字节。 */
                int eff_es = elem_size;
                if (node->is_array && !node->elem_is_ptr &&
                    node->base_elem_size > 0 && node->base_elem_size < elem_size)
                    eff_es = node->base_elem_size;
                if (eff_es == 1) {
                    iw = 1;
                } else if (eff_es == 2) {
                    iw = 2;
                } else if (!has_struct && eff_es >= 8) {
                    iw = 8;
                }
            }

            long v = it->ival;
            if (iw == 1) {
                data_buf[data_size++] = v & 0xFF;
                elem_byte_count += 1;
            } else if (iw == 2) {
                data_buf[data_size++] = v & 0xFF;
                data_buf[data_size++] = (v >> 8) & 0xFF;
                elem_byte_count += 2;
            } else if (iw == 4) {
                data_buf[data_size++] = v & 0xFF;
                data_buf[data_size++] = (v >> 8) & 0xFF;
                data_buf[data_size++] = (v >> 16) & 0xFF;
                data_buf[data_size++] = (v >> 24) & 0xFF;
                elem_byte_count += 4;
            } else {
                /* ≥8 字节（如 NULL 指针的 0 值）：发射 8 字节 */
                data_buf[data_size++] = v & 0xFF;
                data_buf[data_size++] = (v >> 8) & 0xFF;
                data_buf[data_size++] = (v >> 16) & 0xFF;
                data_buf[data_size++] = (v >> 24) & 0xFF;
                data_buf[data_size++] = (v >> 32) & 0xFF;
                data_buf[data_size++] = (v >> 40) & 0xFF;
                data_buf[data_size++] = (v >> 48) & 0xFF;
                data_buf[data_size++] = (v >> 56) & 0xFF;
                elem_byte_count += 8;
            }

            /* 更新 struct 成员计数（数组展开处理） */
            if (has_widths && has_struct && mi < st->member_count) {
                Member *m = &st->members[mi];
                int item_count = 1;
                if (m->memb_is_array && m->elem_size > 0 && m->size > m->elem_size)
                    item_count = m->size / m->elem_size;
                memb_sub++;
                if (memb_sub >= item_count) {
                    mi++;
                    memb_sub = 0;
                }
            }
        }

        item_idx++;

        /* 每元素结束时填充对齐 */
        if (items_per_elem > 0 && item_idx >= items_per_elem) {
            while (elem_byte_count < elem_size) {
                data_buf[data_size++] = 0;
                elem_byte_count++;
            }
            elem_byte_count = 0;
            item_idx = 0;
            mi = 0;
            memb_sub = 0;
            memb_align = 1;
        }
    }
    /* 最后一个元素的尾部填充 */
    while (elem_byte_count > 0 && elem_byte_count < elem_size) {
        data_buf[data_size++] = 0;
        elem_byte_count++;
    }
}

void cgen_program(AstNode *prog) {
    if (!prog || prog->kind != AST_PROGRAM) return;

    global_init_prog = prog;

    /* Phase 1: 收集全局变量
     *
     * 注意：必须在 cgen_emit_data_init 之后创建符号——init 函数可能添加 .LC 符号
     * （sym_count 因此增大），所以 symbol slot 必须在最后分配。
     */
    int bss_offset = 0;
    int data_offset = 0;
    for (AstNode *node = prog->body; node; node = node->next) {
        if (node->kind == AST_VAR_DECL) {
            int vsize = node->ival > 0 ? node->ival : 4;
            int shndx_val;
            int off_val;
            if (node->expr) {
                /* 有初始化器 → .data 段 */
                data_offset = (data_offset + 7) & -8;
                off_val = data_offset;
                shndx_val = 3;
                /* 填充 data_buf 到符号偏移（应对对齐间隙） */
                while (data_size < data_offset)
                    data_buf[data_size++] = 0;
                /* 发射初始化器真实数据（仅大括号初始器且可能添加 .LC 符号） */
                if (node->init_count > 0 && node->init_items) {
                    cgen_emit_data_init(node);
                }
                data_offset += vsize;
            } else {
                /* 无初始化器 → .bss 段 */
                bss_offset = (bss_offset + 7) & -8;
                off_val = bss_offset;
                shndx_val = 5;
                bss_offset += vsize;
            }
            /* 现在创建符号 (sym_count 已经是最终值) */
            if (sym_count >= MAX_SYMS) {
                __write(2, "toyc: too many symbols\n", 22);
                __exit(1); }
            {
                int si = sym_count++;
                CgenSym *s = &syms[si];
                s->name = node->name;
                s->size = vsize;
                s->is_global = !node->is_static;
                s->is_func = 0;
                s->sym_idx = -1;
                s->offset = off_val;
                s->shndx = shndx_val;
                global_elem_size[si] = node->is_array && node->elem_size > 0 ? node->elem_size : 0;
                global_ptr_elem_size[si] = (vsize == 8 && node->elem_size > 0) ? node->elem_size : 0;
                global_base_elem_size[si] = node->base_elem_size;
                global_elem_is_ptr_arr[si] = node->elem_is_ptr;
                global_elem_unsigned[si] = node->elem_is_unsigned;
                global_elem_float[si] = node->elem_is_float ? node->elem_is_float :
                    (node->is_array && node->is_float ? node->is_float : 0);
                global_is_array[si] = node->is_array;
            }
        }
    }
    elf_bss_size = bss_offset;
    elf_data_size = data_offset;
    if (data_offset > DATA_BUF_SIZE) {
        __write(2, "toyc: data buffer overflow\n", 26);
        __exit(1); }
    /* 确保 data_buf 填充到 data_offset（应对 scalar 初始器暂不发射数据的情况） */
    while (data_size < data_offset)
        data_buf[data_size++] = 0;

    /* Phase 1.5: 收集函数返回类型（供 struct 按值返回的 caller 侧使用） */
    func_ret_count = 0;
    for (AstNode *node = prog->body; node; node = node->next) {
        if (node->kind == AST_FUNC_DEF && node->name) {
            if (func_ret_count >= MAX_FUNC_RET_TYPES) {
                __write(2, "toyc: too many function return types\n", 36);
                __exit(1); }
            func_ret_names[func_ret_count] = node->name;
            func_ret_sizes[func_ret_count] = node->type_size;
            func_ret_count++;
        }
    }

    /* Phase 2: 生成函数代码 + 顶层 asm */
    for (AstNode *node = prog->body; node; node = node->next) {
        if (node->kind == AST_FUNC_DEF)
            cgen_func_def(node);
        else if (node->kind == AST_ASM)
            cgen_asm(node);
    }

    /* 在全部函数代码之后追加字符串池 */
    if (strpool_size > 0) {
        if (code_size + strpool_size > CODE_BUF_SIZE) {
            __write(2, "toyc: code buffer overflow\n", 26);
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
