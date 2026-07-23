/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * toyas.c — ToyC x86_64 汇编器
 *
 * 机制：将 AT&T 语法 x86_64 汇编源文件直接编码为 ELF64 .o 文件。
 *       按行解析，支持项目 .S 文件所需的 13 种指令和 5 种伪指令。
 *
 * 用法：
 *   toyas input.S [-o output.o]
 *
 * 索引：
 *   main               入口：参数解析、读文件、调用汇编函数
 *   tas_assemble       主循环：逐行解析、编码、符号管理
 *   encode_instr       发射一条指令的机器码
 *   add_sym            向符号表添加条目
 *   find_sym           按名称查找符号
 */

#include "elf_write.h"

/* ─── 全局缓冲区（elf_write.c 需要，由调用者定义）─── */

unsigned char elf_code_buf[ELF_CODE_BUF_SIZE];
int elf_code_size;
ElfWriteSym elf_syms[ELF_MAX_SYMS];
int elf_sym_count;
Elf64_Rela elf_rels[ELF_MAX_RELS];
int elf_rel_count;

/* ─── 寄存器 ─── */

/* 寄存器编码：低 4 位 = 寄存器号 (0-15)，bit4+ = 标志 */
#define REG_SIZE64  0x10   /* 64-bit 寄存器 */
#define REG_SIZE32  0x20   /* 32-bit 寄存器 */
#define REG_SIZE8   0x30   /* 8-bit 寄存器 */
#define REG_SIZE_MASK 0x30
#define REG_IDX_MASK  0x0F

/* reg_table 已内联到 find_reg 中，避免 toyc 的 struct 数组 sizeof bug */

static int find_reg(const char *name) {
    if (!name || !name[0]) return -1;
    /* 64-bit */
    if (name[0]=='r') {
        if (name[1]=='a'&&name[2]=='x'&&name[3]==0) return 0|REG_SIZE64;
        if (name[1]=='b'&&name[2]=='x'&&name[3]==0) return 3|REG_SIZE64;
        if (name[1]=='c'&&name[2]=='x'&&name[3]==0) return 1|REG_SIZE64;
        if (name[1]=='d'&&name[2]=='x'&&name[3]==0) return 2|REG_SIZE64;
        if (name[1]=='s'&&name[2]=='p'&&name[3]==0) return 4|REG_SIZE64;
        if (name[1]=='b'&&name[2]=='p'&&name[3]==0) return 5|REG_SIZE64;
        if (name[1]=='s'&&name[2]=='i'&&name[3]==0) return 6|REG_SIZE64;
        if (name[1]=='d'&&name[2]=='i'&&name[3]==0) return 7|REG_SIZE64;
        if (name[1]=='8'&&name[2]==0)  return 8|REG_SIZE64;
        if (name[1]=='9'&&name[2]==0)  return 9|REG_SIZE64;
        if (name[1]=='1'&&name[2]=='0'&&name[3]==0) return 10|REG_SIZE64;
        if (name[1]=='1'&&name[2]=='1'&&name[3]==0) return 11|REG_SIZE64;
        if (name[1]=='1'&&name[2]=='2'&&name[3]==0) return 12|REG_SIZE64;
        if (name[1]=='1'&&name[2]=='3'&&name[3]==0) return 13|REG_SIZE64;
        if (name[1]=='1'&&name[2]=='4'&&name[3]==0) return 14|REG_SIZE64;
        if (name[1]=='1'&&name[2]=='5'&&name[3]==0) return 15|REG_SIZE64;
    }
    /* 32-bit: e* */
    if (name[0]=='e') {
        if (name[1]=='a'&&name[2]=='x'&&name[3]==0) return 0|REG_SIZE32;
        if (name[1]=='b'&&name[2]=='x'&&name[3]==0) return 3|REG_SIZE32;
        if (name[1]=='c'&&name[2]=='x'&&name[3]==0) return 1|REG_SIZE32;
        if (name[1]=='d'&&name[2]=='x'&&name[3]==0) return 2|REG_SIZE32;
        if (name[1]=='s'&&name[2]=='p'&&name[3]==0) return 4|REG_SIZE32;
        if (name[1]=='b'&&name[2]=='p'&&name[3]==0) return 5|REG_SIZE32;
        if (name[1]=='s'&&name[2]=='i'&&name[3]==0) return 6|REG_SIZE32;
        if (name[1]=='d'&&name[2]=='i'&&name[3]==0) return 7|REG_SIZE32;
    }
    /* 32-bit: rNNd */
    if (name[0]=='r'&&name[3]=='d') {
        if (name[1]=='8'&&name[2]==0) return 8|REG_SIZE32;
        if (name[1]=='9'&&name[2]==0) return 9|REG_SIZE32;
        if (name[1]=='1'&&name[2]=='0'&&name[4]==0) return 10|REG_SIZE32;
        if (name[1]=='1'&&name[2]=='1'&&name[4]==0) return 11|REG_SIZE32;
        if (name[1]=='1'&&name[2]=='2'&&name[4]==0) return 12|REG_SIZE32;
        if (name[1]=='1'&&name[2]=='3'&&name[4]==0) return 13|REG_SIZE32;
        if (name[1]=='1'&&name[2]=='4'&&name[4]==0) return 14|REG_SIZE32;
        if (name[1]=='1'&&name[2]=='5'&&name[4]==0) return 15|REG_SIZE32;
    }
    /* 8-bit */
    if (name[0]=='a'&&name[1]=='l'&&name[2]==0) return 0|REG_SIZE8;
    if (name[0]=='c'&&name[1]=='l'&&name[2]==0) return 1|REG_SIZE8;
    if (name[0]=='d'&&name[1]=='l'&&name[2]==0) return 2|REG_SIZE8;
    if (name[0]=='b'&&name[1]=='l'&&name[2]==0) return 3|REG_SIZE8;
    if (name[0]=='s'&&name[1]=='p'&&name[2]=='l'&&name[3]==0) return 4|REG_SIZE8;
    if (name[0]=='b'&&name[1]=='p'&&name[2]=='l'&&name[3]==0) return 5|REG_SIZE8;
    if (name[0]=='s'&&name[1]=='i'&&name[2]=='l'&&name[3]==0) return 6|REG_SIZE8;
    if (name[0]=='d'&&name[1]=='i'&&name[2]=='l'&&name[3]==0) return 7|REG_SIZE8;
    if (name[0]=='r'&&name[3]=='b') {
        if (name[1]=='1'&&name[2]=='0'&&name[4]==0) return 10|REG_SIZE8;
        if (name[1]=='1'&&name[2]=='1'&&name[4]==0) return 11|REG_SIZE8;
        if (name[1]=='1'&&name[2]=='2'&&name[4]==0) return 12|REG_SIZE8;
        if (name[1]=='1'&&name[2]=='3'&&name[4]==0) return 13|REG_SIZE8;
        if (name[1]=='1'&&name[2]=='4'&&name[4]==0) return 14|REG_SIZE8;
        if (name[1]=='1'&&name[2]=='5'&&name[4]==0) return 15|REG_SIZE8;
    }
    return -1;
}

