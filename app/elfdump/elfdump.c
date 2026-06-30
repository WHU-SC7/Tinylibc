/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * elfdump — ELF64 目标文件 (.o) 解析查看器
 *
 * 机制：mmap 读取 ELF 文件 → 按文件偏移顺序遍历各组成部分
 *       → 结构化输出。无 FILE 流依赖，使用裸 fd + mmap。
 *
 * 系统调用：openat, mmap, munmap, close, write
 *
 * 用法：
 *   elfdump file.o           # 默认：文件结构概览 → 各部分内容
 *   elfdump -h file.o        # 仅 ELF 文件头
 *   elfdump -s file.o        # 仅符号表
 *   elfdump -x .text file.o  # 仅指定节区的十六进制内容
 *
 * 索引：
 *   main             入口：参数解析 → mmap → 建立索引 → 分派
 *     build_index    按文件偏移收集所有区域
 *     dump_overview  文件结构总览
 *     dump_region    按类型输出区域内容
 *       dump_elf_header      ELF 文件头
 *       dump_shdr_table      节区头表
 *       __dump_hex           裸数据 hex + ASCII
 *       dump_symbol_table    符号表格式化输出
 *       dump_strtab          字符串表列出字符串
 */

#include "tlibc_everything.h"

/* ─── ELF64 类型定义 ─── */

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef  int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef  int64_t Elf64_Sxword;

/* ELF 文件头 */
typedef struct {
    unsigned char e_ident[16];
    Elf64_Half    e_type;
    Elf64_Half    e_machine;
    Elf64_Word    e_version;
    Elf64_Addr    e_entry;
    Elf64_Off     e_phoff;
    Elf64_Off     e_shoff;
    Elf64_Word    e_flags;
    Elf64_Half    e_ehsize;
    Elf64_Half    e_phentsize;
    Elf64_Half    e_phnum;
    Elf64_Half    e_shentsize;
    Elf64_Half    e_shnum;
    Elf64_Half    e_shstrndx;
} Elf64_Ehdr;

/* 节区头 */
typedef struct {
    Elf64_Word   sh_name;
    Elf64_Word   sh_type;
    Elf64_Xword  sh_flags;
    Elf64_Addr   sh_addr;
    Elf64_Off    sh_offset;
    Elf64_Xword  sh_size;
    Elf64_Word   sh_link;
    Elf64_Word   sh_info;
    Elf64_Xword  sh_addralign;
    Elf64_Xword  sh_entsize;
} Elf64_Shdr;

/* 符号表条目 */
typedef struct {
    Elf64_Word    st_name;
    unsigned char st_info;
    unsigned char st_other;
    Elf64_Half    st_shndx;
    Elf64_Addr    st_value;
    Elf64_Xword   st_size;
} Elf64_Sym;

/* ─── ELF 常量 ─── */

#define EI_MAG0    0
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4
#define EI_DATA    5
#define EI_VERSION 6
#define EI_OSABI   7

#define ELFMAG0   0x7f
#define ELFMAG1   'E'
#define ELFMAG2   'L'
#define ELFMAG3   'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1

#define ET_NONE 0
#define ET_REL  1
#define ET_EXEC 2

#define EM_X86_64 62

#define SHT_NULL      0
#define SHT_PROGBITS  1
#define SHT_SYMTAB    2
#define SHT_STRTAB    3
#define SHT_RELA      4
#define SHT_NOBITS    8

#define SHF_WRITE      0x01
#define SHF_ALLOC      0x02
#define SHF_EXECINSTR  0x04

#define ELF64_ST_BIND(i)   ((i) >> 4)
#define ELF64_ST_TYPE(i)   ((i) & 0xf)

#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC   2

/* ─── 区域类型索引 ─── */

typedef enum {
    REGION_EHDR,        /* ELF 文件头 */
    REGION_SHDR_TABLE,  /* 节区头表 */
    REGION_SECTION,     /* 一般节区 */
} RegionKind;

