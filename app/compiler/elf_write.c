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
    int num_sections = 6;

    /* ── 构建 .shstrtab ── */
    unsigned char shstrtab_buf[256];
    const char *sec_names[] = {".text", ".rela.text", ".symtab", ".strtab", ".shstrtab"};
    int shstrtab_sz = build_shstrtab(shstrtab_buf, sec_names, 5);

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
    int rela_ofs = (text_ofs + text_sz + 7) & ~7;
    int rela_sz  = rel_count * 24;
    int sym_ofs  = (rela_ofs + rela_sz + 7) & ~7;
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
    b[p++] = 5; b[p++] = 0;

    /* ── Section header table (占位，后面填充) ── */
    int shdr_start = p;
    for (ei = 0; ei < num_sections * 64; ei++) b[p++] = 0;

    /* ── .text data ── */
    while (p < text_ofs) b[p++] = 0;
    for (ei = 0; ei < code_size; ei++) b[p++] = code_buf[ei];

    /* ── 在写重定位前，重映射符号索引 ── */
    /* ELF 要求局部符号在前、全局在后，但代码中 relocation 的 r_info
     * 使用 syms[] 序号+1 计算。此处为每个符号分配实际 ELF 索引并修复。 */
    {
        int elf_idx = 1;
        for (i = 0; i < sym_count; i++)
            if (!syms[i].is_global) syms[i].sym_idx = elf_idx++;
        for (i = 0; i < sym_count; i++)
            if (syms[i].is_global) syms[i].sym_idx = elf_idx++;
        for (int ri = 0; ri < rel_count; ri++) {
            unsigned long r_sym = ELF64_R_SYM(rels[ri].r_info);
            if (r_sym > 0) {
                int cgen_idx = (int)(r_sym - 1);
                if (cgen_idx >= 0 && cgen_idx < sym_count) {
                    unsigned int r_type = ELF64_R_TYPE(rels[ri].r_info);
                    rels[ri].r_info = ELF64_R_INFO(syms[cgen_idx].sym_idx, r_type);
                }
            }
        }
    }

    /* ── .rela.text data ── */
    while (p < rela_ofs) b[p++] = 0;
    for (ei = 0; ei < rel_count; ei++) {
        int ro = rels[ei].r_offset;
        b[p++] = (ro)&0xFF; b[p++] = (ro>>8)&0xFF;
        b[p++] = (ro>>16)&0xFF; b[p++] = (ro>>24)&0xFF;
        for (int z=0;z<4;z++) b[p++] = 0;
        long ri = rels[ei].r_info;
        b[p++] = (ri)&0xFF; b[p++] = (ri>>8)&0xFF;
        b[p++] = (ri>>16)&0xFF; b[p++] = (ri>>24)&0xFF;
        b[p++] = (ri>>32)&0xFF; b[p++] = (ri>>40)&0xFF;
        b[p++] = (ri>>48)&0xFF; b[p++] = (ri>>56)&0xFF;
        long ra = rels[ei].r_addend;
        b[p++] = (ra)&0xFF; b[p++] = (ra>>8)&0xFF;
        b[p++] = (ra>>16)&0xFF; b[p++] = (ra>>24)&0xFF;
        b[p++] = (ra>>32)&0xFF; b[p++] = (ra>>40)&0xFF;
        b[p++] = (ra>>48)&0xFF; b[p++] = (ra>>56)&0xFF;
    }

    /* ── .symtab data ── */
    while (p < sym_ofs) b[p++] = 0;

    /* 统计局部符号数（含 null 条目），ELF 规范要求局部在前、全局在后 */
    int local_cnt = 1;  /* null 条目 */
    for (i = 0; i < sym_count; i++)
        if (!syms[i].is_global) local_cnt++;
    int first_global = local_cnt;  /* sh_info 值 */

    /* null symbol */
    for (ei = 0; ei < 24; ei++) b[p++] = 0;

    /* 先写局部符号 */
    for (i = 0; i < sym_count; i++) {
        if (syms[i].is_global) continue;
        int idx = sym_str_idx[i];
        b[p++] = (idx) & 0xFF; b[p++] = (idx >> 8) & 0xFF;
        b[p++] = (idx >> 16) & 0xFF; b[p++] = (idx >> 24) & 0xFF;
        b[p++] = ELF64_ST_INFO(0, syms[i].is_func ? 2 : 0);
        b[p++] = 0;
        /* 定义（size>0）驻留 .text = 1，外部引用为 SHN_UNDEF = 0 */
        b[p++] = (syms[i].size > 0) ? 1 : 0; b[p++] = 0;
        int v = syms[i].offset;
        b[p++] = (v) & 0xFF; b[p++] = (v >> 8) & 0xFF;
        b[p++] = (v >> 16) & 0xFF; b[p++] = (v >> 24) & 0xFF;
        b[p++] = 0; b[p++] = 0; b[p++] = 0; b[p++] = 0;
        int sz = syms[i].size;
        b[p++] = (sz) & 0xFF; b[p++] = (sz >> 8) & 0xFF;
        b[p++] = (sz >> 16) & 0xFF; b[p++] = (sz >> 24) & 0xFF;
        b[p++] = 0; b[p++] = 0; b[p++] = 0; b[p++] = 0;
    }

    /* 再写全局符号 */
    for (i = 0; i < sym_count; i++) {
        if (!syms[i].is_global) continue;
        int idx = sym_str_idx[i];
        b[p++] = (idx) & 0xFF; b[p++] = (idx >> 8) & 0xFF;
        b[p++] = (idx >> 16) & 0xFF; b[p++] = (idx >> 24) & 0xFF;
        b[p++] = ELF64_ST_INFO(1, syms[i].is_func ? 2 : 0);
        b[p++] = 0;
        b[p++] = (syms[i].size > 0) ? 1 : 0; b[p++] = 0;
        int v = syms[i].offset;
        b[p++] = (v) & 0xFF; b[p++] = (v >> 8) & 0xFF;
        b[p++] = (v >> 16) & 0xFF; b[p++] = (v >> 24) & 0xFF;
        b[p++] = 0; b[p++] = 0; b[p++] = 0; b[p++] = 0;
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
    /* sh_name 索引：1=.text, 7=.rela.text, 18=.symtab, 26=.strtab, 34=.shstrtab */

    /* 节区 1: .text (offset +64) */
    b[shdr_start+64]=1; b[shdr_start+68]=1; b[shdr_start+72]=6;
    {
        int off = shdr_start + 64;
        /* sh_offset at +24 */
        b[off+24]=(text_ofs)&0xFF; b[off+25]=(text_ofs>>8)&0xFF;
        b[off+26]=(text_ofs>>16)&0xFF; b[off+27]=(text_ofs>>24)&0xFF;
        /* sh_size at +32 */
        b[off+32]=(text_sz)&0xFF; b[off+33]=(text_sz>>8)&0xFF;
        b[off+34]=(text_sz>>16)&0xFF; b[off+35]=(text_sz>>24)&0xFF;
        b[off+48]=16;
    }

    /* 节区 2: .rela.text (offset +128) */
    b[shdr_start+128]=7; b[shdr_start+132]=4;  /* sh_name, SHT_RELA */
    {
        int off = shdr_start + 128;
        b[off+24]=(rela_ofs)&0xFF; b[off+25]=(rela_ofs>>8)&0xFF;
        b[off+26]=(rela_ofs>>16)&0xFF; b[off+27]=(rela_ofs>>24)&0xFF;
        b[off+32]=(rela_sz)&0xFF; b[off+33]=(rela_sz>>8)&0xFF;
        b[off+34]=(rela_sz>>16)&0xFF; b[off+35]=(rela_sz>>24)&0xFF;
        b[off+40]=3;    /* sh_link = .symtab */
        b[off+44]=1;    /* sh_info = .text */
        b[off+48]=8;
        b[off+56]=24;   /* sh_entsize = Elf64_Rela size */
    }

    /* 节区 3: .symtab (offset +192) */
    b[shdr_start+192]=18; b[shdr_start+196]=2;
    {
        int off = shdr_start + 192;
        b[off+24]=(sym_ofs)&0xFF; b[off+25]=(sym_ofs>>8)&0xFF;
        b[off+26]=(sym_ofs>>16)&0xFF; b[off+27]=(sym_ofs>>24)&0xFF;
        b[off+32]=(sym_sz)&0xFF; b[off+33]=(sym_sz>>8)&0xFF;
        b[off+34]=(sym_sz>>16)&0xFF; b[off+35]=(sym_sz>>24)&0xFF;
        b[off+40]=4;    /* sh_link = .strtab */
        b[off+44]=first_global;
        b[off+48]=8;
        b[off+56]=24;
    }

    /* 节区 4: .strtab (offset +256) */
    b[shdr_start+256]=26; b[shdr_start+260]=3;
    {
        int off = shdr_start + 256;
        b[off+24]=(str_ofs)&0xFF; b[off+25]=(str_ofs>>8)&0xFF;
        b[off+26]=(str_ofs>>16)&0xFF; b[off+27]=(str_ofs>>24)&0xFF;
        b[off+32]=(strtab_sz)&0xFF; b[off+33]=(strtab_sz>>8)&0xFF;
        b[off+34]=(strtab_sz>>16)&0xFF; b[off+35]=(strtab_sz>>24)&0xFF;
        b[off+48]=1;
    }

    /* 节区 5: .shstrtab (offset +320) */
    b[shdr_start+320]=34; b[shdr_start+324]=3;
    {
        int off = shdr_start + 320;
        b[off+24]=(shstr_ofs)&0xFF; b[off+25]=(shstr_ofs>>8)&0xFF;
        b[off+26]=(shstr_ofs>>16)&0xFF; b[off+27]=(shstr_ofs>>24)&0xFF;
        b[off+32]=(shstrtab_sz)&0xFF; b[off+33]=(shstrtab_sz>>8)&0xFF;
        b[off+34]=(shstrtab_sz>>16)&0xFF; b[off+35]=(shstrtab_sz>>24)&0xFF;
        b[off+48]=1;
    }

    /* ── 写入文件 ── */
    int fd = __openat(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    int written = __write(fd, buf, p);
    __close(fd);
    return (written == p) ? 0 : -1;
}
