/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * toyld.c — ToyC x86_64 静态链接器
 *
 * 机制：读取 ET_REL(.o) 文件 → 合并段 → 解析符号 → 应用重定位
 *        → 输出 ET_EXEC 静态可执行文件（含 program headers）
 *
 * 支持的 x86_64 重定位类型：
 *   R_X86_64_64     S + A                    8 字节绝对地址（.data 段字符串指针）
 *   R_X86_64_PC32   S + A - P                4 字节 PC 相对（全局变量、成员访问）
 *   R_X86_64_32     S + A  < 2³²            4 字节零扩展绝对（sizeof 全局地址）
 *   R_X86_64_PLT32  S + A - P                4 字节（静态链接中同 PC32）
 *
 * 默认配置（硬编码）：
 *   基地址:   0x400000
 *   入口:     __tlibc_start
 *   段顺序:   .text → .data → .bss
 *
 * 用法：
 *   toyld input1.o [input2.o ...] -o output
 *
 * 零 libc 依赖，使用 toyc_need.h 的 syscall 包装。
 */

#include "toyc_need.h"
#include "elf.h"

/* ═══════════════════════════════════════════════════════════════════
 *  配置常量
 * ═══════════════════════════════════════════════════════════════════ */

#define MAX_INPUTS       256
#define MAX_SYMS         65536
#define BASE_ADDR        0x400000
#define PAGE_SIZE        0x1000

/* 入口符号，可由 -e 覆盖 */
static const char *entry_sym_name = "__tlibc_start";

/* ═══════════════════════════════════════════════════════════════════
 *  归档（.a）格式常量
 * ═══════════════════════════════════════════════════════════════════ */

#define SARMAG          8
#define ARMAG           "!<arch>\n"
#define AR_HDR_SIZE     60
#define H_NAME          0
#define H_SIZE          48
#define H_FMAG          58

#define MAX_ARCHIVES    16
#define MAX_AR_MEMBERS  256
#define MAX_AR_SYMS     4096

/* ═══════════════════════════════════════════════════════════════════
 *  数据结构
 * ═══════════════════════════════════════════════════════════════════ */

/* 输入 .o 文件解析结果 */
typedef struct {
    const char      *name;
    unsigned char   *data;          /* 完整文件内容 */
    long             size;

    /* 已知节区索引（-1 表示无此节区） */
    int              sh_text;
    int              sh_data;
    int              sh_bss;
    int              sh_symtab;
    int              sh_strtab;
    int              sh_rela_text;
    int              sh_rela_data;

    /* 节区原始数据指针 */
    unsigned char   *text_data;
    int              text_size;
    unsigned char   *data_data;
    int              data_size;
    int              bss_size;

    /* 符号表 */
    unsigned char   *syms_raw;
    int              sym_count;
    char            *strtab_data;
    int              strtab_size;

    /* 重定位 */
    unsigned char   *rela_text_raw;
    int              rela_text_count;
    unsigned char   *rela_data_raw;
    int              rela_data_count;

    /* 各段在合并后缓冲区中的偏移 */
    int              text_merge_off;
    int              data_merge_off;
    int              bss_merge_off;

    /* 各段在合并后的最终基地址 */
    uint64_t         text_base;
    uint64_t         data_base;
    uint64_t         bss_base;
} InputFile;

/* 全局符号表条目 */
typedef struct {
    const char     *name;  /* pointer to strtab */
    uint64_t        value;
    int             defined;
    unsigned char   sym_info;
} Symbol;

/* 归档成员：从 .a 文件中提取的单个 .o 文件 */
typedef struct {
    const char     *name;      /* 成员名 */
    unsigned char  *data;      /* 成员 .o 数据（独立分配） */
    int             size;
    int             loaded;    /* 已作为输入加载 */
    int             hdr_off;   /* ar_hdr 在归档中的偏移 */
} ArchiveMember;

/* 归档符号表条目 */
typedef struct {
    const char     *name;      /* 指向归档缓冲区的符号名 */
    int             member_idx;
} ArchiveSym;

/* 单个归档文件的状态 */
typedef struct {
    ArchiveMember   members[MAX_AR_MEMBERS];
    int             member_count;
    ArchiveSym      syms[MAX_AR_SYMS];
    int             sym_count;
    unsigned char  *raw_symtab; /* 原始符号表数据（指向归档缓冲区） */
} Archive;

/* ═══════════════════════════════════════════════════════════════════
 *  全局状态
 * ═══════════════════════════════════════════════════════════════════ */

static InputFile    inputs[MAX_INPUTS];
static int          input_count;
static const char  *output_path;

static Symbol       syms[MAX_SYMS];
static int          sym_count;

static Archive      archives[MAX_ARCHIVES];
static int          archive_count;

/* 合并后段缓冲区（堆分配） */
static unsigned char *merged_text;
static int           merged_text_size;
static unsigned char *merged_data;
static int           merged_data_size;
static int           merged_bss_size;

/* 最终段基址 */
static uint64_t     text_addr;
static uint64_t     data_addr;
static uint64_t     bss_addr;

/* 入口地址 */
static uint64_t     entry_addr;

/* ═══════════════════════════════════════════════════════════════════
 *  LE 整数读写辅助函数
 * ═══════════════════════════════════════════════════════════════════ */