/* 区域描述：记录了文件中的一块区域 */
typedef struct {
    RegionKind kind;
    long offset;
    long size;
    int sec_idx;        /* REGION_SECTION 时有效 */
} Region;

#define MAX_REGIONS 64

/* ─── 全局状态 ─── */

static const unsigned char *g_buf;    /* mmap 后的文件数据 */
static long g_size;                    /* 文件大小 */
static const Elf64_Ehdr *g_ehdr;      /* ELF 文件头 */
static const char *g_shstrtab;         /* 节区名串表 */
static long g_shstrtab_size;
static Region g_regions[MAX_REGIONS];
static int g_region_cnt;

/* ─── 辅助函数 ─── */

static const char *sec_name(int idx) {
    if (!g_shstrtab || idx < 0 || (long)idx >= g_shstrtab_size)
        return "???";
    return g_shstrtab + (unsigned)idx;
}

static int safe_offset(long off, long sz) {
    return off >= 0 && sz >= 0 && off + sz <= g_size;
}

/* ─── 字面量映射 ─── */

static const char *etype_str(int t) {
    switch (t) {
    case ET_NONE: return "NONE";
    case ET_REL:  return "REL (可重定位目标文件)";
    case ET_EXEC: return "EXEC (可执行文件)";
    default:      return "???";
    }
}

static const char *shtype_str(int t) {
    switch (t) {
    case SHT_NULL:     return "NULL";
    case SHT_PROGBITS: return "PROGBITS";
    case SHT_SYMTAB:   return "SYMTAB";
    case SHT_STRTAB:   return "STRTAB";
    case SHT_RELA:     return "RELA";
    case SHT_NOBITS:   return "NOBITS";
    default:           return "???";
    }
}

static const char *bind_str(int b) {
    switch (b) { case STB_LOCAL: return "LOCAL"; case STB_GLOBAL: return "GLOBAL"; default: return "???"; }
}

static const char *type_str(int t) {
    switch (t) { case STT_NOTYPE: return "NOTYPE"; case STT_OBJECT: return "OBJECT"; case STT_FUNC: return "FUNC"; default: return "???"; }
}

static void flags_str(Elf64_Xword f, char *out) {
    int i = 0;
    if (f & SHF_WRITE)     out[i++] = 'W';
    if (f & SHF_ALLOC)     out[i++] = 'A';
    if (f & SHF_EXECINSTR) out[i++] = 'X';
    out[i] = '\0';
}

/* ─── 建立区域索引 ─── */

static int region_compare(const void *a, const void *b) {
    const Region *ra = (const Region *)a;
    const Region *rb = (const Region *)b;
    if (ra->offset < rb->offset) return -1;
    if (ra->offset > rb->offset) return  1;
    return 0;
}

