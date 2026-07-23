/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * elf_write.h — ELF64 可重定位目标文件写入器（toyc/toyas 共享）
 *
 * 调用者填充全局缓冲区（code/syms/rels），然后调用 elf_write_object()。
 */

#ifndef ELF_WRITE_H
#define ELF_WRITE_H

#include "elf.h"     /* 通过 elf.h → toyc_need.h 获得所有基础类型 */

/* ─── 符号描述符 ─── */

#define ELF_MAX_SYMS 16384
#define ELF_MAX_RELS 32768
#define ELF_CODE_BUF_SIZE 524288  /* (512 * 1024) */
#ifndef DATA_BUF_SIZE
#define DATA_BUF_SIZE 524288      /* .data 段缓冲区 (512*1024) */
#endif

typedef struct {
    const char *name;
    int offset;
    int size;
    int is_global;
    int is_func;
    int shndx;       /* 节区索引：1=.text, 3=.data, 4=.bss */
    int sym_idx;
} ElfWriteSym;

/* ─── 全局输出状态 ─── */

extern unsigned char elf_code_buf[ELF_CODE_BUF_SIZE];
extern int elf_code_size;
extern ElfWriteSym elf_syms[ELF_MAX_SYMS];
extern int elf_sym_count;
extern Elf64_Rela elf_rels[ELF_MAX_RELS];
extern int elf_rel_count;

/* ─── .data 跟踪 ─── */

extern unsigned char elf_data_buf[DATA_BUF_SIZE];
extern int elf_data_size;
extern Elf64_Rela elf_data_rels[ELF_MAX_RELS];
extern int elf_data_rel_count;

extern int elf_bss_size;

/* ─── 入口 ─── */

int elf_write_object(const char *path);

#endif
