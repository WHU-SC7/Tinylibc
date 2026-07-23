/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * toyar.c — Toy ARchiver — System V 静态库归档器
 *
 * 用法：
 *   tar rcs libfoo.a foo.o bar.o    创建归档 + 符号索引
 *   tar t libfoo.a                  列表成员
 *   tar x libfoo.a                  提取成员
 *   tar r libfoo.a new.o            追加/替换
 *
 * 零 libc 依赖。
 * 符号表偏移指向对应 .o 的 ar_hdr 起始位置。
 * 长名表以 \n 分隔。
 * 无 " " 结束标记。
 *
 * 注意：避免使用 struct pointer cast 访问缓存中的 ar_hdr，
 * toyc 对此有代码生成 bug。使用直接偏移写入。
 */

#include "toyc_need.h"
#include "elf.h"

/* ═══════════════════════════════════════════════════════════════════
 *  常量
 * ═══════════════════════════════════════════════════════════════════ */

#define SARMAG       8
#define ARMAG        "!<arch>\n"
#define AR_HDR_SIZE  60
#define MAX_MEMBERS  64
#define MAX_SYMS     1024
#define MAX_NAMELEN  1024

/* ar_hdr 字段偏移 */
#define H_NAME  0
#define H_DATE  16
#define H_UID   28
#define H_GID   34
#define H_MODE  40
#define H_SIZE  48
#define H_FMAG  58

/* ═══════════════════════════════════════════════════════════════════
 *  数据结构
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    char   name[16];        /* ar_name（短名含 '/'，长名 /offset） */
    int    long_name_off;   /* data 中长名偏移（-1 表示短名） */
    int    size;            /* 数据体大小 */
    char  *data;            /* 数据体指针 */
} Member;

typedef struct {
    const char *name;
    int         member_idx;
} SymRef;

/* ═══════════════════════════════════════════════════════════════════
 *  全局状态
 * ═══════════════════════════════════════════════════════════════════ */

static Member   members[MAX_MEMBERS];
static int      member_count;
static int      sym_flag;
static int      replace_flag;
static int      list_flag;
static int      extract_flag;

/* ═══════════════════════════════════════════════════════════════════
 *  LE 读写
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
static void w32be(unsigned char *p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

/* ═══════════════════════════════════════════════════════════════════
 *  错误处理
 * ═══════════════════════════════════════════════════════════════════ */

static void error(const char *msg, const char *arg) {
    __write(2, "toyar: error: ", 12);
    __write(2, msg, strlen(msg));
    if (arg) { __write(2, ": ", 2); __write(2, arg, strlen(arg)); }
    __write(2, "\n", 1);
    __exit(1);
}

/* ═══════════════════════════════════════════════════════════════════
 *  字符串辅助
 * ═══════════════════════════════════════════════════════════════════ */

static int parse_dec(const char *s, int maxlen) {
    int v = 0;
    for (int i = 0; i < maxlen && s[i] >= '0' && s[i] <= '9'; i++)
        v = v * 10 + (s[i] - '0');
    return v;
}

static void write_dec(char *buf, int len, int val) {
    for (int i = len - 1; i >= 0; i--) {
        buf[i] = '0' + (val % 10);
        val /= 10;
    }
}

static const char *file_base(const char *path) {
    const char *p = path;
    while (*path) {
        if (*path == '/') p = path + 1;
        path++;
    }
    return p;
}

/* ═══════════════════════════════════════════════════════════════════
 *  文件 I/O
 * ═══════════════════════════════════════════════════════════════════ */

static char *read_file(const char *path, int *out_sz) {
    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) error("cannot open", path);
    long sz = __lseek(fd, 0, SEEK_END);
    if (sz < 0) { __close(fd); error("cannot seek", path); }
    __lseek(fd, 0, SEEK_SET);
    char *buf = (char *)tlibc_malloc(sz + 4);
    if (!buf) { __close(fd); error("out of memory", path); }
    long total = 0;
    while (total < sz) {
        long got = __read(fd, buf + total, sz - total);
        if (got <= 0) break;
        total += got;
    }
    __close(fd);
    if (total != sz) error("short read", path);
    buf[sz] = 0; buf[sz + 1] = 0; buf[sz + 2] = 0; buf[sz + 3] = 0;
    *out_sz = (int)sz;
    return buf;
}