static void build_index(void) {
    g_region_cnt = 0;

    /* 1. ELF 文件头 */
    long ehdr_sz = g_ehdr->e_ehsize;
    if (ehdr_sz < (long)sizeof(Elf64_Ehdr)) ehdr_sz = sizeof(Elf64_Ehdr);
    g_regions[g_region_cnt].kind   = REGION_EHDR;
    g_regions[g_region_cnt].offset = 0;
    g_regions[g_region_cnt].size   = ehdr_sz;
    g_regions[g_region_cnt].sec_idx = -1;
    g_region_cnt++;

    /* 2. 节区头表 */
    if (g_ehdr->e_shnum > 0 && g_ehdr->e_shoff > 0) {
        long sz = g_ehdr->e_shnum * (long)sizeof(Elf64_Shdr);
        if (safe_offset(g_ehdr->e_shoff, sz)) {
            g_regions[g_region_cnt].kind   = REGION_SHDR_TABLE;
            g_regions[g_region_cnt].offset = g_ehdr->e_shoff;
            g_regions[g_region_cnt].size   = sz;
            g_regions[g_region_cnt].sec_idx = -1;
            g_region_cnt++;
        }
    }

    /* 3. 各节区（跳过 NULL 节区，按 sh_offset 排序后添加） */
    int i;
    for (i = 0; i < g_ehdr->e_shnum; i++) {
        long off = g_ehdr->e_shoff + i * (long)sizeof(Elf64_Shdr);
        if (!safe_offset(off, (long)sizeof(Elf64_Shdr)))
            continue;
        const Elf64_Shdr *sh = (const Elf64_Shdr *)(g_buf + off);
        if (sh->sh_type == SHT_NULL || sh->sh_type == SHT_NOBITS)
            continue;
        if (sh->sh_offset <= 0 || sh->sh_size <= 0)
            continue;
        if (!safe_offset(sh->sh_offset, sh->sh_size))
            continue;

        g_regions[g_region_cnt].kind    = REGION_SECTION;
        g_regions[g_region_cnt].offset  = sh->sh_offset;
        g_regions[g_region_cnt].size    = sh->sh_size;
        g_regions[g_region_cnt].sec_idx = i;
        g_region_cnt++;
    }

    /* 按文件偏移排序 */
    if (g_region_cnt > 1)
        /* 冒泡排序（最多几十个区域，够用） */
        for (i = 0; i < g_region_cnt - 1; i++) {
            int j;
            for (j = 0; j < g_region_cnt - 1 - i; j++) {
                if (g_regions[j].offset > g_regions[j + 1].offset) {
                    Region t = g_regions[j];
                    g_regions[j] = g_regions[j + 1];
                    g_regions[j + 1] = t;
                }
            }
        }
}

/* ─── 输出：文件结构概览 ─── */

static void dump_overview(void) {
    __printf("=== 文件结构概览 ===\n");
    __printf("偏移        结束        大小      内容\n");
    __printf("----------- ----------- --------- ------------------------------\n");

    int i;
    for (i = 0; i < g_region_cnt; i++) {
        Region *r = &g_regions[i];
        long end = r->offset + r->size - 1;

        __printf("0x%08lx 0x%08lx %-5ld (0x%lx) ",
                 (unsigned long)r->offset,
                 (unsigned long)end,
                 (long)r->size,
                 (unsigned long)r->size);

        switch (r->kind) {
        case REGION_EHDR:
            __printf("ELF 文件头\n");
            break;
        case REGION_SHDR_TABLE: {
            int n = g_ehdr->e_shnum;
            __printf("节区头表 (%d 个条目)\n", n);
            break;
        }
        case REGION_SECTION: {
            long off = g_ehdr->e_shoff +
                       r->sec_idx * (long)sizeof(Elf64_Shdr);
            const Elf64_Shdr *sh = (const Elf64_Shdr *)(g_buf + off);
            const char *name = sec_name(sh->sh_name);
            __printf("[%d] %s (%s)\n",
                     r->sec_idx,
                     name[0] ? name : "(null)",
                     shtype_str(sh->sh_type));

            /* 符号表额外显示条目数 */
            if (sh->sh_type == SHT_SYMTAB && sh->sh_entsize > 0)
                __printf("  └─ %d 个符号\n",
                         (int)(sh->sh_size / sh->sh_entsize));
            break;
        }
        }

        /* 标注区域间的间隙（填充字节） */
        if (i + 1 < g_region_cnt) {
            long next_off = g_regions[i + 1].offset;
            long cur_end  = r->offset + r->size;
            if (cur_end < next_off) {
                long gap = next_off - cur_end;
                __printf("                    %5ld (0x%lx) (填充/对齐)\n",
                         (long)gap, (unsigned long)gap);
            }
        }
    }

    /* 文件末尾 */
    if (g_region_cnt > 0) {
        Region *last = &g_regions[g_region_cnt - 1];
        long file_end = last->offset + last->size;
        if (file_end < g_size)
            __printf("                    %5ld (0x%lx) (文件末尾填充)\n",
                     (long)(g_size - file_end),
                     (unsigned long)(g_size - file_end));
    }
}

/* ─── 输出：ELF 文件头详情 ─── */

