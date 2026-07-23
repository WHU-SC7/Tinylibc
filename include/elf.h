/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * elf.h — ELF64 文件格式结构体和常量
 *
 * 机制：定义 ELF64 可重定位目标文件和可执行文件所需的全部结构。
 *       使用 uint64_t 等宽类型保证布局正确，不依赖外部头文件。
 *
 * 索引：
 *   Elf64_Ehdr    ELF 文件头
 *   Elf64_Shdr    节区头（section header）
 *   Elf64_Sym     符号表条目
 *   Elf64_Rela    重定位条目（带 addend）
 */

#ifndef ELF_H
#define ELF_H

#include "toyc_need.h"

/* ─── ELF 标识 ─── */

#define EI_MAG0        0
#define EI_MAG1        1
#define EI_MAG2        2
#define EI_MAG3        3
#define EI_CLASS       4
#define EI_DATA        5
#define EI_VERSION     6
#define EI_OSABI       7
#define EI_ABIVERSION  8
#define EI_PAD         9

#define ELFMAG0       0x7f
#define ELFMAG1       'E'
#define ELFMAG2       'L'
#define ELFMAG3       'F'
#define ELFCLASS64    2
#define ELFDATA2LSB   1
#define EV_CURRENT    1
#define ELFOSABI_NONE 0

/* ─── 类型 ─── */

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef  int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef  int64_t Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;

/* ─── 文件头 ─── */

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

#define ELF64_EHDR_SIZE 64

/* e_type */
#define ET_NONE   0
#define ET_REL    1
#define ET_EXEC   2

/* e_machine */
#define EM_X86_64 62

/* ─── 节区头 ─── */

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

#define ELF64_SHDR_SIZE 64

/* sh_type */
#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_NOBITS      8

/* sh_flags */
#define SHF_WRITE       0x01
#define SHF_ALLOC       0x02
#define SHF_EXECINSTR   0x04

/* ─── 符号表 ─── */

typedef struct {
    Elf64_Word    st_name;
    unsigned char st_info;
    unsigned char st_other;
    Elf64_Half    st_shndx;
    Elf64_Addr    st_value;
    Elf64_Xword   st_size;
} Elf64_Sym;

#define ELF64_SYM_SIZE 24

/* st_info 辅助宏 */
#define ELF64_ST_BIND(i)       ((i) >> 4)
#define ELF64_ST_TYPE(i)       ((i) & 0xf)
#define ELF64_ST_INFO(b, t)    (((b) << 4) | ((t) & 0xf))

#define STB_LOCAL     0
#define STB_GLOBAL    1
#define STB_WEAK      2

#define STT_NOTYPE    0
#define STT_OBJECT    1
#define STT_FUNC      2

/* st_other */
#define STV_DEFAULT   0

/* 特殊节区索引 */
#define SHN_UNDEF     0

/* ─── 重定位 ─── */

typedef struct {
    Elf64_Addr   r_offset;
    Elf64_Xword  r_info;
    Elf64_Sxword r_addend;
} Elf64_Rela;

#define ELF64_RELA_SIZE 24

#define ELF64_R_SYM(i)       ((i) >> 32)
#define ELF64_R_TYPE(i)      ((Elf64_Word)(i))
#define ELF64_R_INFO(s, t)   ((((Elf64_Xword)(s)) << 32) | ((t) & 0xffffffffUL))

#define R_X86_64_NONE      0
#define R_X86_64_64        1
#define R_X86_64_PC32      2
#define R_X86_64_32        10
#define R_X86_64_PLT32     4

#endif /* ELF_H */