static uint16_t r16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t r32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t r64(const unsigned char *p) {
    return (uint64_t)r32(p) | ((uint64_t)r32(p + 4) << 32);
}
static void w16(unsigned char *dest, uint16_t v) {
    dest[0] = v & 0xFF; dest[1] = (v >> 8) & 0xFF;
}
static void w32(unsigned char *dest, uint32_t v) {
    dest[0] = v & 0xFF; dest[1] = (v >> 8) & 0xFF;
    dest[2] = (v >> 16) & 0xFF; dest[3] = (v >> 24) & 0xFF;
}
static void w64(unsigned char *dest, uint64_t v) {
    w32(dest, v & 0xFFFFFFFF);
    w32(dest + 4, (v >> 32) & 0xFFFFFFFF);
}

/* 大端 uint32 读取 */
static uint32_t r32be(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ═══════════════════════════════════════════════════════════════════
 *  错误处理
 * ═══════════════════════════════════════════════════════════════════ */

static void error(const char *fmt, const char *arg) {
    __write(2, "toyld: error: ", 12);
    __write(2, fmt, strlen(fmt));
    if (arg) { __write(2, ": ", 2); __write(2, arg, strlen(arg)); }
    __write(2, "\n", 1);
    __exit(1);
}

/* ═══════════════════════════════════════════════════════════════════
 *  文件 I/O
 * ═══════════════════════════════════════════════════════════════════ */

static unsigned char *read_whole_file(const char *path, long *out_size) {
    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) error("cannot open input file", path);

    long size = __lseek(fd, 0, SEEK_END);
    if (size < 0) { __close(fd); error("cannot seek input file", path); }
    __lseek(fd, 0, SEEK_SET);

    unsigned char *buf = (unsigned char *)tlibc_malloc(size + 64);
    if (!buf) { __close(fd); error("out of memory reading", path); }

    long total = 0;
    while (total < size) {
        long got = __read(fd, buf + total, size - total);
        if (got <= 0) break;
        total += got;
    }
    __close(fd);
    if (total != size) {
        tlibc_free(buf);
        error("short read on input file", path);
    }
    /* 末尾多 zero-pad 防止字符串越界 */
    buf[size] = 0; buf[size + 1] = 0;
    buf[size + 2] = 0; buf[size + 3] = 0;

    *out_size = size;
    return buf;
}

/* ═══════════════════════════════════════════════════════════════════
 *  ELF .o 文件解析
 * ═══════════════════════════════════════════════════════════════════ */

static int find_sh_by_name(InputFile *f, const char *target) {
    int shnum  = r16(f->data + 60);    /* e_shnum */
    int shstrndx = r16(f->data + 62);  /* e_shstrndx */
    if (shnum <= 0 || shstrndx < 0 || shstrndx >= shnum) return -1;

    int shoff = r64(f->data + 40);     /* e_shoff */
    unsigned char *shdr_base = f->data + shoff;

    /* .shstrtab section header → 内容 */
    unsigned char *shstr_sh = shdr_base + shstrndx * 64;
    uint64_t shstr_off = r64(shstr_sh + 24);
    unsigned char *shstr_data = f->data + shstr_off;

    for (int i = 1; i < shnum; i++) {
        unsigned char *sh = shdr_base + i * 64;
        uint32_t name_off = r32(sh);
        const char *sname = (const char *)(shstr_data + name_off);
        if (strcmp(sname, target) == 0) return i;
    }
    return -1;
}

static void read_input(const char *path) {
    if (input_count >= MAX_INPUTS)
        error("too many input files (max 64)", NULL);

    InputFile *f = &inputs[input_count];
    f->name = path;
    f->sh_text = f->sh_data = f->sh_bss = f->sh_symtab = f->sh_strtab = -1;
    f->sh_rela_text = f->sh_rela_data = -1;

    f->data = read_whole_file(path, &f->size);

    /* ── 校验 ELF header ── */
    if (f->size < 64 || f->data[0] != 0x7F || f->data[1] != 'E'
        || f->data[2] != 'L' || f->data[3] != 'F')
        error("not a valid ELF file", path);
    if (f->data[4] != 2)
        error("not a 64-bit ELF file", path);
    if (r16(f->data + 16) != 1)
        error("not a relocatable object file (.o expected)", path);
    if (r16(f->data + 18) != 62)
        error("not an x86_64 ELF file", path);

    /* ── 查找各段 ── */
    f->sh_text      = find_sh_by_name(f, ".text");
    f->sh_data      = find_sh_by_name(f, ".data");
    f->sh_bss       = find_sh_by_name(f, ".bss");
    f->sh_symtab    = find_sh_by_name(f, ".symtab");
    f->sh_strtab    = find_sh_by_name(f, ".strtab");
    f->sh_rela_text = find_sh_by_name(f, ".rela.text");
    f->sh_rela_data = find_sh_by_name(f, ".rela.data");

    int shnum = r16(f->data + 60);
    int shoff = r64(f->data + 40);
    unsigned char *shdr_base = f->data + shoff;

    /* 辅助宏：读取段偏移和大小 */
    #define READ_SH(i, off_var, sz_var) do { \
        if ((i) >= 0 && (i) < shnum) { \
            unsigned char *_sh = shdr_base + (i) * 64; \
            off_var = (int)r64(_sh + 24); \
            sz_var  = (int)r64(_sh + 32); \
        } \
    } while(0)

    /* .text */
    if (f->sh_text >= 0) { int off=0, sz=0;
        READ_SH(f->sh_text, off, sz);
        f->text_data = f->data + off; f->text_size = sz; }

    /* .data */
    if (f->sh_data >= 0) { int off=0, sz=0;
        READ_SH(f->sh_data, off, sz);
        f->data_data = f->data + off; f->data_size = sz; }

    /* .bss — 只有大小，无数据 */
    if (f->sh_bss >= 0) {
        f->bss_size = (int)r64((shdr_base + f->sh_bss * 64) + 32); }

    /* .symtab */
    if (f->sh_symtab >= 0) { int off=0, sz=0;
        READ_SH(f->sh_symtab, off, sz);
        f->syms_raw = f->data + off; f->sym_count = sz / 24; }

    /* .strtab */
    if (f->sh_strtab >= 0) { int off=0, sz=0;
        READ_SH(f->sh_strtab, off, sz);
        f->strtab_data = (char *)f->data + off; f->strtab_size = sz; }

    /* .rela.text */
    if (f->sh_rela_text >= 0) { int off=0, sz=0;
        READ_SH(f->sh_rela_text, off, sz);
        f->rela_text_raw = f->data + off; f->rela_text_count = sz / 24; }

    /* .rela.data */
    if (f->sh_rela_data >= 0) { int off=0, sz=0;
        READ_SH(f->sh_rela_data, off, sz);
        f->rela_data_raw = f->data + off; f->rela_data_count = sz / 24; }

    #undef READ_SH

    input_count++;
}