static void dump_elf_header(void) {
    const unsigned char *id = g_ehdr->e_ident;

    __printf("Magic:     %02x %02x %02x %02x",
             id[EI_MAG0], id[EI_MAG1], id[EI_MAG2], id[EI_MAG3]);
    if (id[EI_MAG0] == ELFMAG0 && id[EI_MAG1] == ELFMAG1 &&
        id[EI_MAG2] == ELFMAG2 && id[EI_MAG3] == ELFMAG3)
        __printf("  [OK]\n");
    else
        __printf("  [无效]\n");

    __printf("Class:     %s\n", id[EI_CLASS] == ELFCLASS64 ? "ELF64" : "???");
    __printf("Encoding:  %s\n", id[EI_DATA] == ELFDATA2LSB ? "小端序 (LSB)" : "???");
    __printf("Version:   %d\n", id[EI_VERSION]);
    __printf("OS/ABI:    UNIX - System V\n");
    __printf("Type:      %s\n", etype_str(g_ehdr->e_type));
    __printf("Machine:   x86_64\n");
    __printf("Entry:     0x%lx\n", (unsigned long)g_ehdr->e_entry);
    __printf("SH Offset: 0x%lx (%d 个 × %d 字节)\n",
             (unsigned long)g_ehdr->e_shoff,
             g_ehdr->e_shnum, g_ehdr->e_shentsize);
    __printf("SH StrTab: [%d]\n", g_ehdr->e_shstrndx);
}

/* ─── 输出：节区头表 ─── */

static void dump_shdr_table(void) {
    __printf("节区头表:\n");
    __printf(" 编号 名称                      类型       标志   偏移        大小        对齐    条目大小\n");
    __printf(" ---- ------------------------- ---------- ------ ----------- ----------- ------- --------\n");
    int i;
    for (i = 0; i < g_ehdr->e_shnum; i++) {
        long off = g_ehdr->e_shoff + i * (long)sizeof(Elf64_Shdr);
        if (!safe_offset(off, (long)sizeof(Elf64_Shdr))) {
            __printf(" [%3d] (越界)\n", i);
            continue;
        }
        const Elf64_Shdr *sh = (const Elf64_Shdr *)(g_buf + off);
        char flags[8];
        flags_str(sh->sh_flags, flags);
        const char *name = sec_name(sh->sh_name);

        __printf(" [%3d] %-24s %-10s %-6s %11ld %11ld %7ld %8ld\n",
                 i,
                 name[0] ? name : "(null)",
                 shtype_str(sh->sh_type), flags,
                 (unsigned long)sh->sh_offset,
                 (unsigned long)sh->sh_size,
                 (unsigned long)sh->sh_addralign,
                 (unsigned long)sh->sh_entsize);

        /* link/info 第二行 */
        if (sh->sh_link > 0 || sh->sh_info > 0)
            __printf("       └─ link=%d info=%d\n", sh->sh_link, sh->sh_info);
    }
}

/* ─── 输出：hex dump ─── */