/* reg 编号是否 ≥ 8（需要 REX.R/REX.B/REX.X） */
static int reg_hi(int r) { return ((r) & REG_IDX_MASK) >= 8; }

/* ─── 指令编码表 ─── */

typedef enum {
    F_NONE,          /* 无操作数 */
    F_RR,            /* reg, reg */
    F_IR,            /* $imm, %reg (64-bit) */
    F_IR8,           /* $imm8, %reg (8-bit: mov $56,%al → B0+reg) */
    F_MR,            /* mem, reg  — 从内存加载 */
    F_RM,            /* reg, mem  — 存储到内存 */
    F_LEA_MR,        /* lea disp(%base), %dst */
    F_LEA_MI,        /* lea disp(%base,%idx,scale), %dst (SIB) */
    F_CALL_DIR,      /* call label — E8 rel32 */
    F_CALL_IND,      /* call *%reg — FF /2 */
    F_JCC8,          /* jcc label — 条件跳转 rel8 */
    F_POP,           /* pop %reg */
    F_ONEBYTE,       /* 单字节指令 (hlt=F4, ret=C3) */
    F_PUSH,          /* push %reg — 50+reg */
} AsmFmt;

typedef struct {
    const char *name;
    AsmFmt fmt;
    unsigned char opcode[2];
    int oplen;
    int op_ext;     /* ModRM reg/opcode 扩展 */
} InstrDef;

/* 指令定义 — 用 int 常量编码所有字段，避免 toyc 对 struct 数据布局的 bug */
/* 每个指令用 5 个 int: { opcode0, opcode1, oplen, op_ext, fmt } */
#define INSTR_ENCODE(o0,o1,l,ex,f) (o0),(o1),(l),(ex),(f)
/* 指令编码表（int 平面数组，不含 name 指针字段） */
static const int instr_enc[] = {
    INSTR_ENCODE(0x89,0,1,0,F_RR),      /* mov F_RR */
    INSTR_ENCODE(0xC7,0,1,0,F_IR),      /* mov F_IR */
    INSTR_ENCODE(0xB0,0,1,0,F_IR8),     /* mov F_IR8 */
    INSTR_ENCODE(0x8B,0,1,0,F_MR),      /* mov F_MR */
    INSTR_ENCODE(0x89,0,1,0,F_RM),      /* mov F_RM */
    INSTR_ENCODE(0x8D,0,1,0,F_LEA_MR),  /* lea F_LEA_MR */
    INSTR_ENCODE(0x8D,0,1,0,F_LEA_MI),  /* lea F_LEA_MI */
    INSTR_ENCODE(0xE8,0,1,0,F_CALL_DIR),/* call F_CALL_DIR */
    INSTR_ENCODE(0xFF,0,1,2,F_CALL_IND),/* call F_CALL_IND */
    INSTR_ENCODE(0x0F,0x05,2,0,F_NONE), /* syscall */
    INSTR_ENCODE(0x31,0,1,0,F_RR),      /* xor F_RR */
    INSTR_ENCODE(0x83,0,1,4,F_IR),      /* and F_IR (op_ext=4) */
    INSTR_ENCODE(0x83,0,1,5,F_IR),      /* sub F_IR (op_ext=5) */
    INSTR_ENCODE(0x85,0,1,0,F_RR),      /* test F_RR */
    INSTR_ENCODE(0x75,0,1,0,F_JCC8),    /* jnz/jne F_JCC8 */
    INSTR_ENCODE(0x74,0,1,0,F_JCC8),    /* je F_JCC8 */
    INSTR_ENCODE(0xEB,0,1,0,F_JCC8),    /* jmp F_JCC8 */
    INSTR_ENCODE(0x58,0,1,0,F_POP),     /* pop F_POP */
    INSTR_ENCODE(0x50,0,1,0,F_PUSH),    /* push F_PUSH */
    INSTR_ENCODE(0xF4,0,1,0,F_NONE),    /* hlt */
    INSTR_ENCODE(0xC3,0,1,0,F_NONE),    /* ret */
    INSTR_ENCODE(0x90,0,1,0,F_NONE),    /* nop */
};

/* ─── 局部数字标号 ─── */

#define MAX_LOCAL_LABELS 2048
static int local_offsets[MAX_LOCAL_LABELS];           /* -1 = 未定义 */
static int local_fixups[MAX_LOCAL_LABELS * 32];       /* 待回填的偏移列表（1D，避免 toyc 2D 数组 stride bug） */
#define LOCAL_FIXUP(i,j) local_fixups[(i) * 32 + (j)]
static int local_nfixups[MAX_LOCAL_LABELS];

static void reset_locals(void) {
    int i;
    for (i = 0; i < MAX_LOCAL_LABELS; i++) {
        local_offsets[i] = -1;
        local_nfixups[i] = 0;
    }
}

/* ─── 符号管理 ─── */

#define TOYAS_MAX_SYMS 2048
static char sym_names[TOYAS_MAX_SYMS * 128];  /* 持久化符号名（1D，避免 toyc 2D 数组 stride bug） */
#define SYM_NAME(i) (sym_names + (i) * 128)

static int find_sym(const char *name) {
    int i;
    for (i = 0; i < elf_sym_count; i++)
        if (elf_syms[i].name && strcmp(elf_syms[i].name, name) == 0)
            return i;
    return -1;
}