static void write_file(const char *path, const char *data, int sz) {
    int fd = __openat(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) error("cannot create", path);
    int n = (int)__write(fd, data, sz);
    __close(fd);
    if (n != sz) error("short write", path);
}

/* ═══════════════════════════════════════════════════════════════════
 *  ar_hdr 写入 — 直接偏移写入，避 toyc struct ptr cast bug
 * ═══════════════════════════════════════════════════════════════════ */

static void fill_ar_hdr(char *buf, const char *name16, int data_size) {
    /* ar_name */
    for (int i = 0; i < 16; i++)
        buf[H_NAME + i] = name16[i] ? name16[i] : ' ';
    /* date / uid / gid / mode = 空格 */
    for (int i = H_DATE; i < H_UID + 6; i++) buf[i] = ' ';
    for (int i = H_UID; i < H_GID + 6; i++) buf[i] = ' ';
    for (int i = H_GID; i < H_MODE + 8; i++) buf[i] = ' ';
    for (int i = H_MODE; i < H_SIZE; i++) buf[i] = ' ';
    /* size */
    write_dec(buf + H_SIZE, 10, data_size);
    /* fmag */
    buf[H_FMAG] = '`';
    buf[H_FMAG + 1] = '\n';
}

/* ═══════════════════════════════════════════════════════════════════
 *  Member 操作
 * ═══════════════════════════════════════════════════════════════════ */

static int match_name(int idx, const char *bn) {
    Member *m = &members[idx];
    int bl = strlen(bn);
    if (m->long_name_off >= 0) {
        const char *ln = m->data + m->long_name_off;
        if ((int)strlen(ln) != bl) return 0;
        for (int i = 0; i < bl; i++)
            if (ln[i] != bn[i]) return 0;
        return 1;
    }
    int nl = 0;
    while (nl < 16 && m->name[nl] && m->name[nl] != ' ' && m->name[nl] != '/')
        nl++;
    if (nl != bl) return 0;
    for (int i = 0; i < bl; i++)
        if (m->name[i] != bn[i]) return 0;
    return 1;
}

