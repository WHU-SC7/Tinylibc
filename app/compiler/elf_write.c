/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * elf_write.c — ELF64 可重定位目标文件写入器
 *
 * 机制：接收 cgen 产生的代码缓冲区和符号表，生成 ELF64 .o 文件。
 *       使用静态缓冲区逐步构建并一次性写入。
 */

#include "tcc.h"

/* ─── 构建 .shstrtab ─── */

static int build_shstrtab(unsigned char *buf, const char *names[], int n) {
    int len = 0;
    buf[len++] = '\0';
    int i;
    for (i = 0; i < n; i++) {
        const char *s = names[i];
        while (*s)
            buf[len++] = *s++;
        buf[len++] = '\0';
    }
    return len;
}

/* ─── 主入口 ─── */

int elf_write_object(const char *path) {
    /* ── 节区总数 ── */
    int num_sections = 5;

    /* ── 构建 .shstrtab ── */
    unsigned char shstrtab_buf[256];
    const char *sec_names[] = {".text", ".symtab", ".strtab", ".shstrtab"};
    int shstrtab_sz = build_shstrtab(shstrtab_buf, sec_names, 4);

    /* ── 构建 .strtab ── */
    static unsigned char strtab_buf[4096];
    int strtab_sz = 0;
    strtab_buf[strtab_sz++] = '\0';
    int sym_str_idx[4096];
    int i;
    for (i = 0; i < sym_count; i++) {
        sym_str_idx[i] = strtab_sz;
        const char *s = syms[i].name;
        while (*s)
            strtab_buf[strtab_sz++] = *s++;
        strtab_buf[strtab_sz++] = '\0';
    }

    /* ── 计算文件偏移 ── */
    int shdr_ofs = 64;
    int text_ofs = (shdr_ofs + num_sections * 64 + 15) & ~15;
    int text_sz  = code_size;
    int sym_ofs  = (text_ofs + text_sz + 7) & ~7;
    int sym_sz   = (sym_count + 1) * 24;
    int str_ofs  = sym_ofs + sym_sz;
    int shstr_ofs = str_ofs + strtab_sz;

    /* ── 用静态缓冲区一次性构建完整文件 ── */
    static unsigned char buf[262144];
    int p = 0;
    unsigned char *b = buf;

    /* ELF header */
    b[p++] = 0x7f; b[p++] = 'E'; b[p++] = 'L'; b[p++] = 'F'; /* magic */
    b[p++] = 2;     /* ELFCLASS64 */
    b[p++] = 1;     /* ELFDATA2LSB */
    b[p++] = 1;     /* EV_CURRENT */
    b[p++] = 0;     /* ELFOSABI_NONE */
    b[p++] = 0;     /* padding */
    int ei;
    for (ei = 9; ei < 16; ei++) b[p++] = 0; /* padding */

    /* e_type, e_machine, e_version */
    b[p++] = 1; b[p++] = 0;       /* ET_REL */
    b[p++] = 62; b[p++] = 0;      /* EM_X86_64 */
    b[p++] = 1; b[p++] = 0; b[p++] = 0; b[p++] = 0; /* EV_CURRENT */

    /* e_entry (8), e_phoff (8) */
    for (ei = 0; ei < 16; ei++) b[p++] = 0;

    /* e_shoff (8) */
    b[p++] = (shdr_ofs) & 0xFF; b[p++] = (shdr_ofs >> 8) & 0xFF;
    b[p++] = (shdr_ofs >> 16) & 0xFF; b[p++] = (shdr_ofs >> 24) & 0xFF;
    for (ei = 0; ei < 4; ei++) b[p++] = 0;

    /* e_flags (4) */
    for (ei = 0; ei < 4; ei++) b[p++] = 0;

    /* e_ehsize (2), e_phentsize (2) */
    b[p++] = 64; b[p++] = 0;    /* e_ehsize = 64 */
    b[p++] = 0; b[p++] = 0;    /* e_phentsize = 0 */

    /* e_phnum (2) */
    b[p++] = 0; b[p++] = 0;

    /* e_shentsize (2) */
    b[p++] = 64; b[p++] = 0;

    /* e_shnum (2) */
    b[p++] = num_sections; b[p++] = 0;

    /* e_shstrndx (2) */
    b[p++] = 4; b[p++] = 0;

    /* ── Section header table (占位，后面填充) ── */
    int shdr_start = p;
    for (ei = 0; ei < num_sections * 64; ei++) b[p++] = 0;

    /* ── .text data ── */
    while (p < text_ofs) b[p++] = 0;
    for (ei = 0; ei < code_size; ei++) b[p++] = code_buf[ei];

    /* ── .symtab data ── */
    while (p < sym_ofs) b[p++] = 0;

    /* null symbol */
    for (ei = 0; ei < 24; ei++) b[p++] = 0;

    for (i = 0; i < sym_count; i++) {
        int idx = sym_str_idx[i];
        b[p++] = (idx) & 0xFF;
        b[p++] = (idx >> 8) & 0xFF;
        b[p++] = (idx >> 16) & 0xFF;
        b[p++] = (idx >> 24) & 0xFF;
        b[p++] = ELF64_ST_INFO(syms[i].is_global ? 1 : 0,
                                syms[i].is_func ? 2 : 0);
        b[p++] = 0;  /* st_other */
        b[p++] = syms[i].is_func ? 1 : 0;  /* st_shndx */
        b[p++] = 0;
        /* st_value (8) */
        int v = syms[i].offset;
        b[p++] = (v) & 0xFF; b[p++] = (v >> 8) & 0xFF;
        b[p++] = (v >> 16) & 0xFF; b[p++] = (v >> 24) & 0xFF;
        b[p++] = 0; b[p++] = 0; b[p++] = 0; b[p++] = 0;
        /* st_size (8) */
        int sz = syms[i].size;
        b[p++] = (sz) & 0xFF; b[p++] = (sz >> 8) & 0xFF;
        b[p++] = (sz >> 16) & 0xFF; b[p++] = (sz >> 24) & 0xFF;
        b[p++] = 0; b[p++] = 0; b[p++] = 0; b[p++] = 0;
    }

    /* ── .strtab data ── */
    for (ei = 0; ei < strtab_sz; ei++) b[p++] = strtab_buf[ei];

    /* ── .shstrtab data ── */
    for (ei = 0; ei < shstrtab_sz; ei++) b[p++] = shstrtab_buf[ei];

    /* ── 回填节区头表 ── */
    /* 这是按字节布局手工编码的节区头，每个 64 字节 */

    /* 节区 0: NULL */
    /* 全 0，b[shdr_start..shdr_start+63] 已经为 0 */

    /* 节区 1: .text */
    {
        int off = shdr_start + 64;
        /* sh_name(4) + sh_type(4) + sh_flags(8) */
        b[off+0]=1; b[off+4]=1; b[off+8]=6; b[off+10]=0;
        /* sh_addr(8) = 0 */
        /* sh_offset(8) at +24 */
        b[off+24]=(text_ofs)&0xFF; b[off+25]=(text_ofs>>8)&0xFF;
        b[off+26]=(text_ofs>>16)&0xFF; b[off+27]=(text_ofs>>24)&0xFF;
        /* sh_size(8) at +32 */
        b[off+32]=(text_sz)&0xFF; b[off+33]=(text_sz>>8)&0xFF;
        b[off+34]=(text_sz>>16)&0xFF; b[off+35]=(text_sz>>24)&0xFF;
        /* sh_link(4)+sh_info(4) at +40 — zero */
        /* sh_addralign(8) at +48 */
        b[off+48]=16;
        /* sh_entsize(8) at +56 — zero */
    }

    /* 节区 2: .symtab */
    {
        int off = shdr_start + 128;
        b[off+0]=7; b[off+4]=2;
        /* sh_offset(8) at +24 */
        b[off+24]=(sym_ofs)&0xFF; b[off+25]=(sym_ofs>>8)&0xFF;
        b[off+26]=(sym_ofs>>16)&0xFF; b[off+27]=(sym_ofs>>24)&0xFF;
        /* sh_size(8) at +32 */
        b[off+32]=(sym_sz)&0xFF; b[off+33]=(sym_sz>>8)&0xFF;
        b[off+34]=(sym_sz>>16)&0xFF; b[off+35]=(sym_sz>>24)&0xFF;
        /* sh_link(4) at +40 */
        b[off+40]=3;
        /* sh_info(4) at +44 */
        b[off+44]=1;
        /* sh_addralign(8) at +48 */
        b[off+48]=8;
        /* sh_entsize(8) at +56 */
        b[off+56]=24;
    }

    /* 节区 3: .strtab */
    {
        int off = shdr_start + 192;
        b[off+0]=15; b[off+4]=3;
        /* sh_offset(8) at +24 */
        b[off+24]=(str_ofs)&0xFF; b[off+25]=(str_ofs>>8)&0xFF;
        b[off+26]=(str_ofs>>16)&0xFF; b[off+27]=(str_ofs>>24)&0xFF;
        /* sh_size(8) at +32 */
        b[off+32]=(strtab_sz)&0xFF; b[off+33]=(strtab_sz>>8)&0xFF;
        b[off+34]=(strtab_sz>>16)&0xFF; b[off+35]=(strtab_sz>>24)&0xFF;
        /* sh_addralign(8) at +48 */
        b[off+48]=1;
    }

    /* 节区 4: .shstrtab */
    {
        int off = shdr_start + 256;
        b[off+0]=23; b[off+4]=3;
        /* sh_offset(8) at +24 */
        b[off+24]=(shstr_ofs)&0xFF; b[off+25]=(shstr_ofs>>8)&0xFF;
        b[off+26]=(shstr_ofs>>16)&0xFF; b[off+27]=(shstr_ofs>>24)&0xFF;
        /* sh_size(8) at +32 */
        b[off+32]=(shstrtab_sz)&0xFF; b[off+33]=(shstrtab_sz>>8)&0xFF;
        b[off+34]=(shstrtab_sz>>16)&0xFF; b[off+35]=(shstrtab_sz>>24)&0xFF;
        /* sh_addralign(8) at +48 */
        b[off+48]=1;
    }

    /* ── 写入文件 ── */
    int fd = __openat(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    int written = __write(fd, buf, p);
    __close(fd);
    return (written == p) ? 0 : -1;
}