static int add_sym(const char *name, int offset, int size,
                   int is_global, int is_func, int shndx) {
    if (elf_sym_count >= TOYAS_MAX_SYMS) {
        __write(2, "toyas: too many symbols\n", 22);
        __exit(1); }
    ElfWriteSym *s = &elf_syms[elf_sym_count];
    /* 持久化复制符号名 */
    int ni = 0;
    while (name[ni] && ni < 127) { SYM_NAME(elf_sym_count)[ni] = name[ni]; ni++; }
    SYM_NAME(elf_sym_count)[ni] = '\0';
    s->name = SYM_NAME(elf_sym_count);
    s->offset = offset;
    s->size = size;
    s->is_global = is_global;
    s->is_func = is_func;
    s->shndx = shndx;
    s->sym_idx = -1;
    elf_sym_count++;
    return elf_sym_count - 1;
}

static void add_rel(int offset, int sym_idx, int type, Elf64_Sxword addend) {
    if (elf_rel_count >= ELF_MAX_RELS) {
        __write(2, "toyas: too many relocations\n", 26);
        __exit(1); }
    Elf64_Rela *r = &elf_rels[elf_rel_count++];
    r->r_offset = offset;
    r->r_info = ELF64_R_INFO(sym_idx + 1, type);
    r->r_addend = addend;
}

/* ─── 字节发射 ─── */

static void e1(int b) {
    if (elf_code_size >= ELF_CODE_BUF_SIZE) {
        __write(2, "toyas: code buffer overflow\n", 26);
        __exit(1);
    }
    elf_code_buf[elf_code_size++] = b & 0xFF;
}
static void e4(int v) { e1(v); e1(v>>8); e1(v>>16); e1(v>>24); }

/* ─── ModRM / SIB / REX ─── */

static int make_modrm(int mod, int reg, int rm) {
    return ((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7);
}
static int make_sib(int scale_bits, int index, int base) {
    return ((scale_bits & 3) << 6) | ((index & 7) << 3) | (base & 7);
}
static void emit_rex(int w, int r, int x, int b) {
    int rex = 0x40 | (w ? 8 : 0) | (r ? 4 : 0) | (x ? 2 : 0) | (b ? 1 : 0);
    if (rex != 0x40) e1(rex);
}
/* 根据 reg1, reg2 发射 REX 前缀 (w=1 for 64-bit) */
static void emit_rex_rr(int w, int r1, int r2) {
    emit_rex(w, reg_hi(r1), 0, reg_hi(r2));
}
static void emit_rex_rm(int w, int r, int base) {
    emit_rex(w, reg_hi(r), 0, reg_hi(base));
}

/* ─── 操作数解析 ─── */

/* 解析一行 AT&T 语法操作数。
 * 返回消费的 token 数，失败返回 -1。
 * 支持格式:
 *   %reg              → OP_REG
 *   $imm              → OP_IMM
 *   (%reg)            → OP_MEM (reg indirect)
 *   disp(%reg)        → OP_MEM_DISP
 *   disp(%base,%idx,scale) → OP_MEM_SIB
 *   *%reg             → OP_INDIRECT (用于 call *)
 */

typedef enum {
    O_NONE, O_REG, O_IMM, O_MEM, O_MEM_DISP, O_MEM_SIB, O_INDIRECT
} OpKind;

typedef struct {
    OpKind kind;
    int reg;          /* 寄存器编号+大小 */
    int reg2;         /* SIB index 寄存器 */
    int scale;        /* SIB scale */
    long imm;    /* 立即数/偏移 */
    const char *label;/* 标号名 (call label) */
} Operand;

/* 读取一个 token。返回 token 起始，*out_len = token 长度。
 * 跳过前导空格。token 结束于空格/逗号/换行/括号/null。 */
static const char *next_tok(const char *p, int *out_len) {
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\n' || *p == '#') { *out_len = 0; return p; }
    /* 单字符 token */
    if (*p == ',' || *p == '(' || *p == ')' || *p == '*' || *p == ':') {
        *out_len = 1; return p;
    }
    /* 多字符 token */
    const char *start = p;
    if (*p == '$' || *p == '%' || *p == '.') p++;
    if (*p == '-') p++;  /* 负号 */
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '#'
           && *p != ',' && *p != '(' && *p != ')' && *p != ':' && *p != '*')
        p++;
    *out_len = p - start;
    return start;
}

/* 比较 token 和字符串（不区分大小写用于助记符） */
static int tok_eq(const char *t, int len, const char *s) {
    int i;
    for (i = 0; i < len && *s; i++, s++)
        if (t[i] != *s && (t[i] < 'a' || t[i] > 'z' || t[i] - 'a' + 'A' != *s))
            return 0;
    return i == len && *s == '\0';
}

/* 查找匹配的指令定义 — 返回 instr_enc 数组的索引（-1=未找到） */
static int find_instr_def(const char *mnem, int mlen, AsmFmt want_fmt) {
    if (!mnem) return -1;
    if (tok_eq(mnem, mlen, "mov")) {
        if (want_fmt == F_RR)   return 0;
        if (want_fmt == F_IR)   return 1;
        if (want_fmt == F_IR8)  return 2;
        if (want_fmt == F_MR)   return 3;
        if (want_fmt == F_RM)   return 4;
        return -1;
    }
    if (tok_eq(mnem, mlen, "lea")) {
        if (want_fmt == F_LEA_MR) return 5;
        if (want_fmt == F_LEA_MI) return 6;
        return -1;
    }
    if (tok_eq(mnem, mlen, "call")) {
        if (want_fmt == F_CALL_DIR) return 7;
        if (want_fmt == F_CALL_IND) return 8;
        return -1;
    }
    if (tok_eq(mnem, mlen, "syscall") && want_fmt == F_NONE) return 9;
    if (tok_eq(mnem, mlen, "xor") && want_fmt == F_RR) return 10;
    if (tok_eq(mnem, mlen, "and") && want_fmt == F_IR) return 11;
    if (tok_eq(mnem, mlen, "sub") && want_fmt == F_IR) return 12;
    if (tok_eq(mnem, mlen, "test") && want_fmt == F_RR) return 13;
    if ((tok_eq(mnem, mlen, "jnz")||tok_eq(mnem, mlen, "jne")) && want_fmt == F_JCC8) return 14;
    if (tok_eq(mnem, mlen, "je") && want_fmt == F_JCC8) return 15;
    if (tok_eq(mnem, mlen, "jmp") && want_fmt == F_JCC8) return 16;
    if (tok_eq(mnem, mlen, "pop") && want_fmt == F_POP) return 17;
    if (tok_eq(mnem, mlen, "push") && want_fmt == F_PUSH) return 18;
    if (tok_eq(mnem, mlen, "hlt") && want_fmt == F_NONE) return 19;
    if (tok_eq(mnem, mlen, "ret") && want_fmt == F_NONE) return 20;
    if (tok_eq(mnem, mlen, "nop") && want_fmt == F_NONE) return 21;
    return -1;
}