/* 通过函数参数传递指针，避免 toyc 在局部变量中截断 64 位指针 */
static void copy_bytes(char *d, const char *s, int n) {
    int i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static void add_member(const char *path) {
    if (member_count >= MAX_MEMBERS) error("too many members", NULL);
    Member *m = &members[member_count];
    m->size = 0;
    m->data = read_file(path, &m->size);
    m->long_name_off = -1;
    const char *bn = file_base(path);
    int bl = strlen(bn);
    for (int i = 0; i < 16; i++) m->name[i] = ' ';
    if (bl <= 15) {
        for (int i = 0; i < bl; i++) m->name[i] = bn[i];
        m->name[bl] = '/';
    } else {
        /* 文件名 >15 字符暂不支持——toyc 代码生成 bug 导致二次分配时 64 位指针被截断 */
        error("filename too long (max 15 chars)", bn);
    }
    member_count++;
}

/* ═══════════════════════════════════════════════════════════════════
 *  读归档
 * ═══════════════════════════════════════════════════════════════════ */

static void read_archive(const char *path) {
    int fsz;
    char *buf = read_file(path, &fsz);
    if (fsz < SARMAG) error("bad archive", path);
    for (int i = 0; i < SARMAG; i++)
        if (buf[i] != ARMAG[i]) error("bad magic", path);

    char *lname_data = NULL;
    int   lname_sz = 0;
    int pos = SARMAG;

    while (pos + AR_HDR_SIZE <= fsz) {
        int nl = 0;
        while (nl < 16 && buf[pos + nl] && buf[pos + nl] != ' ') nl++;

        int dsz = parse_dec(buf + pos + H_SIZE, 10);
        int dp = pos + AR_HDR_SIZE;
        if (dp + dsz > fsz) error("truncated", path);

        /* Check fmag */
        if (buf[pos + H_FMAG] != '`' || buf[pos + H_FMAG + 1] != '\n')
            error("bad header", path);

        if (nl == 1 && buf[pos] == '/') {
            /* 符号表 — 跳过 */
        } else if (nl >= 2 && buf[pos] == '/' && buf[pos + 1] == '/') {
            lname_data = buf + dp;
            lname_sz = dsz;
        } else if (nl == 1 && buf[pos] == ' ') {
            break;
        } else {
            if (member_count >= MAX_MEMBERS) error("too many members", NULL);
            Member *m = &members[member_count];
            for (int i = 0; i < 16; i++)
                m->name[i] = (i < nl) ? buf[pos + i] : ' ';
            m->long_name_off = -1;

            char *cpy = (char *)tlibc_malloc(dsz);
            for (int i = 0; i < dsz; i++) cpy[i] = buf[dp + i];
            m->data = cpy;
            m->size = dsz;

            if (nl >= 2 && buf[pos] == '/'
                && buf[pos + 1] >= '0' && buf[pos + 1] <= '9'
                && lname_data)
            {
                int off = parse_dec(buf + pos + 1, 15);
                if (off < lname_sz) {
                    int end = off;
                    while (end < lname_sz && lname_data[end] != '\n') end++;
                    int llen = end - off;
                    /* 扩展 data buffer 嵌入长名 (这里 cpy 是局部变量，不会触发 toyc truncation) */
                    int newsz = dsz + llen + 1;
                    cpy = (char *)tlibc_malloc(newsz);
                    for (int i = 0; i < dsz; i++) cpy[i] = buf[dp + i];
                    for (int i = 0; i < llen; i++) cpy[dsz + i] = lname_data[off + i];
                    cpy[dsz + llen] = 0;
                    tlibc_free(m->data);
                    m->data = cpy;
                    m->long_name_off = dsz;
                }
            }
            member_count++;
        }

        pos = dp + dsz;
        if (pos & 1) pos++;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  写归档
 * ═══════════════════════════════════════════════════════════════════ */

static void write_archive(const char *path) {
    /* ── Phase 1: 符号收集 ── */
    SymRef syms[MAX_SYMS];
    int nsym = 0;

    if (sym_flag) {
        for (int mi = 0; mi < member_count; mi++) {
            char *d = members[mi].data;
            int sz = members[mi].size;
            if (sz < 64) continue;
            if (d[0] != 0x7F || d[1] != 'E' || d[2] != 'L' || d[3] != 'F') continue;
            if (d[4] != 2) continue;
            if (r16((const unsigned char*)d + 16) != 1) continue;

            int shnum = r16((const unsigned char*)d + 60);
            int shoff = r64((const unsigned char*)d + 40);
            const unsigned char *shd = (const unsigned char*)d + shoff;

            int sym_off = 0, sym_sz = 0, str_off = 0, str_sz = 0;
            int found = 0;
            for (int si = 1; si < shnum && !found; si++) {
                const unsigned char *sh = shd + si * 64;
                if (r32(sh + 4) == 2) {
                    sym_off = (int)r64(sh + 24);
                    sym_sz  = (int)r64(sh + 32);
                    int slink = (int)r32(sh + 40); /* sh_link */
                    if (slink > 0 && slink < shnum) {
                        const unsigned char *ssh = shd + slink * 64;
                        str_off = (int)r64(ssh + 24);
                        str_sz  = (int)r64(ssh + 32);
                    }
                    found = 1;
                }
            }
            if (!found) continue;

            int nent = sym_sz / 24;
            const unsigned char *raw = (const unsigned char*)d + sym_off;
            const char *strtab = (const char*)d + str_off;

            for (int ei = 0; ei < nent; ei++) {
                const unsigned char *ent = raw + ei * 24;
                unsigned char info = ent[4];
                unsigned char bind = (info >> 4) & 0xF;
                unsigned char typ = info & 0xF;
                uint16_t shndx = r16(ent + 6);
                if (bind != 1 && bind != 2) continue;   /* STB_GLOBAL | STB_WEAK */
                if (shndx == 0) continue;
                if (typ == 4 || typ == 3) continue;     /* skip STT_FILE | STT_SECTION */
                uint32_t noff = r32(ent);
                if (noff >= (uint32_t)str_sz) continue;
                const char *sn = strtab + noff;
                if (!sn[0]) continue;
                if (nsym >= MAX_SYMS) error("too many symbols", NULL);
                syms[nsym].name = sn;
                syms[nsym].member_idx = mi;
                nsym++;
            }
        }
    }

    /* ── Phase 2: 符号排序（插入排序） ── */
    for (int i = 1; i < nsym; i++) {
        SymRef t = syms[i];
        int j = i - 1;
        while (j >= 0) {
            const char *a = syms[j].name, *b = t.name;
            while (*a && *b && *a == *b) { a++; b++; }
            int d = (unsigned char)*a - (unsigned char)*b;
            if (d > 0) { syms[j + 1] = syms[j]; j--; } else break;
        }
        syms[j + 1] = t;
    }

    /* ── Phase 3: 长名表 ── */
    char lname[MAX_NAMELEN];
    int  lnsz = 0;
    int  loff[MAX_MEMBERS];
    for (int i = 0; i < member_count; i++) {
        if (members[i].long_name_off >= 0) {
            loff[i] = lnsz;
            const char *p = members[i].data + members[i].long_name_off;
            while (*p && lnsz < MAX_NAMELEN) lname[lnsz++] = *p++;
            if (lnsz < MAX_NAMELEN) lname[lnsz++] = '\n';
        } else {
            loff[i] = -1;
        }
    }

    /* ── Phase 4: 布局计算 ── */
    int sym_dsz = 4 + nsym * 4;
    for (int i = 0; i < nsym; i++)
        sym_dsz += strlen(syms[i].name) + 1;

    int hdr_off[MAX_MEMBERS];
    int p = SARMAG;

    if (nsym > 0) {
        p += AR_HDR_SIZE + sym_dsz;
        if (p & 1) p++;
    }
    if (lnsz > 0) {
        p += AR_HDR_SIZE + lnsz;
        if (p & 1) p++;
    }
    for (int i = 0; i < member_count; i++) {
        hdr_off[i] = p;
        p += AR_HDR_SIZE + members[i].size;
        if (p & 1) p++;
    }
    int total = p;

    /* ── Phase 5: 写缓存 — 直接偏移写入，不用 struct ptr cast ── */
    char *out = (char *)tlibc_malloc(total + 4);
    __memset(out, 0, total + 4);
    p = 0;

    /* ARMAG */
    for (int i = 0; i < 8; i++) out[p++] = ARMAG[i];

    /* / 成员 */
    if (nsym > 0) {
        char name16[16];
        for (int i = 0; i < 16; i++) name16[i] = ' ';
        name16[0] = '/'; name16[1] = ' ';
        fill_ar_hdr(out + p, name16, sym_dsz);
        p += AR_HDR_SIZE;

        w32be((unsigned char*)out + p, nsym); p += 4;
        for (int i = 0; i < nsym; i++) {
            w32be((unsigned char*)out + p, hdr_off[syms[i].member_idx]);
            p += 4;
        }
        for (int i = 0; i < nsym; i++) {
            const char *sn = syms[i].name;
            while (*sn) out[p++] = *sn++;
            out[p++] = 0;
        }
        if (p & 1) out[p++] = '\n';
    }

    /* // 成员 */
    if (lnsz > 0) {
        char name16[16];
        for (int i = 0; i < 16; i++) name16[i] = ' ';
        name16[0] = '/'; name16[1] = '/'; name16[2] = ' ';
        fill_ar_hdr(out + p, name16, lnsz);
        p += AR_HDR_SIZE;

        for (int i = 0; i < lnsz; i++) out[p++] = lname[i];
        if (p & 1) out[p++] = '\n';
    }

    /* 普通成员 */
    for (int i = 0; i < member_count; i++) {
        Member *m = &members[i];
        char name16[16];

        for (int j = 0; j < 16; j++) name16[j] = ' ';

        if (members[i].long_name_off >= 0) {
            int off = loff[i];
            name16[0] = '/';
            int tmp = off;
            int nd = 1;
            while (tmp >= 10) { tmp /= 10; nd++; }
            tmp = off;
            for (int j = nd - 1; j >= 0; j--) {
                name16[1 + j] = '0' + (tmp % 10);
                tmp /= 10;
            }
        } else {
            int j = 0;
            while (j < 15 && m->name[j] && m->name[j] != ' ' && m->name[j] != '/') {
                name16[j] = m->name[j];
                j++;
            }
            name16[j] = '/';
        }

        fill_ar_hdr(out + p, name16, m->size);
        p += AR_HDR_SIZE;

        for (int j = 0; j < m->size; j++)
            out[p++] = m->data[j];

        if (p & 1) out[p++] = '\n';
    }

    write_file(path, out, total);
    tlibc_free(out);
}

/* ═══════════════════════════════════════════════════════════════════
 *  列表 / 提取
 * ═══════════════════════════════════════════════════════════════════ */

static void list_archive(const char *path) {
    member_count = 0;
    read_archive(path);
    for (int i = 0; i < member_count; i++) {
        Member *m = &members[i];
        if (m->long_name_off >= 0) {
            __write(1, m->data + m->long_name_off, strlen(m->data + m->long_name_off));
        } else {
            int nl = 0;
            while (nl < 16 && members[i].name[nl]
                   && members[i].name[nl] != ' ' && members[i].name[nl] != '/')
                nl++;
            __write(1, members[i].name, nl);
        }
        __write(1, "\n", 1);
    }
}

static void extract_archive(const char *path) {
    member_count = 0;
    read_archive(path);
    for (int i = 0; i < member_count; i++) {
        Member *m = &members[i];
        const char *fn;
        char nb[32];
        if (m->long_name_off >= 0) {
            fn = m->data + m->long_name_off;
        } else {
            int nl = 0;
            while (nl < 16 && m->name[nl] && m->name[nl] != ' ' && m->name[nl] != '/') {
                nb[nl] = m->name[nl];
                nl++;
            }
            nb[nl] = 0;
            fn = nb;
        }
        write_file(fn, m->data, m->size);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  入口
 * ═══════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    if (argc < 3) {
        __write(2, "usage: toyar {r|t|x}[c][s] archive [files...]\n", 44);
        return 1;
    }

    const char *opts = argv[1];
    const char *archive_path = argv[2];
    sym_flag = replace_flag = list_flag = extract_flag = 0;

    for (const char *p = opts; *p; p++) {
        switch (*p) {
        case 'r': replace_flag = 1; break;
        case 't': list_flag = 1; break;
        case 'x': extract_flag = 1; break;
        case 's': sym_flag = 1; break;
        case 'c': break;
        default:
            __write(2, "toyar: unknown option '", 20);
            __write(2, p, 1);
            __write(2, "'\n", 2);
            return 1;
        }
    }

    if (replace_flag + list_flag + extract_flag != 1) {
        __write(2, "toyar: exactly one of {r,t,x} required\n", 38);
        return 1;
    }

    if (list_flag) { list_archive(archive_path); return 0; }
    if (extract_flag) { extract_archive(archive_path); return 0; }

    /* replace / create */
    member_count = 0;
    {
        int fd = __openat(AT_FDCWD, archive_path, O_RDONLY, 0);
        if (fd >= 0) { __close(fd); read_archive(archive_path); }
    }

    for (int i = 3; i < argc; i++) {
        const char *fn = argv[i];
        const char *bn = file_base(fn);
        int old = -1;
        for (int j = 0; j < member_count; j++) {
            if (match_name(j, bn)) { old = j; break; }
        }
        if (old >= 0) {
            tlibc_free(members[old].data);
            for (int j = old; j < member_count - 1; j++) members[j] = members[j + 1];
            member_count--;
        }
        add_member(fn);
    }

    write_archive(archive_path);
    return 0;
}