static void __dump_hex(const unsigned char *data, long size) {
    long i;
    for (i = 0; i < size; i += 16) {
        __printf("  %08lx: ", (unsigned long)i);
        int j;
        for (j = 0; j < 16; j++) {
            if (i + j < size)
                __printf("%02x ", data[i + j]);
            else
                __printf("   ");
            if (j == 7) __printf(" ");
        }
        __printf(" ");
        for (j = 0; j < 16 && i + j < size; j++) {
            unsigned char c = data[i + j];
            __printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        __printf("\n");
    }
}

/* ─── 输出：符号表 ─── */

static void dump_symbol_table(const Elf64_Shdr *sh) {
    int n = g_ehdr->e_shnum;

    /* 找对应的 .strtab */
    const char *strtab = NULL;
    if (sh->sh_link > 0 && sh->sh_link < n) {
        long stroff = g_ehdr->e_shoff + sh->sh_link * (long)sizeof(Elf64_Shdr);
        if (safe_offset(stroff, (long)sizeof(Elf64_Shdr))) {
            const Elf64_Shdr *strsh = (const Elf64_Shdr *)(g_buf + stroff);
            if (strsh->sh_type == SHT_STRTAB &&
                safe_offset(strsh->sh_offset, strsh->sh_size))
                strtab = (const char *)(g_buf + strsh->sh_offset);
        }
    }

    int num = sh->sh_size / sizeof(Elf64_Sym);
    __printf("  符号表 (%d 个条目, link=[%d]):\n", num, sh->sh_link);
    __printf("  编号  值              大小  类型    绑定   节区  名称\n");
    __printf("  ----- --------------- ---- ------- ------ ----- --------------------\n");

    int k;
    for (k = 0; k < num; k++) {
        long symoff = sh->sh_offset + k * (long)sizeof(Elf64_Sym);
        if (!safe_offset(symoff, (long)sizeof(Elf64_Sym)))
            break;
        const Elf64_Sym *sym = (const Elf64_Sym *)(g_buf + symoff);
        const char *sname = (strtab && sym->st_name > 0)
                            ? strtab + sym->st_name : "";

        __printf("  [%3d]  %016lx %4ld %-7s %-6s %3d    %s\n",
                 k,
                 (unsigned long)sym->st_value,
                 (unsigned long)sym->st_size,
                 type_str(ELF64_ST_TYPE(sym->st_info)),
                 bind_str(ELF64_ST_BIND(sym->st_info)),
                 sym->st_shndx,
                 sname[0] ? sname : "(null)");
    }
}

/* ─── 输出：字符串表 ─── */

static void dump_strtab_content(const Elf64_Shdr *sh) {
    if (!safe_offset(sh->sh_offset, sh->sh_size))
        return;

    const unsigned char *data = g_buf + sh->sh_offset;
    long sz = sh->sh_size;

    __printf("  字符串 (%ld 字节):\n", sz);
    long pos = 1;
    while (pos < sz) {
        if (data[pos] == '\0') { pos++; continue; }
        __printf("    [%04ld] %s\n", pos, (const char *)(data + pos));
        pos += strlen((const char *)(data + pos)) + 1;
    }
}

/* ─── 输出：重定位内容 ─── */

static void dump_rela_content(const Elf64_Shdr *sh) {
    int n = g_ehdr->e_shnum;

    /* 找对应的符号表 → 再找符号表的 strtab */
    const char *strtab = NULL;
    if (sh->sh_link > 0 && sh->sh_link < n) {
        long stroff = g_ehdr->e_shoff + sh->sh_link * (long)sizeof(Elf64_Shdr);
        if (safe_offset(stroff, (long)sizeof(Elf64_Shdr))) {
            const Elf64_Shdr *symsh = (const Elf64_Shdr *)(g_buf + stroff);
            if (symsh->sh_type == SHT_SYMTAB &&
                safe_offset(symsh->sh_offset, symsh->sh_size)) {
                int strlink = symsh->sh_link;
                if (strlink > 0 && strlink < n) {
                    long stoff2 = g_ehdr->e_shoff + strlink * (long)sizeof(Elf64_Shdr);
                    if (safe_offset(stoff2, (long)sizeof(Elf64_Shdr))) {
                        const Elf64_Shdr *s2 = (const Elf64_Shdr *)(g_buf + stoff2);
                        if (s2->sh_type == SHT_STRTAB &&
                            safe_offset(s2->sh_offset, s2->sh_size))
                            strtab = (const char *)(g_buf + s2->sh_offset);
                    }
                }
            }
        }
    }

    int num = sh->sh_size / 24;
    __printf("  重定位 (%d 个条目, link=[%d]):\n", num, sh->sh_link);
    __printf("    偏移              信息              附加          符号\n");
    __printf("    ----------------- ---------------- --------------- -------\n");

    int k;
    for (k = 0; k < num; k++) {
        long roff = sh->sh_offset + k * 24;
        if (!safe_offset(roff, 24))
            break;
        const unsigned char *r = g_buf + roff;
        Elf64_Addr r_off   = *(const Elf64_Addr *)(r + 0);
        Elf64_Xword r_info = *(const Elf64_Xword *)(r + 8);
        Elf64_Sxword r_add = *(const Elf64_Sxword *)(r + 16);

        int sym_idx = r_info >> 32;
        const char *sym_name = "";

        if (strtab && sh->sh_link > 0 && sh->sh_link < n) {
            long symoff2 = g_ehdr->e_shoff + sh->sh_link * (long)sizeof(Elf64_Shdr);
            if (safe_offset(symoff2, (long)sizeof(Elf64_Shdr))) {
                const Elf64_Shdr *symsh = (const Elf64_Shdr *)(g_buf + symoff2);
                if (symsh->sh_type == SHT_SYMTAB && sym_idx >= 0) {
                    long eoff = symsh->sh_offset + sym_idx * (long)sizeof(Elf64_Sym);
                    if (safe_offset(eoff, (long)sizeof(Elf64_Sym))) {
                        const Elf64_Sym *esym = (const Elf64_Sym *)(g_buf + eoff);
                        if (esym->st_name > 0 && strtab)
                            sym_name = strtab + esym->st_name;
                    }
                }
            }
        }

        __printf("    %016lx %016lx %016lx %s+%ld\n",
                 (unsigned long)r_off, (unsigned long)r_info,
                 (unsigned long)r_add,
                 sym_name[0] ? sym_name : "(?)", (long)r_add);
    }
}

/* ─── 输出：按区域显示内容（默认路径） ─── */

static void dump_region_content(int idx) {
    Region *r = &g_regions[idx];
    const unsigned char *data = g_buf + r->offset;

    switch (r->kind) {
    case REGION_EHDR:
        __printf("\n── ELF 文件头 ──────────────────────────────\n");
        dump_elf_header();
        break;

    case REGION_SHDR_TABLE:
        __printf("\n── 节区头表 ────────────────────────────────\n");
        dump_shdr_table();
        break;

    case REGION_SECTION: {
        long shoff = g_ehdr->e_shoff +
                     r->sec_idx * (long)sizeof(Elf64_Shdr);
        if (!safe_offset(shoff, (long)sizeof(Elf64_Shdr)))
            break;
        const Elf64_Shdr *sh = (const Elf64_Shdr *)(g_buf + shoff);

        const char *name = sec_name(sh->sh_name);
        if (!name[0]) name = "(null)";

        __printf("\n── [%d] %s (%s, %ld 字节) ─────────────────\n",
                 r->sec_idx, name, shtype_str(sh->sh_type),
                 (long)sh->sh_size);

        switch (sh->sh_type) {
        case SHT_SYMTAB:
            dump_symbol_table(sh);
            break;
        case SHT_STRTAB:
            dump_strtab_content(sh);
            break;
        case SHT_RELA:
            dump_rela_content(sh);
            break;
        default:
            /* PROGBITS / 其他：hex dump */
            __dump_hex(data, r->size);
            break;
        }
        break;
    }
    }
}

/* ─── 主入口 ─── */

int main(int argc, char *argv[]) {
    int mode_header = 0;
    int mode_syms   = 0;
    int mode_hex    = 0;
    const char *hex_target = NULL;
    const char *file = NULL;

    int i;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'h' && argv[i][2] == '\0')
                mode_header = 1;
            else if (argv[i][1] == 's' && argv[i][2] == '\0')
                mode_syms = 1;
            else if (argv[i][1] == 'x' && argv[i][2] == '\0') {
                mode_hex = 1;
                if (i + 1 < argc && argv[i+1][0] != '-')
                    hex_target = argv[++i];
            } else {
                __printf("用法: elfdump [-h] [-s] [-x <节区名>] <file.o>\n");
                __printf("  (无选项)  文件结构概览 + 各部分内容\n");
                __printf("  -h        仅 ELF 文件头\n");
                __printf("  -s        仅符号表\n");
                __printf("  -x <名>   仅指定节区的 hex 内容\n");
                return 1;
            }
        } else {
            file = argv[i];
        }
    }

    if (!file) {
        __printf("用法: elfdump [-h] [-s] [-x <节区名>] <file.o>\n");
        return 1;
    }

    /* ── mmap ── */
    int fd = __openat(AT_FDCWD, file, O_RDONLY, 0);
    if (fd < 0) {
        __printf("elfdump: 无法打开 '%s'\n", file);
        return 1;
    }

    g_size = __lseek(fd, 0, SEEK_END);
    if (g_size < (long)sizeof(Elf64_Ehdr)) {
        __printf("elfdump: 文件过小\n");
        __close(fd);
        return 1;
    }

    g_buf = (const unsigned char *)__mmap(NULL, g_size, PROT_READ,
                                          MAP_PRIVATE, fd, 0);
    __close(fd);

    if (g_buf == MAP_FAILED) {
        __printf("elfdump: mmap 失败\n");
        return 1;
    }

    g_ehdr = (const Elf64_Ehdr *)g_buf;

    /* 魔数校验 */
    if (g_ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        g_ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        g_ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        g_ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        __printf("elfdump: 不是 ELF 文件\n");
        __munmap((void *)g_buf, g_size);
        return 1;
    }

    /* 解析 shstrtab */
    if (g_ehdr->e_shstrndx > 0 && g_ehdr->e_shstrndx < g_ehdr->e_shnum) {
        long soff = g_ehdr->e_shoff +
                    g_ehdr->e_shstrndx * (long)sizeof(Elf64_Shdr);
        if (safe_offset(soff, (long)sizeof(Elf64_Shdr))) {
            const Elf64_Shdr *sh = (const Elf64_Shdr *)(g_buf + soff);
            if (sh->sh_type == SHT_STRTAB &&
                safe_offset(sh->sh_offset, sh->sh_size)) {
                g_shstrtab = (const char *)(g_buf + sh->sh_offset);
                g_shstrtab_size = sh->sh_size;
            }
        }
    }

    /* ── 分派 ── */

    if (mode_header) {
        dump_elf_header();
    } else if (mode_syms) {
        /* 找符号表并输出 */
        int j;
        for (j = 0; j < g_ehdr->e_shnum; j++) {
            long soff = g_ehdr->e_shoff + j * (long)sizeof(Elf64_Shdr);
            if (!safe_offset(soff, (long)sizeof(Elf64_Shdr)))
                continue;
            const Elf64_Shdr *sh = (const Elf64_Shdr *)(g_buf + soff);
            if (sh->sh_type == SHT_SYMTAB) {
                dump_symbol_table(sh);
                break;
            }
        }
    } else if (mode_hex) {
        /* 找指定节区输出 hex */
        int j;
        for (j = 0; j < g_ehdr->e_shnum; j++) {
            long soff = g_ehdr->e_shoff + j * (long)sizeof(Elf64_Shdr);
            if (!safe_offset(soff, (long)sizeof(Elf64_Shdr)))
                continue;
            const Elf64_Shdr *sh = (const Elf64_Shdr *)(g_buf + soff);
            if (strcmp(sec_name(sh->sh_name), hex_target) == 0) {
                if (sh->sh_type != SHT_NOBITS && sh->sh_size > 0 &&
                    safe_offset(sh->sh_offset, sh->sh_size)) {
                    __printf("=== [%d] %s 节区 (%ld 字节) ===\n",
                             j, hex_target, (long)sh->sh_size);
                    __dump_hex(g_buf + sh->sh_offset, sh->sh_size);
                } else {
                    __printf("(空节区)\n");
                }
                break;
            }
        }
    } else {
        /* 默认：建立索引 → 概览 → 逐一展示 */
        build_index();
        dump_overview();

        for (i = 0; i < g_region_cnt; i++)
            dump_region_content(i);
    }

    __munmap((void *)g_buf, g_size);
    __printf("\n");
    return 0;
}
