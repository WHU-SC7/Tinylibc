/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * elf_write.h — ELF64 可重定位目标文件写入器（tcc/tas 共享）
 *
 * 调用者填充全局缓冲区（code/syms/rels），然后调用 elf_write_object()。
 */

#ifndef ELF_WRITE_H
#define ELF_WRITE_H

#include "tlibc_everything.h"
#include "elf.h"

/* ─── 符号描述符 ─── */

#define ELF_MAX_SYMS 8192
#define ELF_MAX_RELS 16384
#define ELF_CODE_BUF_SIZE 262144  /* (256 * 1024) */

typedef struct {
    const char *name;
    int offset;
    int size;
    int is_global;
    int is_func;
    int shndx;       /* 节区索引：1=.text, 3=.bss */
    int sym_idx;
} ElfWriteSym;

/* ─── 全局输出状态 ─── */

extern unsigned char elf_code_buf[ELF_CODE_BUF_SIZE];
extern int elf_code_size;
extern ElfWriteSym elf_syms[ELF_MAX_SYMS];
extern int elf_sym_count;
extern Elf64_Rela elf_rels[ELF_MAX_RELS];
extern int elf_rel_count;

/* ─── .bss 跟踪 ─── */

extern int elf_bss_size;

/* ─── 入口 ─── */

int elf_write_object(const char *path);

#endif