/* 解析一个操作数 */
static int parse_operand(const char *line, Operand *op, const char **endp) {
    int tlen;
    const char *t = next_tok(line, &tlen);
    if (tlen == 0) { op->kind = O_NONE; *endp = t; return 0; }
    op->kind = O_NONE; op->reg = -1; op->imm = 0;
    op->label = NULL; op->reg2 = -1; op->scale = 1;

    /* 数字后紧跟 ( 表示内存引用: disp(%reg) */
    if ((*t >= '0' && *t <= '9') || *t == '-') {
        /* 解析数字 */
        long val = 0;
        const char *ns = t;
        int neg = 0;
        if (*ns == '-') { neg = 1; ns++; }
        while (ns < t + tlen) { val = val * 10 + (*ns - '0'); ns++; }
        if (neg) val = -val;
        /* 检查后面是否跟着 ( → 内存引用 */
        const char *after = t + tlen;
        while (*after == ' ' || *after == '\t') after++;
        if (*after == '(') {
            /* disp(%reg) — 进入内存引用解析 */
            const char *q = after + 1;  /* 跳过 ( */
            int t2len;
            const char *t2 = next_tok(q, &t2len);
            if (t2len < 2 || *t2 != '%') return -1;
            char rname[16]; int ri;
            for (ri = 0; ri < t2len - 1 && ri < 15; ri++) rname[ri] = t2[ri + 1];
            rname[ri] = '\0';
            int base = find_reg(rname);
            if (base < 0) return -1;
            q = t2 + t2len;
            t2 = next_tok(q, &t2len);
            if (t2len == 1 && *t2 == ')') {
                /* disp(%base) */
                op->kind = O_MEM_DISP;
                op->reg = base;
                op->imm = val;
                *endp = t2 + 1;
                return 0;
            }
            if (t2len == 1 && *t2 == ',') {
                /* disp(%base,%idx,scale) — SIB */
                const char *t3 = next_tok(t2 + 1, &t2len);
                if (t2len < 2 || *t3 != '%') return -1;
                char iname[16]; int ii;
                for (ii = 0; ii < t2len - 1 && ii < 15; ii++) iname[ii] = t3[ii + 1];
                iname[ii] = '\0';
                int idx = find_reg(iname);
                if (idx < 0) return -1;
                const char *t4 = next_tok(t3 + t2len, &t2len);
                int scale = 1;
                if (t2len == 1 && *t4 == ',') {
                    const char *t5 = next_tok(t4 + 1, &t2len);
                    scale = 0;
                    while (t2len > 0 && *t5 >= '0' && *t5 <= '9')
                        { scale = scale * 10 + (*t5 - '0'); t5++; t2len--; }
                    if (scale == 0) scale = 1;
                    t4 = next_tok(t5, &t2len);
                }
                if (t2len != 1 || *t4 != ')') return -1;
                op->kind = O_MEM_SIB;
                op->reg = base;
                op->reg2 = idx;
                op->scale = scale;
                op->imm = val;
                *endp = t4 + 1;
                return 0;
            }
            return -1;
        } else {
            op->kind = O_IMM;
            op->imm = val;
            *endp = t + tlen;
            return 0;
        }
    }

    if (*t == '%') {
        /* 寄存器 */
        char name[16]; int i;
        for (i = 0; i < tlen - 1 && i < 15; i++) name[i] = t[i + 1];
        name[i] = '\0';
        int rc = find_reg(name);
        if (rc < 0) return -1;
        op->kind = O_REG;
        op->reg = rc;
        *endp = t + tlen;
        return 0;
    }

    if (*t == '*') {
        /* 间接 (call *%reg) — '*' 本身只是一个字符，需要读取后面的 %reg */
        const char *after_star = t + 1;
        int t2len;
        const char *t2 = next_tok(after_star, &t2len);
        if (t2len < 2 || *t2 != '%') return -1;
        char name[16]; int i;
        for (i = 0; i < t2len - 1 && i < 15; i++) name[i] = t2[i + 1];
        name[i] = '\0';
        int rc = find_reg(name);
        if (rc < 0) return -1;
        op->kind = O_INDIRECT;
        op->reg = rc;
        *endp = t2 + t2len;
        return 0;
    }

    if (*t == '$') {
        /* 立即数 */
        const char *ns = t + 1;
        int neg = 0;
        if (*ns == '-') { neg = 1; ns++; }
        long val = 0;
        if (ns[0] == '0' && (ns[1] == 'x' || ns[1] == 'X')) {
            ns += 2;
            while (*ns >= '0' && *ns <= '9')
                { val = val * 16 + (*ns - '0'); ns++; }
            while (*ns >= 'a' && *ns <= 'f')
                { val = val * 16 + (*ns - 'a' + 10); ns++; }
            while (*ns >= 'A' && *ns <= 'F')
                { val = val * 16 + (*ns - 'A' + 10); ns++; }
        } else {
            while (*ns >= '0' && *ns <= '9')
                { val = val * 10 + (*ns - '0'); ns++; }
        }
        if (neg) val = -val;
        op->kind = O_IMM;
        op->imm = val;
        *endp = t + tlen;
        return 0;
    }

    if (*t == '(') {
        /* 内存引用: (reg) 或 disp(reg) 或 disp(%base,%idx,scale) */
        /* 回退到 '(' 之前看有没有偏移 */
        const char *p = line;
        while (p < t && (*p == ' ' || *p == '\t')) p++;
        int has_disp = 0;
        long disp = 0;
        if (p < t) {
            /* 有偏移 */
            has_disp = 1;
            int dlen = (int)(t - p);
            const char *ds = p;
            int neg = 0;
            if (*ds == '-') { neg = 1; ds++; }
            while (ds < t) { disp = disp * 10 + (*ds - '0'); ds++; }
            if (neg) disp = -disp;
        }
        /* 跳过 '(' */
        const char *q = t + 1;
        /* 读第一个寄存器 */
        int t2len;
        const char *t2 = next_tok(q, &t2len);
        if (t2len < 2 || *t2 != '%') return -1;
        char rname[16]; int ri;
        for (ri = 0; ri < t2len - 1 && ri < 15; ri++) rname[ri] = t2[ri + 1];
        rname[ri] = '\0';
        int base = find_reg(rname);
        if (base < 0) return -1;

        /* 读下一个 token */
        q = t2 + t2len;
        t2 = next_tok(q, &t2len);

        if (t2len == 1 && *t2 == ')') {
            /* 简单内存引用: disp(%base) 或 (%base) */
            if (has_disp)
                op->kind = O_MEM_DISP;
            else
                op->kind = O_MEM;
            op->reg = base;
            op->imm = disp;
            *endp = t2 + 1;
            return 0;
        }

        if (t2len == 1 && *t2 == ',') {
            /* SIB: disp(%base,%idx,scale) */
            /* 读 index */
            const char *t3 = next_tok(t2 + 1, &t2len);
            if (t2len < 2 || *t3 != '%') return -1;
            char iname[16]; int ii;
            for (ii = 0; ii < t2len - 1 && ii < 15; ii++) iname[ii] = t3[ii + 1];
            iname[ii] = '\0';
            int idx = find_reg(iname);
            if (idx < 0) return -1;

            /* 读下一个: ',' 或 ')' */
            const char *t4 = next_tok(t3 + t2len, &t2len);
            int scale = 1;
            if (t2len == 1 && *t4 == ',') {
                /* 读 scale */
                const char *t5 = next_tok(t4 + 1, &t2len);
                scale = 0;
                while (t2len > 0 && *t5 >= '0' && *t5 <= '9') {
                    scale = scale * 10 + (*t5 - '0');
                    t5++; t2len--;
                }
                if (scale == 0) scale = 1;
                t4 = next_tok(t5, &t2len);
            }
            if (t2len != 1 || *t4 != ')') return -1;
            op->kind = O_MEM_SIB;
            op->reg = base;
            op->reg2 = idx;
            op->scale = scale;
            op->imm = disp;
            *endp = t4 + 1;
            return 0;
        }
        return -1;
    }

    if ((*t >= 'a' && *t <= 'z') || (*t >= 'A' && *t <= 'Z')
        || *t == '_' || *t == '.') {
        /* 标号 (用于 call label) */
        op->kind = O_IMM;  /* 暂存为立即数，编码时特殊处理 */
        op->label = t;  /* 不 null-terminated，编码时用 strncmp */
        *endp = t + tlen;
        return 0;
    }

    if ((*t >= '0' && *t <= '9') || *t == '-') {
        long val = 0;
        const char *ns = t;
        int neg = 0;
        if (*ns == '-') { neg = 1; ns++; }
        while (ns < t + tlen) { val = val * 10 + (*ns - '0'); ns++; }
        if (neg) val = -val;
        op->kind = O_IMM;
        op->imm = val;
        *endp = t + tlen;
        return 0;
    }

    return -1;
}

