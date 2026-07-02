/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * elf_write.c — ELF64 可重定位目标文件写入器（tcc/tas 共享）
 *
 * 机制：接收代码缓冲区和符号表，生成 ELF64 .o 文件。
 *       使用静态缓冲区逐步构建并一次性写入。
 */

#include "elf_write.h"

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

int elf_bss_size;

int elf_write_object(const char *path) {
    int num_sections = 7;

    /* ── 构建 .shstrtab ── */
    unsigned char shstrtab_buf[256];
    const char *sec_names[] = {".text", ".rela.text", ".bss", ".symtab", ".strtab", ".shstrtab"};
    int shstrtab_sz = build_shstrtab(shstrtab_buf, sec_names, 6);

    /* ── 构建 .strtab ── */
    #define ELF_STRTAB_SIZE 16384
    #define ELF_MAX_SYM_STR 4096
    static unsigned char strtab_buf[ELF_STRTAB_SIZE];
    int strtab_sz = 0;
    strtab_buf[strtab_sz++] = '\0';
    int sym_str_idx[ELF_MAX_SYM_STR];
    if (elf_sym_count > ELF_MAX_SYM_STR) {
        __write(2, "elf_write: too many symbols\n", 28);
        __exit(1);
    }
    int i;
    for (i = 0; i < elf_sym_count; i++) {
        sym_str_idx[i] = strtab_sz;
        const char *s = elf_syms[i].name;
        if (!s) s = "";
        int nlen = 0; while (s[nlen]) nlen++;
        if (strtab_sz + nlen + 1 > ELF_STRTAB_SIZE) {
            __write(2, "elf_write: strtab overflow\n", 27);
            __exit(1);
        }
        while (*s)
            strtab_buf[strtab_sz++] = *s++;
        strtab_buf[strtab_sz++] = '\0';
    }

    /* ── 计算文件偏移 ── */
    int shdr_ofs = 64;
    int text_ofs = (shdr_ofs + num_sections * 64 + 15) & ~15;
    int text_sz  = elf_code_size;
    int rela_ofs = (text_ofs + text_sz + 7) & ~7;
    int rela_sz  = elf_rel_count * 24;
    int sym_ofs  = (rela_ofs + rela_sz + 7) & ~7;
    int sym_sz   = (elf_sym_count + 1) * 24;
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
    for (ei = 9; ei < 16; ei++) b[p++] = 0;

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
    b[p++] = 64; b[p++] = 0;
    b[p++] = 0; b[p++] = 0;

    /* e_phnum (2) */
    b[p++] = 0; b[p++] = 0;

    /* e_shentsize (2) */
    b[p++] = 64; b[p++] = 0;

    /* e_shnum (2) */
    b[p++] = num_sections; b[p++] = 0;

    /* e_shstrndx (2) — 指向 .shstrtab 节区（索引 6） */
    b[p++] = 6; b[p++] = 0;

    /* ── Section header table (占位) ── */
    int shdr_start = p;
    for (ei = 0; ei < num_sections * 64; ei++) b[p++] = 0;

    /* ── .text data ── */
    while (p < text_ofs) b[p++] = 0;
    for (ei = 0; ei < elf_code_size; ei++) b[p++] = elf_code_buf[ei];

    /* ── 符号索引重映射（局部在前，全局在后）── */
    {
        int elf_idx = 1;
        for (i = 0; i < elf_sym_count; i++)
            if (!elf_syms[i].is_global) elf_syms[i].sym_idx = elf_idx++;
        for (i = 0; i < elf_sym_count; i++)
            if (elf_syms[i].is_global) elf_syms[i].sym_idx = elf_idx++;
        for (int ri = 0; ri < elf_rel_count; ri++) {
            unsigned long r_sym = ELF64_R_SYM(elf_rels[ri].r_info);
            if (r_sym > 0) {
                int cgen_idx = (int)(r_sym - 1);
                if (cgen_idx >= 0 && cgen_idx < elf_sym_count) {
                    unsigned int r_type = ELF64_R_TYPE(elf_rels[ri].r_info);
                    elf_rels[ri].r_info = ELF64_R_INFO(elf_syms[cgen_idx].sym_idx, r_type);
                }
            }
        }
    }

    /* ── .rela.text data ── */
    while (p < rela_ofs) b[p++] = 0;
    for (ei = 0; ei < elf_rel_count; ei++) {
        int ro = elf_rels[ei].r_offset;
        b[p++] = (ro)&0xFF; b[p++] = (ro>>8)&0xFF;
        b[p++] = (ro>>16)&0xFF; b[p++] = (ro>>24)&0xFF;
        for (int z=0;z<4;z++) b[p++] = 0;
        long ri = elf_rels[ei].r_info;
        b[p++] = (ri)&0xFF; b[p++] = (ri>>8)&0xFF;
        b[p++] = (ri>>16)&0xFF; b[p++] = (ri>>24)&0xFF;
        b[p++] = (ri>>32)&0xFF; b[p++] = (ri>>40)&0xFF;
        b[p++] = (ri>>48)&0xFF; b[p++] = (ri>>56)&0xFF;
        long ra = elf_rels[ei].r_addend;
        b[p++] = (ra)&0xFF; b[p++] = (ra>>8)&0xFF;
        b[p++] = (ra>>16)&0xFF; b[p++] = (ra>>24)&0xFF;
        b[p++] = (ra>>32)&0xFF; b[p++] = (ra>>40)&0xFF;
        b[p++] = (ra>>48)&0xFF; b[p++] = (ra>>56)&0xFF;
    }

    /* ── .symtab data ── */
    while (p < sym_ofs) b[p++] = 0;
    int first_global = 1;

    /* null symbol */
    for (ei = 0; ei < 24; ei++) b[p++] = 0;

    /* 辅助：写入一个符号条目 */
    #define WRITE_SYM(idx, info, shndx, value, size) \
        do { \
            b[p++] = (idx) & 0xFF; b[p++] = ((idx) >> 8) & 0xFF; \
            b[p++] = ((idx) >> 16) & 0xFF; b[p++] = ((idx) >> 24) & 0xFF; \
            b[p++] = (info); b[p++] = 0; \
            b[p++] = (shndx) & 0xFF; b[p++] = ((shndx) >> 8) & 0xFF; \
            int _v = (value); \
            b[p++] = (_v) & 0xFF; b[p++] = ((_v) >> 8) & 0xFF; \
            b[p++] = ((_v) >> 16) & 0xFF; b[p++] = ((_v) >> 24) & 0xFF; \
            b[p++] = 0; b[p++] = 0; b[p++] = 0; b[p++] = 0; \
            int _sz = (size); \
            b[p++] = (_sz) & 0xFF; b[p++] = ((_sz) >> 8) & 0xFF; \
            b[p++] = ((_sz) >> 16) & 0xFF; b[p++] = ((_sz) >> 24) & 0xFF; \
            b[p++] = 0; b[p++] = 0; b[p++] = 0; b[p++] = 0; \
        } while(0)

    /* 先写局部符号 */
    for (i = 0; i < elf_sym_count; i++) {
        if (elf_syms[i].is_global) continue;
        first_global++;
        int bind = 0;  /* STB_LOCAL */
        int type = elf_syms[i].is_func ? 2 : 0;
        int sym_shndx = elf_syms[i].shndx;
        if (sym_shndx == 0) sym_shndx = 0;  /* SHN_UNDEF */
        WRITE_SYM(sym_str_idx[i], ELF64_ST_INFO(bind, type),
                  sym_shndx, elf_syms[i].offset, elf_syms[i].size);
    }

    /* 再写全局符号 */
    for (i = 0; i < elf_sym_count; i++) {
        if (!elf_syms[i].is_global) continue;
        int bind = 1;  /* STB_GLOBAL */
        int type = elf_syms[i].is_func ? 2 : 0;
        int sym_shndx = elf_syms[i].shndx;
        if (sym_shndx == 0) sym_shndx = 0;
        WRITE_SYM(sym_str_idx[i], ELF64_ST_INFO(bind, type),
                  sym_shndx, elf_syms[i].offset, elf_syms[i].size);
    }
    #undef WRITE_SYM

    /* ── .strtab data ── */
    for (ei = 0; ei < strtab_sz; ei++) b[p++] = strtab_buf[ei];

    /* ── .shstrtab data ── */
    for (ei = 0; ei < shstrtab_sz; ei++) b[p++] = shstrtab_buf[ei];

    /* ── 回填节区头表 ── */
    /* 辅助宏：写 int32 LE */
    #define SHDR_W4(off, val) \
        do { int _v = (val); b[(off)]=_v&0xFF; b[(off)+1]=(_v>>8)&0xFF; \
             b[(off)+2]=(_v>>16)&0xFF; b[(off)+3]=(_v>>24)&0xFF; } while(0)

    /* 节区 1: .text (sh_name=1) */
    b[shdr_start+64]=1; b[shdr_start+68]=1; b[shdr_start+72]=6;
    { int off = shdr_start + 64;
      SHDR_W4(off+24, text_ofs); SHDR_W4(off+32, text_sz);
      b[off+48]=16; }

    /* 节区 2: .rela.text (sh_name=7) */
    b[shdr_start+128]=7; b[shdr_start+132]=4;
    { int off = shdr_start + 128;
      SHDR_W4(off+24, rela_ofs); SHDR_W4(off+32, rela_sz);
      b[off+40]=4;    /* sh_link = .symtab (4) */
      b[off+44]=1;    /* sh_info = .text (1) */
      b[off+48]=8; b[off+56]=24; }

    /* 节区 3: .bss (sh_name=18) */
    b[shdr_start+192]=18; b[shdr_start+196]=8;  /* SHT_NOBITS */
    b[shdr_start+200]=3;                         /* SHF_ALLOC|SHF_WRITE */
    { int off = shdr_start + 192;
      SHDR_W4(off+24, 0);   /* sh_offset = 0 (NOBITS) */
      SHDR_W4(off+32, elf_bss_size);
      b[off+48]=32; }       /* sh_addralign */

    /* 节区 4: .symtab (sh_name=23) */
    b[shdr_start+256]=23; b[shdr_start+260]=2;
    { int off = shdr_start + 256;
      SHDR_W4(off+24, sym_ofs); SHDR_W4(off+32, sym_sz);
      b[off+40]=5;    /* sh_link = .strtab (5) */
      b[off+44]=first_global;
      b[off+48]=8; b[off+56]=24; }

    /* 节区 5: .strtab (sh_name=31) */
    b[shdr_start+320]=31; b[shdr_start+324]=3;
    { int off = shdr_start + 320;
      SHDR_W4(off+24, str_ofs); SHDR_W4(off+32, strtab_sz);
      b[off+48]=1; }

    /* 节区 6: .shstrtab (sh_name=39) */
    b[shdr_start+384]=39; b[shdr_start+388]=3;
    { int off = shdr_start + 384;
      SHDR_W4(off+24, shstr_ofs); SHDR_W4(off+32, shstrtab_sz);
      b[off+48]=1; }

    #undef SHDR_W4

    /* ── 写入文件 ── */
    int fd = __openat(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    int written = __write(fd, buf, p);
    __close(fd);
    return (written == p) ? 0 : -1;
}