/* ═══════════════════════════════════════════════════════════════════
 *  从内存缓冲区读取 ELF .o（用于归档成员）
 * ═══════════════════════════════════════════════════════════════════ */

static void read_input_mem(const char *name, unsigned char *data, int size) {
    if (input_count >= MAX_INPUTS)
        error("too many input files", NULL);

    InputFile *f = &inputs[input_count];
    f->name = name;
    f->sh_text = f->sh_data = f->sh_bss = f->sh_symtab = f->sh_strtab = -1;
    f->sh_rela_text = f->sh_rela_data = -1;

    f->data = data;
    f->size = size;

    /* ── 校验 ELF header ── */
    if (f->size < 64 || f->data[0] != 0x7F || f->data[1] != 'E'
        || f->data[2] != 'L' || f->data[3] != 'F')
        error("not a valid ELF member in archive", name);
    if (f->data[4] != 2)
        error("not a 64-bit ELF member in archive", name);
    if (r16(f->data + 16) != 1)
        error("not a relocatable object member in archive", name);
    if (r16(f->data + 18) != 62)
        error("not an x86_64 ELF member in archive", name);

    /* ── 查找各段 ── */
    f->sh_text      = find_sh_by_name(f, ".text");
    f->sh_data      = find_sh_by_name(f, ".data");
    f->sh_bss       = find_sh_by_name(f, ".bss");
    f->sh_symtab    = find_sh_by_name(f, ".symtab");
    f->sh_strtab    = find_sh_by_name(f, ".strtab");
    f->sh_rela_text = find_sh_by_name(f, ".rela.text");
    f->sh_rela_data = find_sh_by_name(f, ".rela.data");

    int shnum = r16(f->data + 60);
    int shoff = r64(f->data + 40);
    unsigned char *shdr_base = f->data + shoff;

    #define READ_SH(i, off_var, sz_var) do { \
        if ((i) >= 0 && (i) < shnum) { \
            unsigned char *_sh = shdr_base + (i) * 64; \
            off_var = (int)r64(_sh + 24); \
            sz_var  = (int)r64(_sh + 32); \
        } \
    } while(0)

    /* .text */
    if (f->sh_text >= 0) { int off=0, sz=0;
        READ_SH(f->sh_text, off, sz);
        f->text_data = f->data + off; f->text_size = sz; }

    /* .data */
    if (f->sh_data >= 0) { int off=0, sz=0;
        READ_SH(f->sh_data, off, sz);
        f->data_data = f->data + off; f->data_size = sz; }

    /* .bss — 只有大小，无数据 */
    if (f->sh_bss >= 0) {
        f->bss_size = (int)r64((shdr_base + f->sh_bss * 64) + 32); }

    /* .symtab */
    if (f->sh_symtab >= 0) { int off=0, sz=0;
        READ_SH(f->sh_symtab, off, sz);
        f->syms_raw = f->data + off; f->sym_count = sz / 24; }

    /* .strtab */
    if (f->sh_strtab >= 0) { int off=0, sz=0;
        READ_SH(f->sh_strtab, off, sz);
        f->strtab_data = (char *)f->data + off; f->strtab_size = sz; }

    /* .rela.text */
    if (f->sh_rela_text >= 0) { int off=0, sz=0;
        READ_SH(f->sh_rela_text, off, sz);
        f->rela_text_raw = f->data + off; f->rela_text_count = sz / 24; }

    /* .rela.data */
    if (f->sh_rela_data >= 0) { int off=0, sz=0;
        READ_SH(f->sh_rela_data, off, sz);
        f->rela_data_raw = f->data + off; f->rela_data_count = sz / 24; }

    #undef READ_SH

    input_count++;
}

/* ═══════════════════════════════════════════════════════════════════
 *  符号注册（用于归档延迟加载前的 UNDEF 检测）
 * ═══════════════════════════════════════════════════════════════════ */