/* ─── 指令编码 ─── */

/* 编码并发射一条指令。
 * idx: instr_enc 数组中的索引
 * ops[0..1]: 操作数 (call label 使用 ops[0].label)
 * local_label: JCC 目标数字标号 (0=无, >0=前向引用, <0=后向引用)
 * 返回 0=成功, -1=失败 */
static int encode_instr(int idx, Operand *op0, Operand *op1,
                        const char *call_label, int local_label) {
    /* 从平面数组提取指令编码 */
    int ocode0 = instr_enc[idx * 5 + 0];
    int ocode1 = instr_enc[idx * 5 + 1];
    int op_len = instr_enc[idx * 5 + 2];
    int ext_val = instr_enc[idx * 5 + 3];
    int enc_fmt = instr_enc[idx * 5 + 4];
    /* 提取寄存器编号（去除大小标志） */
    int r0 = (op0 && op0->reg >= 0) ? (op0->reg & REG_IDX_MASK) : 0;
    int r1 = (op1 && op1->reg >= 0) ? (op1->reg & REG_IDX_MASK) : 0;
    int sz0 = (op0 && op0->reg >= 0) ? (op0->reg & REG_SIZE_MASK) : REG_SIZE64;
    int sz1 = (op1 && op1->reg >= 0) ? (op1->reg & REG_SIZE_MASK) : REG_SIZE64;

    switch (enc_fmt) {
    case F_NONE:
        { int i; for (i = 0; i < op_len; i++) e1(i == 0 ? ocode0 : ocode1); }
        return 0;

    case F_RR:
        {
            int use_w = ((r0 >= 0 && (sz0 == REG_SIZE64)) ||
                         (r1 >= 0 && (sz1 == REG_SIZE64)));
            emit_rex_rr(use_w ? 1 : 0, r0, r1);
        }
        e1(ocode0);
        e1(make_modrm(3, r0, r1));
        return 0;

    case F_IR:
        {
            emit_rex_rm(1, ext_val, r1);
            e1(ocode0);
            e1(make_modrm(3, ext_val, r1));
            long v = op0 ? op0->imm : 0;
            if (ocode0 == 0x83) {
                e1(v & 0xFF);
            } else {
                e4((int)v);
            }
        }
        return 0;

    case F_IR8:
        if (!op0 || !op1) return -1;
        e1(ocode0 + r1);
        e1((int)op0->imm & 0xFF);
        return 0;

    case F_MR:
        if (!op0 || !op1) return -1;
        {
            int base = op0->reg & REG_IDX_MASK;
            if (op0->kind == O_MEM && base == 4) {
                emit_rex_rm(1, r1, base);
                e1(ocode0);
                e1(make_modrm(0, r1, 4));
                e1(make_sib(0, 4, 4));
            } else if (op0->kind == O_MEM_DISP) {
                int disp = (int)op0->imm;
                if (base == 4) {
                    emit_rex_rm(1, r1, base);
                    e1(ocode0);
                    if (disp >= -128 && disp < 128) {
                        e1(make_modrm(1, r1, 4));
                        e1(make_sib(0, 4, 4));
                        e1(disp & 0xFF);
                    } else {
                        e1(make_modrm(2, r1, 4));
                        e1(make_sib(0, 4, 4));
                        e4(disp);
                    }
                } else {
                    emit_rex_rm(1, r1, base);
                    e1(ocode0);
                    if (disp == 0) {
                        e1(make_modrm(0, r1, base));
                    } else if (disp >= -128 && disp < 128) {
                        e1(make_modrm(1, r1, base));
                        e1(disp & 0xFF);
                    } else {
                        e1(make_modrm(2, r1, base));
                        e4(disp);
                    }
                }
            } else {
                emit_rex_rm(1, r1, base);
                e1(ocode0);
                if (base == 5) {
                    e1(make_modrm(1, r1, 5));
                    e1(0);
                } else if (base == 4) {
                    e1(make_modrm(0, r1, 4));
                    e1(make_sib(0, 4, 4));
                } else {
                    e1(make_modrm(0, r1, base));
                }
            }
        }
        return 0;

    case F_RM:
        if (!op0 || !op1) return -1;
        {
            int base = op1->reg & REG_IDX_MASK;
            emit_rex_rm(1, r0, base);
            e1(ocode0);
            if (base == 4) {
                e1(make_modrm(0, r0, 4));
                e1(make_sib(0, 4, 4));
            } else {
                e1(make_modrm(0, r0, base));
            }
        }
        return 0;

    case F_LEA_MR:
        if (!op0 || !op1) return -1;
        {
            int base = op0->reg & REG_IDX_MASK;
            int disp = (int)op0->imm;
            emit_rex_rm(1, r1, base);
            e1(ocode0);
            if (base == 4) {
                if (disp >= -128 && disp < 128) {
                    e1(make_modrm(1, r1, 4));
                    e1(make_sib(0, 4, 4));
                    e1(disp & 0xFF);
                } else {
                    e1(make_modrm(2, r1, 4));
                    e1(make_sib(0, 4, 4));
                    e4(disp);
                }
            } else {
                if (disp == 0 && base != 5) {
                    e1(make_modrm(0, r1, base));
                } else if (disp >= -128 && disp < 128) {
                    e1(make_modrm(1, r1, base));
                    e1(disp & 0xFF);
                } else {
                    e1(make_modrm(2, r1, base));
                    e4(disp);
                }
            }
        }
        return 0;

    case F_LEA_MI:
        if (!op0 || !op1) return -1;
        {
            int base = op0->reg & REG_IDX_MASK;
            int idx  = (op0->reg2 >= 0) ? (op0->reg2 & REG_IDX_MASK) : 4;
            int scale = op0->scale;
            int sbits = (scale == 8) ? 3 : (scale == 4) ? 2 : (scale == 2) ? 1 : 0;
            int disp = (int)op0->imm;
            emit_rex(1, reg_hi(r1), reg_hi(idx), reg_hi(base));
            e1(ocode0);
            if (disp >= -128 && disp < 128) {
                e1(make_modrm(1, r1, 4));
                e1(make_sib(sbits, idx, base));
                e1(disp & 0xFF);
            } else {
                e1(make_modrm(2, r1, 4));
                e1(make_sib(sbits, idx, base));
                e4(disp);
            }
        }
        return 0;

    case F_CALL_DIR:
        if (!call_label) return -1;
        {
            char lbuf[128]; int lbi = 0;
            while (call_label[lbi] && lbi < 127
                   && call_label[lbi] != ' ' && call_label[lbi] != '\t'
                   && call_label[lbi] != '\n' && call_label[lbi] != '\r')
                { lbuf[lbi] = call_label[lbi]; lbi++; }
            lbuf[lbi] = '\0';
            int si = find_sym(lbuf);
            if (si < 0) {
                si = add_sym(lbuf, 0, 0, 1, 1, 0);
            }
            e1(0xE8);
            int reloc_off = elf_code_size;
            e4(0);
            add_rel(reloc_off, si, R_X86_64_PLT32, -4LL);
        }
        return 0;

    case F_CALL_IND:
        emit_rex_rr(0, 0, r0);
        e1(0xFF);
        e1(make_modrm(3, ext_val, r0));
        return 0;

    case F_JCC8:
        e1(ocode0);
        if (local_label > 0) {
            int n = local_label;
            if (n >= MAX_LOCAL_LABELS) {
                __write(2, "toyas: local label overflow\n", 26);
                __exit(1); }
            if (local_nfixups[n] >= 32) {
                __write(2, "toyas: too many fixups for local label\n", 37);
                __exit(1); }
            LOCAL_FIXUP(n, local_nfixups[n]++) = elf_code_size;
            e1(0);
        } else if (local_label < 0) {
            int n = -local_label;
            int target = local_offsets[n];
            if (target < 0) return -1;
            int disp = target - (elf_code_size + 1);
            e1(disp & 0xFF);
        } else {
            return -1;
        }
        return 0;

    case F_POP:
        if (r0 >= 8) e1(0x41);
        e1(ocode0 + (r0 & 7));
        return 0;

    case F_PUSH:
        if (r0 >= 8) e1(0x41);
        e1(ocode0 + (r0 & 7));
        return 0;

    default:
        return -1;
    }
}

/* ─── 主汇编循环 ─── */

/* 回填所有待处理的局部前向标号 */
static void apply_local_fixups(void) {
    int i, j;
    for (i = 0; i < MAX_LOCAL_LABELS; i++) {
        int target = local_offsets[i];
        if (target < 0) continue;
        for (j = 0; j < local_nfixups[i]; j++) {
            int fixup = LOCAL_FIXUP(i, j);
            int disp = target - (fixup + 1);
            elf_code_buf[fixup] = disp & 0xFF;
        }
    }
}

/* 扫描一行中是否包含 "Nf" 或 "Nb"（局部标号引用） */
/* 返回: >0 = 前向引用号, <0 = 后向引用号, 0 = 无引用 */
static int scan_local_ref(const char *line) {
    const char *p = line;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            int n = 0;
            while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
            if (n > 0 && n < MAX_LOCAL_LABELS) {
                if (*p == 'f' || *p == 'F') return n;   /* forward */
                if (*p == 'b' || *p == 'B') return -n;  /* backward */
            }
        }
        p++;
    }
    return 0;
}

int tas_assemble(const char *src, int len) {
    /* 初始化全局状态 */
    elf_code_size = 0;
    elf_sym_count = 0;
    elf_rel_count = 0;
    reset_locals();

    int last_label_sym = -1;   /* 上一个标号的符号索引（用于计算 size） */
    int next_is_global = 0;    /* 下一个标号标记为全局 */
    int next_is_func = 0;      /* 下一个标号标记为函数 */
    const char *line = src;
    const char *end = src + len;

    while (line < end) {
        /* 跳空白行 */
        while (line < end && (*line == '\n' || *line == '\r')) line++;
        if (line >= end) break;

        /* 找行尾 */
        const char *ln_end = line;
        while (ln_end < end && *ln_end != '\n' && *ln_end != '\r') ln_end++;

        /* 去除尾空白 */
        while (ln_end > line && (ln_end[-1] == ' ' || ln_end[-1] == '\t'))
            ln_end--;

        int llen = (int)(ln_end - line);
        if (llen == 0) { line = ln_end + 1; continue; }

        /* 跳行首空白 */
        const char *l = line;
        while (l < ln_end && (*l == ' ' || *l == '\t')) l++;
        if (l >= ln_end) { line = ln_end + 1; continue; }

        /* 跳过注释行 */
        if (*l == '#') { line = ln_end + 1; continue; }

        /* 跳过 "斜杠+星号" 块注释（支持多行） */
        if (l[0] == '/' && l[1] == '*') {
            const char *p = l + 2;
            while (p < end) {
                if (p[0] == '*' && p + 1 < end && p[1] == '/') {
                    line = p + 2;
                    break;
                }
                p++;
            }
            if (p >= end) line = end;
            continue;
        }

        /* ── 伪指令 ── */
        if (*l == '.') {
            int tlen;
            const char *t = next_tok(l, &tlen);
            /* .globl / .global */
            if (tok_eq(t, tlen, ".globl") || tok_eq(t, tlen, ".global")) {
                next_is_global = 1;
            }
            /* .extern */
            else if (tok_eq(t, tlen, ".extern")) {
                const char *a = next_tok(t + tlen, &tlen);
                if (tlen > 0) {
                    /* 创建未定义符号 — add_sym 内部已持久化名字 */
                    char nbuf[128]; int ni;
                    for (ni = 0; ni < tlen && ni < 127; ni++) nbuf[ni] = a[ni];
                    nbuf[ni] = '\0';
                    add_sym(nbuf, 0, 0, 1, 1, 0);  /* SHN_UNDEF */
                }
            }
            /* .text */
            else if (tok_eq(t, tlen, ".text")) {
                /* no-op: 默认就在 .text */
            }
            /* .hidden */
            else if (tok_eq(t, tlen, ".hidden")) {
                /* 简化：记录但不影响 .o 输出 (SHN_HIDDEN 在 st_other) */
            }
            /* .type name,@function */
            else if (tok_eq(t, tlen, ".type")) {
                next_is_func = 1;
            }
            line = ln_end + 1;
            continue;
        }

        /* ── 检查标号定义 (identifier: 或 N:) ── */
        const char *cp = l;
        int is_local_num = 0;
        int local_num = 0;

        if (*cp >= '0' && *cp <= '9') {
            /* 数字标号 */
            int n = 0;
            while (cp < ln_end && *cp >= '0' && *cp <= '9') { n = n * 10 + (*cp - '0'); cp++; }
            if (cp < ln_end && *cp == ':') {
                is_local_num = 1;
                local_num = n;
                l = cp + 1;
                while (l < ln_end && (*l == ' ' || *l == '\t')) l++;
            }
        } else if ((*cp >= 'a' && *cp <= 'z') || (*cp >= 'A' && *cp <= 'Z') || *cp == '_') {
            /* 命名标号 */
            const char *id_start = cp;
            while (cp < ln_end && ((*cp >= 'a' && *cp <= 'z')
                    || (*cp >= 'A' && *cp <= 'Z')
                    || (*cp >= '0' && *cp <= '9') || *cp == '_')) cp++;
            if (cp < ln_end && *cp == ':') {
                /* 标号定义 */
                int nlen = (int)(cp - id_start);
                char nbuf[256]; int ni;
                for (ni = 0; ni < nlen && ni < 255; ni++) nbuf[ni] = id_start[ni];
                nbuf[ni] = '\0';

                /* 终结前一个标号的 size */
                if (last_label_sym >= 0 && last_label_sym < elf_sym_count) {
                    elf_syms[last_label_sym].size =
                        elf_code_size - elf_syms[last_label_sym].offset;
                }

                last_label_sym = add_sym(nbuf, elf_code_size, 0,
                                         next_is_global, next_is_func, 1);  /* .text */
                next_is_global = 0;
                next_is_func = 0;

                l = cp + 1;
                while (l < ln_end && (*l == ' ' || *l == '\t')) l++;
                /* 标号后可能紧接指令（如 "1: ret"），继续处理 */
                if (l >= ln_end) { line = ln_end + 1; continue; }
                /* fall through to instruction parsing */
            }
        }

        if (is_local_num) {
            if (local_num >= MAX_LOCAL_LABELS) {
                __write(2, "toyas: too many local labels\n", 27);
                __exit(1); }
            local_offsets[local_num] = elf_code_size;
            /* 数字标号后可能有同行指令（如 "1: ret"） */
            while (l < ln_end && (*l == ' ' || *l == '\t')) l++;
            if (l >= ln_end) { line = ln_end + 1; continue; }
            /* fall through to instruction parsing */
        }

        /* 如果行剩余为空，跳过 */
        if (l >= ln_end) { line = ln_end + 1; continue; }

        /* ── 指令 ── */
        {
            /* 读取助记符 */
            int mlen;
            const char *mnem = next_tok(l, &mlen);
            if (mlen == 0) { line = ln_end + 1; continue; }
            int saved_mlen = mlen;  /* 保存！后续操作数解析会覆盖 mlen */

            /* 找指令定义 */
            int instr_idx = -1;
            AsmFmt want_fmt = F_NONE;
            Operand op0, op1;
            op0.kind = O_NONE; op1.kind = O_NONE;
            const char *label_ref = NULL;
            int local_label = 0;

            /* 解析操作数 */
            const char *after_mnem = mnem + saved_mlen;
            const char *op_pos = next_tok(after_mnem, &mlen);
            int has_op0 = 0, has_op1 = 0;

            if (mlen > 0) {
                if (parse_operand(after_mnem, &op0, &op_pos) == 0)
                    has_op0 = 1;
                /* 检查逗号 */
                const char *comma = next_tok(op_pos, &mlen);
                if (mlen == 1 && *comma == ',') {
                    if (parse_operand(comma + 1, &op1, &op_pos) == 0)
                        has_op1 = 1;
                }
            }

            /* 推测指令格式 — 先根据助记符做特殊判断 */
            if (tok_eq(mnem, saved_mlen, "call")) {
                if (has_op0 && (op0.kind == O_INDIRECT || op0.kind == O_REG))
                    want_fmt = F_CALL_IND;
                else if (has_op0 && op0.label)
                    { want_fmt = F_CALL_DIR; label_ref = op0.label; }
            } else if (tok_eq(mnem, saved_mlen, "lea")) {
                if (op0.kind == O_MEM_SIB)
                    want_fmt = F_LEA_MI;
                else
                    want_fmt = F_LEA_MR;
            } else if (tok_eq(mnem, saved_mlen, "jnz")
                       || tok_eq(mnem, saved_mlen, "jne")
                       || tok_eq(mnem, saved_mlen, "je")
                       || tok_eq(mnem, saved_mlen, "jmp")) {
                want_fmt = F_JCC8;
                local_label = scan_local_ref(l);
            } else if (tok_eq(mnem, saved_mlen, "ret")
                       || tok_eq(mnem, saved_mlen, "hlt")
                       || tok_eq(mnem, saved_mlen, "nop")
                       || tok_eq(mnem, saved_mlen, "syscall")) {
                want_fmt = F_NONE;
            } else if (tok_eq(mnem, saved_mlen, "pop")) {
                want_fmt = F_POP;
            } else if (tok_eq(mnem, saved_mlen, "push")) {
                want_fmt = F_PUSH;
            } else if (!has_op0 && !has_op1) {
                want_fmt = F_NONE;
            } else if (has_op0 && has_op1) {
                if (op0.kind == O_IMM && op1.kind == O_REG) {
                    if ((op1.reg & REG_SIZE_MASK) == REG_SIZE8)
                        want_fmt = F_IR8;
                    else
                        want_fmt = F_IR;
                } else if (op0.kind == O_REG && op1.kind == O_REG) {
                    want_fmt = F_RR;
                } else if ((op0.kind == O_MEM || op0.kind == O_MEM_DISP)
                           && op1.kind == O_REG) {
                    want_fmt = F_MR;
                } else if (op0.kind == O_REG
                           && (op1.kind == O_MEM || op1.kind == O_MEM_DISP)) {
                    want_fmt = F_RM;
                } else if ((op0.kind == O_MEM_DISP || op0.kind == O_MEM_SIB)
                           && op1.kind == O_REG) {
                    if (op0.kind == O_MEM_SIB)
                        want_fmt = F_LEA_MI;
                    else
                        want_fmt = F_LEA_MR;
                }
            } else if (has_op0) {
                if (op0.kind == O_INDIRECT) {
                    want_fmt = F_CALL_IND;
                } else if (op0.kind == O_REG) {
                    want_fmt = F_POP;
                } else if (op0.label) {
                    /* call label */
                    want_fmt = F_CALL_DIR;
                    label_ref = op0.label;
                }
            }

            /* 查找匹配的指令定义 */
            instr_idx = find_instr_def(mnem, saved_mlen, want_fmt);

            if (instr_idx < 0) {
                __write(2, "toyas: unknown instruction: ", 26);
                __write(2, mnem, saved_mlen < 10 ? saved_mlen : 10);
                __write(2, "\n", 1);
                line = ln_end + 1;
                continue;
            }

            if (encode_instr(instr_idx, has_op0 ? &op0 : NULL,
                             has_op1 ? &op1 : NULL,
                             label_ref, local_label) != 0) {
                __write(2, "toyas: encoding error: ", 21);
                __write(2, mnem, saved_mlen < 10 ? saved_mlen : 10);
                __write(2, "\n", 1);
            }
        }

        line = ln_end + 1;
    }

    /* 回填局部标号 */
    apply_local_fixups();

    /* 终结最后一个标号的 size */
    if (last_label_sym >= 0 && last_label_sym < elf_sym_count) {
        elf_syms[last_label_sym].size =
            elf_code_size - elf_syms[last_label_sym].offset;
    }

    return 0;
}

/* ─── 入口 ─── */

int main(int argc, char *argv[]) {
    const char *in_path = NULL;
    const char *out_path = NULL;

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (argv[i][0] != '-') {
            in_path = argv[i];
        }
    }

    if (!in_path) {
        __write(2, "usage: toyas input.S [-o output.o]\n", 33);
        return 1;
    }
    if (!out_path) out_path = "tas_out.o";

    /* 读文件 */
    int fd = __openat(AT_FDCWD, in_path, O_RDONLY, 0);
    if (fd < 0) {
        __write(2, "toyas: cannot open input file\n", 28);
        return 1;
    }

    static char src_buf[65536];
    int total = 0;
    int n;
    while ((n = __read(fd, src_buf + total, sizeof(src_buf) - total - 1)) > 0)
        total += n;
    __close(fd);
    src_buf[total] = '\0';

    /* 汇编 */
    tas_assemble(src_buf, total);

    /* 写 .o 文件 */
    if (elf_write_object(out_path) != 0) {
        __write(2, "toyas: failed to write output\n", 28);
        return 1;
    }

    __printf("toyas: wrote %s (%d bytes code, %d symbols)\n",
             out_path, elf_code_size, elf_sym_count);
    return 0;
}