/* 前向声明（GCC 要求定义前调用） */
static const char *sym_name(InputFile *f, int idx);

/* 将指定输入文件的全局 UNDEF 符号注册到 syms 表 */
static void register_input_undefs(int idx) {
    InputFile *f = &inputs[idx];
    if (!f->syms_raw) return;
    for (int si = 1; si < f->sym_count; si++) {
        unsigned char *sym = f->syms_raw + si * 24;
        unsigned char info = sym[4];
        int bind = (info >> 4) & 0xF;
        if (bind == 0) continue;                       /* STB_LOCAL */
        if (r16(sym + 6) != 0) continue;               /* 不是 UNDEF */
        const char *name = sym_name(f, si);
        if (!name || !name[0]) continue;
        find_or_add_sym(name);  /* 创建 UNDEF 条目（如不存在） */
    }
}

/* 检查符号是否已定义 */
static int sym_is_undef(const char *name) {
    for (int i = 0; i < sym_count; i++)
        if (syms[i].name && strcmp(syms[i].name, name) == 0 && !syms[i].defined)
            return 1;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  归档文件（.a）解析与延迟加载
 * ═══════════════════════════════════════════════════════════════════ */

/* 复制内存（通过函数参数传递，避免 toyc 在局部变量中截断 64 位指针） */
static void copy_bytes(unsigned char *d, const unsigned char *s, int n) {
    int i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

/* 解析十进制字符串 */
static int parse_dec(const char *s, int maxlen) {
    int v = 0;
    for (int i = 0; i < maxlen && s[i] >= '0' && s[i] <= '9'; i++)
        v = v * 10 + (s[i] - '0');
    return v;
}

/* 在归档中查找成员索引（通过 ar_hdr 偏移） */
static int find_ar_member(Archive *a, int hdr_off) {
    for (int i = 0; i < a->member_count; i++)
        if (a->members[i].hdr_off == hdr_off) return i;
    return -1;
}

/* 读取并解析 .a 归档文件 */
static void read_archive(const char *path) {
    if (archive_count >= MAX_ARCHIVES)
        error("too many archive files", NULL);

    Archive *a = &archives[archive_count];
    a->member_count = 0;
    a->sym_count = 0;
    a->raw_symtab = NULL;

    long fsz;
    unsigned char *buf = read_whole_file(path, &fsz);

    /* ── 校验 magic ── */
    if (fsz < SARMAG) error("bad archive", path);
    if (buf[0] != '!' || buf[1] != '<' || buf[2] != 'a' || buf[3] != 'r'
        || buf[4] != 'c' || buf[5] != 'h' || buf[6] != '>' || buf[7] != '\n')
        error("bad archive magic", path);

    int pos = SARMAG;
    char *lname_data = NULL;
    int lname_sz = 0;

    /* ── Phase 1: 扫描所有成员 ── */
    while (pos + AR_HDR_SIZE <= fsz) {
        /* 读取名称长度（到空格或 '/' 的字符数） */
        int nl = 0;
        while (nl < 16 && buf[pos + nl] && buf[pos + nl] != ' ') nl++;

        int dsz = parse_dec((const char *)buf + pos + H_SIZE, 10);
        int dp = pos + AR_HDR_SIZE;
        if (dp + dsz > fsz) error("truncated archive", path);

        /* 校验 fmag */
        if (buf[pos + H_FMAG] != '`' || buf[pos + H_FMAG + 1] != '\n')
            error("bad archive header", path);

        if (nl == 1 && buf[pos] == '/') {
            /* 符号表成员 — 保存原始数据，Phase 2 再解析 */
            a->raw_symtab = buf + dp;
        } else if (nl >= 2 && buf[pos] == '/' && buf[pos + 1] == '/') {
            /* 长名表 */
            lname_data = (char *)buf + dp;
            lname_sz = dsz;
        } else if (!(nl == 1 && buf[pos] == ' ')) {
            /* 普通成员（跳过尾部的 ` ` 空记录） */
            if (a->member_count >= MAX_AR_MEMBERS)
                error("too many archive members", path);

            ArchiveMember *m = &a->members[a->member_count];
            m->hdr_off = pos;
            m->loaded = 0;
            m->size = dsz;
            m->data = NULL;
            m->name = NULL;

            /* 分配并复制成员数据（末尾补零） */
            m->data = (unsigned char *)tlibc_malloc(dsz + 4);
            if (!m->data) error("out of memory reading archive", path);
            copy_bytes(m->data, buf + dp, dsz);
            m->data[dsz] = 0; m->data[dsz+1] = 0;
            m->data[dsz+2] = 0; m->data[dsz+3] = 0;

            /* 提取成员名 */
            if (nl >= 2 && buf[pos] == '/'
                && buf[pos + 1] >= '0' && buf[pos + 1] <= '9' && lname_data)
            {
                /* 长名引用：/N → N 指向长名表偏移 */
                int noff = parse_dec((const char *)buf + pos + 1, 15);
                if (noff < lname_sz) {
                    int end = noff;
                    while (end < lname_sz && lname_data[end] != '\n') end++;
                    int llen = end - noff;
                    char *nb = (char *)tlibc_malloc(llen + 1);
                    if (nb) {
                        copy_bytes((unsigned char *)nb,
                                   (const unsigned char *)lname_data + noff, llen);
                        nb[llen] = 0;
                        m->name = nb;
                    }
                }
            }
            if (!m->name) {
                /* 短名：去掉末尾的 '/' */
                int sl = nl;
                if (sl > 0 && buf[pos + sl - 1] == '/') sl--;
                char *nb = (char *)tlibc_malloc(sl + 1);
                if (nb) {
                    for (int i = 0; i < sl; i++) nb[i] = buf[pos + i];
                    nb[sl] = 0;
                    m->name = nb;
                }
            }

            a->member_count++;
        }

        pos = dp + dsz;
        if (pos & 1) pos++;
    }

    /* ── Phase 2: 解析符号表 ── */
    if (a->raw_symtab) {
        int nsym = (int)r32be(a->raw_symtab);
        if (nsym > 0 && nsym < (fsz - (a->raw_symtab - buf)) / 4) {
            int name_pos = 4 + nsym * 4;
            for (int i = 0; i < nsym && a->sym_count < MAX_AR_SYMS; i++) {
                int member_off = (int)r32be(a->raw_symtab + 4 + i * 4);
                const char *sym_name = (const char *)(a->raw_symtab + name_pos);

                int mi = find_ar_member(a, member_off);

                /* 计算名称长度用于步进 */
                int nlen = 0;
                while (sym_name[nlen]) nlen++;

                if (mi >= 0 && sym_name[0]) {
                    a->syms[a->sym_count].name = sym_name;
                    a->syms[a->sym_count].member_idx = mi;
                    a->sym_count++;
                }

                name_pos += nlen + 1;
                if (name_pos >= (int)fsz - (int)(a->raw_symtab - buf)) break;
            }
        }
    }

    archive_count++;
}

/* 检查归档文件扩展名 */
static int is_archive_name(const char *path) {
    int len = 0;
    while (path[len]) len++;
    return (len >= 2 && path[len-2] == '.' && path[len-1] == 'a');
}

/* 延迟加载：从归档中取出需要的成员 */
static void lazy_load_archives(void) {
    int loaded;
    do {
        loaded = 0;
        for (int ai = 0; ai < archive_count && !loaded; ai++) {
            Archive *a = &archives[ai];
            for (int mi = 0; mi < a->member_count && !loaded; mi++) {
                if (a->members[mi].loaded) continue;
                /* 检查该成员是否定义了当前未定义的符号 */
                int needed = 0;
                for (int si = 0; si < a->sym_count && !needed; si++) {
                    if (a->syms[si].member_idx == mi
                        && sym_is_undef(a->syms[si].name))
                        needed = 1;
                }
                if (needed) {
                    read_input_mem(a->members[mi].name,
                                   a->members[mi].data,
                                   a->members[mi].size);
                    a->members[mi].loaded = 1;
                    /* 注册新输入中的 UNDEF，以便触发更多归档加载 */
                    register_input_undefs(input_count - 1);
                    loaded = 1;
                }
            }
        }
    } while (loaded);
}

/* ═══════════════════════════════════════════════════════════════════
 *  段合并
 * ═══════════════════════════════════════════════════════════════════ */

static int align_up(int val, int align) {
    if (align <= 1) return val;
    return (val + align - 1) & -align;
}

/* 只计算偏移和基地址，不复制数据（数据复制由 main 在 resolve 之前完成） */
static void merge_sections(void) {
    merged_text_size = 0;
    merged_data_size = 0;
    merged_bss_size  = 0;

    for (int i = 0; i < input_count; i++) {
        InputFile *f = &inputs[i];

        f->text_merge_off = merged_text_size;
        merged_text_size += f->text_size;
        merged_text_size = align_up(merged_text_size, 16);

        f->data_merge_off = merged_data_size;
        merged_data_size += f->data_size;
        merged_data_size = align_up(merged_data_size, 32);

        f->bss_merge_off = merged_bss_size;
        merged_bss_size += f->bss_size;
        merged_bss_size = align_up(merged_bss_size, 32);
    }

    text_addr = BASE_ADDR;
    data_addr = text_addr + merged_text_size;
    bss_addr  = data_addr + merged_data_size;

    for (int i = 0; i < input_count; i++) {
        InputFile *f = &inputs[i];
        f->text_base = text_addr + f->text_merge_off;
        f->data_base = data_addr + f->data_merge_off;
        f->bss_base  = bss_addr  + f->bss_merge_off;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  符号解析
 * ═══════════════════════════════════════════════════════════════════ */

static const char *sym_name(InputFile *f, int idx) {
    if (idx < 0 || idx >= f->sym_count) return "";
    unsigned char *sym = f->syms_raw + idx * 24;
    uint32_t name_off = r32(sym);
    if (name_off >= (uint32_t)f->strtab_size) return "";
    return f->strtab_data + name_off;
}

static Symbol *find_or_add_sym(const char *name) {
    for (int i = 0; i < sym_count; i++)
        if (strcmp(syms[i].name, name) == 0) return &syms[i];
    if (sym_count >= MAX_SYMS) error("too many symbols", NULL);
    Symbol *s = &syms[sym_count++];
    s->name = name;   /* 指向 strtab，避免 toyc 的 char 逐字节拷贝 bug */
    s->value = 0;
    s->defined = 0;
    s->sym_info = 0;
    return s;
}

/* 计算本地符号在最终输出中的地址 */
static uint64_t sym_final_addr(InputFile *f, unsigned char *sym_entry) {
    uint64_t st_value = r64(sym_entry + 8);
    uint16_t st_shndx = r16(sym_entry + 6);

    if (st_shndx == 0) return 0;              /* SHN_UNDEF */
    if (st_shndx == 0xFFF1) return st_value;  /* SHN_ABS */
    if (st_shndx == (uint16_t)f->sh_text) return f->text_base + (int)st_value;
    if (st_shndx == (uint16_t)f->sh_data) return f->data_base + (int)st_value;
    if (st_shndx == (uint16_t)f->sh_bss)  return f->bss_base  + (int)st_value;

    /* 其它段：按 shndx 计算（.o 中 sh_addr 为 0，需用合并后基址） */
    return st_value;
}

static void resolve_symbols(void) {
    entry_addr = 0;

    /* 第一遍：收集 GLOBAL / WEAK 定义 */
    for (int fi = 0; fi < input_count; fi++) {
        InputFile *f = &inputs[fi];
        if (!f->syms_raw || f->sym_count <= 0) continue;

        for (int si = 0; si < f->sym_count; si++) {
            unsigned char *sym = f->syms_raw + si * 24;
            unsigned char info  = sym[4];         /* st_info */
            int bind  = (info >> 4) & 0xF;
            uint16_t shndx = r16(sym + 6);

            if (bind == 0) continue;      /* STB_LOCAL */
            if (shndx == 0) continue;     /* SHN_UNDEF — 未定义 */

            const char *name = sym_name(f, si);
            if (!name || !name[0]) continue;

            Symbol *s = find_or_add_sym(name);
            if (!s->defined) {
                s->defined = 1;
                s->value = sym_final_addr(f, sym);
                s->sym_info = info;
            }
        }
    }

    /* 第二遍：登记 UNDEF 符号引用 */
    for (int fi = 0; fi < input_count; fi++) {
        InputFile *f = &inputs[fi];
        if (!f->syms_raw) continue;

        for (int si = 1; si < f->sym_count; si++) {
            unsigned char *sym = f->syms_raw + si * 24;
            unsigned char info = sym[4];
            int bind = (info >> 4) & 0xF;
            if (bind == 0) continue;
            if (r16(sym + 6) != 0) continue;  /* 不是 UNDEF */

            const char *name = sym_name(f, si);
            if (!name || !name[0]) continue;
            find_or_add_sym(name);  /* 确保符号在全局表中存在 */
        }
    }

    /* 第三遍：检查未定义，找入口 */
    for (int i = 0; i < sym_count; i++) {
        if (!syms[i].defined) {
            /* _GLOBAL_OFFSET_TABLE_ 是特殊符号，忽略 */
            if (syms[i].name[0] == '_' && syms[i].name[1] == 'G')
                continue;
            error("undefined symbol", syms[i].name);
        }
        if (strcmp(syms[i].name, entry_sym_name) == 0)
            entry_addr = syms[i].value;
    }

    if (entry_addr == 0)
        error("entry point not found", entry_sym_name);
}

/* ═══════════════════════════════════════════════════════════════════
 *  重定位应用
 * ═══════════════════════════════════════════════════════════════════ */

static uint64_t find_sym_value(int sym_idx, InputFile *f) {
    if (sym_idx < 0 || sym_idx >= f->sym_count) return 0;
    unsigned char *sym = f->syms_raw + sym_idx * 24;
    unsigned char info = sym[4];
    int bind = (info >> 4) & 0xF;

    if (bind == 0) {
        /* LOCAL — 同一文件内 */
        return sym_final_addr(f, sym);
    } else {
        /* GLOBAL / WEAK — 查全局表 */
        const char *name = sym_name(f, sym_idx);
        for (int i = 0; i < sym_count; i++)
            if (strcmp(syms[i].name, name) == 0) return syms[i].value;
        error("undefined symbol in relocation", name);
        return 0;
    }
}

static void apply_one_rela(InputFile *f,
    unsigned char *rela_raw, int count,
    unsigned char *merged_buf,
    uint64_t section_base, int section_merge_off)
{
    for (int ri = 0; ri < count; ri++) {
        unsigned char *rel = rela_raw + ri * 24;
        uint64_t r_offset = r64(rel);
        uint64_t r_info   = r64(rel + 8);
        uint64_t r_addend = r64(rel + 16);  /* 当作 uint64 读取，后续符号扩展 */

        uint32_t sym_idx = (uint32_t)(r_info >> 32);
        uint32_t r_type  = (uint32_t)(r_info & 0xFFFFFFFF);

        uint64_t P = section_base + (int)r_offset;   /* 重定位位置 */
        uint64_t S = find_sym_value((int)sym_idx, f); /* 符号值 */
        int patch_off = section_merge_off + (int)r_offset;

        switch (r_type) {
        case 0:  /* R_X86_64_NONE */
            break;
        case 1: { /* R_X86_64_64 — S + A */
            uint64_t val = S + (int64_t)r_addend;
            w64(merged_buf + patch_off, val);
            break;
        }
        case 2:  /* R_X86_64_PC32 */
        case 4: { /* R_X86_64_PLT32 — 静态链接中同 PC32 */
            uint64_t val = S + (int64_t)r_addend - P;
            w32(merged_buf + patch_off, (uint32_t)(val & 0xFFFFFFFF));
            break;
        }
        case 10: { /* R_X86_64_32 S + A，零扩展 */
            uint64_t val = S + (int64_t)r_addend;
            w32(merged_buf + patch_off, (uint32_t)(val & 0xFFFFFFFF));
            break;
        }
        default:
            error("unsupported relocation type", NULL);
        }
    }
}

static void apply_relocations(void) {
    for (int fi = 0; fi < input_count; fi++) {
        InputFile *f = &inputs[fi];
        apply_one_rela(f, f->rela_text_raw, f->rela_text_count,
                       merged_text, f->text_base, f->text_merge_off);
        apply_one_rela(f, f->rela_data_raw, f->rela_data_count,
                       merged_data, f->data_base, f->data_merge_off);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  可执行文件输出
 * ═══════════════════════════════════════════════════════════════════ */

static void write_output(const char *path) {
    /* ── 布局：
     *   [0]         ELF header (64)
     *   [64]        Program header (56)
     *   [120..4095] 填充到页面边界
     *   [0x1000]    .text 数据
     *   [0x1000+ts] .data 数据
     *   [ += mds ]  .shstrtab
     *   [ += shstr_sz ] Section header table (5 × 64)
     *   PT_LOAD: p_offset=0x1000, p_vaddr=BASE_ADDR, p_filesz=ts+ds, p_memsz=ts+ds+bs
     *   → 节区表位于 PT_LOAD 范围之后，不干扰程序加载
     * ── */

    /* .shstrtab：各节区名称的 null-结尾顺序拼接 */
    static const char shstrtab_data[28] = {
        0, '.','t','e','x','t', 0,
        '.','d','a','t','a', 0,
        '.','b','s','s', 0,
        '.','s','h','s','t','r','t','a','b', 0
    };
    /* 偏移：0="" 1=".text" 7=".data" 13=".bss" 18=".shstrtab" */
    int shstrtab_size = sizeof(shstrtab_data);
    int shnum = 5;         /* SHN_UNDEF + .text + .data + .bss + .shstrtab */
    int shentsize = 64;

    int text_file_off = PAGE_SIZE;
    int data_file_off = text_file_off + merged_text_size;
    int shstrtab_off  = data_file_off + merged_data_size;
    int shdr_off      = shstrtab_off + shstrtab_size;
    int total_file_sz = shdr_off + shnum * shentsize;

    unsigned char *buf = (unsigned char *)tlibc_malloc(total_file_sz + 16);
    if (!buf) error("out of memory writing output", NULL);
    __memset(buf, 0, total_file_sz + 16);

    int p = 0;

    /* ── ELF header ── */
    buf[p++] = 0x7F; buf[p++] = 'E'; buf[p++] = 'L'; buf[p++] = 'F';
    buf[p++] = 2;     /* ELFCLASS64 */
    buf[p++] = 1;     /* ELFDATA2LSB */
    buf[p++] = 1;     /* EV_CURRENT */
    buf[p++] = 0;     /* ELFOSABI_NONE */
    for (int i = 0; i < 8; i++) buf[p++] = 0;  /* e_ident[8..15] = 0 */

    w16(buf + p, 2);    p += 2;  /* e_type = ET_EXEC */
    w16(buf + p, 62);   p += 2;  /* e_machine = EM_X86_64 */
    w32(buf + p, 1);    p += 4;  /* e_version */
    w64(buf + p, entry_addr);    p += 8;  /* e_entry */
    w64(buf + p, 64);            p += 8;  /* e_phoff */
    w64(buf + p, (uint64_t)shdr_off); p += 8;  /* e_shoff */
    w32(buf + p, 0);    p += 4;  /* e_flags */
    w16(buf + p, 64);   p += 2;  /* e_ehsize */
    w16(buf + p, 56);   p += 2;  /* e_phentsize */
    w16(buf + p, 1);    p += 2;  /* e_phnum */
    w16(buf + p, shentsize); p += 2;  /* e_shentsize */
    w16(buf + p, shnum); p += 2;  /* e_shnum */
    w16(buf + p, 4);    p += 2;  /* e_shstrndx (.shstrtab = index 4) */

    /* ── Program header: PT_LOAD ── */
    w32(buf + p, 1);   p += 4;  /* p_type = PT_LOAD */
    w32(buf + p, 7);   p += 4;  /* p_flags = PF_R|PF_W|PF_X */
    w64(buf + p, text_file_off); p += 8;
    w64(buf + p, BASE_ADDR); p += 8;
    w64(buf + p, BASE_ADDR); p += 8;
    w64(buf + p, merged_text_size + merged_data_size); p += 8;
    w64(buf + p, merged_text_size + merged_data_size + merged_bss_size); p += 8;
    w64(buf + p, PAGE_SIZE); p += 8;

    /* ── 写入段数据 ── */
    for (int i = 0; i < merged_text_size; i++)
        buf[text_file_off + i] = merged_text[i];
    for (int i = 0; i < merged_data_size; i++)
        buf[data_file_off + i] = merged_data[i];

    /* ── 写入 .shstrtab ── */
    for (int i = 0; i < shstrtab_size; i++)
        buf[shstrtab_off + i] = shstrtab_data[i];

    /* ── 写入 Section header table ── */
    p = shdr_off;

    /* index 0: SHN_UNDEF — 全零（memset 已清零，跳过） */
    p += shentsize;

    /* index 1: .text — SHF_ALLOC | SHF_EXECINSTR */
    w32(buf + p, 1);   p += 4;  /* sh_name */
    w32(buf + p, 1);   p += 4;  /* sh_type = SHT_PROGBITS */
    w64(buf + p, 0x6); p += 8;  /* sh_flags */
    w64(buf + p, text_addr); p += 8;
    w64(buf + p, text_file_off); p += 8;
    w64(buf + p, merged_text_size); p += 8;
    w32(buf + p, 0);   p += 4;
    w32(buf + p, 0);   p += 4;
    w64(buf + p, 16);  p += 8;  /* sh_addralign */
    w64(buf + p, 0);   p += 8;

    /* index 2: .data — SHF_ALLOC | SHF_WRITE */
    w32(buf + p, 7);   p += 4;  /* sh_name */
    w32(buf + p, 1);   p += 4;  /* sh_type = SHT_PROGBITS */
    w64(buf + p, 0x3); p += 8;  /* sh_flags */
    w64(buf + p, data_addr); p += 8;
    w64(buf + p, data_file_off); p += 8;
    w64(buf + p, merged_data_size); p += 8;
    w32(buf + p, 0);   p += 4;
    w32(buf + p, 0);   p += 4;
    w64(buf + p, 32);  p += 8;
    w64(buf + p, 0);   p += 8;

    /* index 3: .bss — SHF_ALLOC | SHF_WRITE | SHT_NOBITS */
    w32(buf + p, 13);  p += 4;  /* sh_name */
    w32(buf + p, 8);   p += 4;  /* sh_type = SHT_NOBITS */
    w64(buf + p, 0x3); p += 8;  /* sh_flags */
    w64(buf + p, bss_addr); p += 8;
    w64(buf + p, 0);   p += 8;  /* sh_offset = 0 (NOBITS) */
    w64(buf + p, merged_bss_size); p += 8;
    w32(buf + p, 0);   p += 4;
    w32(buf + p, 0);   p += 4;
    w64(buf + p, 32);  p += 8;
    w64(buf + p, 0);   p += 8;

    /* index 4: .shstrtab — SHT_STRTAB */
    w32(buf + p, 18);  p += 4;  /* sh_name */
    w32(buf + p, 3);   p += 4;  /* sh_type = SHT_STRTAB */
    w64(buf + p, 0);   p += 8;  /* sh_flags = 0 */
    w64(buf + p, 0);   p += 8;  /* sh_addr = 0 (不加载) */
    w64(buf + p, shstrtab_off); p += 8;
    w64(buf + p, shstrtab_size); p += 8;
    w32(buf + p, 0);   p += 4;
    w32(buf + p, 0);   p += 4;
    w64(buf + p, 1);   p += 8;  /* sh_addralign = 1 */
    w64(buf + p, 0);   p += 8;

    /* ── RWX 警告 ── */
    __write(2, "toyld: warning: all sections mapped RWX "
               "(single PT_LOAD segment)\n", 63);

    /* ── 写文件 ── */
    int fd = __openat(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) error("cannot create output file", path);

    int written = (int)__write(fd, buf, total_file_sz);
    __close(fd);
    tlibc_free(buf);

    if (written != total_file_sz)
        error("short write on output file", path);
}

/* ═══════════════════════════════════════════════════════════════════
 *  入口
 * ═══════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    output_path = "a.out";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            entry_sym_name = argv[++i];
        } else if (argv[i][0] == '-') {
            /* 忽略其他 ld 参数 */
        } else if (is_archive_name(argv[i])) {
            read_archive(argv[i]);
        } else {
            read_input(argv[i]);
        }
    }

    if (input_count == 0 && archive_count == 0)
        error("no input files", NULL);

    /* ── 注册直接输入的 UNDEF 符号 ── */
    for (int i = 0; i < input_count; i++)
        register_input_undefs(i);

    /* ── 从归档延迟加载需要的成员 ── */
    lazy_load_archives();

    /* 1) 段合并 */
    merge_sections();

    /* 2) 数据复制到合并缓冲区 */

    /* 1.5) 分配合并段缓冲区 */
    merged_text = (unsigned char *)tlibc_malloc(merged_text_size > 0 ? merged_text_size : 1);
    merged_data = (unsigned char *)tlibc_malloc(merged_data_size > 0 ? merged_data_size : 1);
    if (!merged_text || !merged_data) error("out of memory", NULL);
    __memset(merged_text, 0, merged_text_size);
    __memset(merged_data, 0, merged_data_size);

    merged_text_size = 0;
    merged_data_size = 0;
    merged_bss_size  = 0;
    for (int i = 0; i < input_count; i++) {
        InputFile *f = &inputs[i];
        for (int j = 0; j < f->text_size; j++)
            merged_text[merged_text_size + j] = f->text_data[j];
        merged_text_size += f->text_size;
        merged_text_size = align_up(merged_text_size, 16);

        for (int j = 0; j < f->data_size; j++)
            merged_data[merged_data_size + j] = f->data_data[j];
        merged_data_size += f->data_size;
        merged_data_size = align_up(merged_data_size, 32);

        merged_bss_size += f->bss_size;
        merged_bss_size = align_up(merged_bss_size, 32);
    }

    /* 3) 符号解析 */
    resolve_symbols();

    /* 4) 重定位 */
    apply_relocations();

    /* 5) 写输出 */
    write_output(output_path);

    return 0;
}
