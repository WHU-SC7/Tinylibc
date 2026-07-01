#include "tcc.h"

#define MAX_MACROS 4096
#define MAX_FUNC_MACROS 1024
#define MAX_MACRO_PARAMS 64

typedef struct { const char *name; const char *value; int value_len; } Macro;
static Macro macros[MAX_MACROS];
static int macro_count;

typedef struct {
    const char *name;
    const char *params[MAX_MACRO_PARAMS];
    int param_count;
    int is_variadic;
    const char *replacement;
    int repl_len;
} FuncMacro;
static FuncMacro func_macros[MAX_FUNC_MACROS];
static int func_macro_count;

static const char *inc_paths[MAX_INCLUDE_PATHS];
static int inc_path_count;

void add_include_path(const char *path) {
    if (inc_path_count < MAX_INCLUDE_PATHS) inc_paths[inc_path_count++] = path; }

static void add_macro(const char *name, const char *val, int vlen) {
    if (macro_count < MAX_MACROS) {
        macros[macro_count].name = name; macros[macro_count].value = val;
        macros[macro_count].value_len = vlen; macro_count++; } }

static void undef_macro(const char *name) {
    int i; for (i = 0; i < macro_count; i++) {
        int j; for (j = 0; name[j]; j++) if (macros[i].name[j] != name[j]) goto nx;
        if (macros[i].name[j] == '\0') { macros[i] = macros[--macro_count]; return; }
        nx:; } }

static int macro_defined(const char *name) {
    int i; for (i = 0; i < macro_count; i++) {
        int j; for (j = 0; name[j]; j++) if (macros[i].name[j] != name[j]) goto nx2;
        if (macros[i].name[j] == '\0') return 1;
        nx2:; } return 0; }

typedef struct { char *data; int len; int cap; } OutBuf;

static void out_putc(OutBuf *b, char c) {
    if (b->data == 0) { b->cap = 65536; b->data = (char *)tlibc_malloc(b->cap); } else if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 65536;
        char *nd = (char *)tlibc_malloc(b->cap);
        int i; for (i = 0; i < b->len; i++) nd[i] = b->data[i];
        tlibc_free(b->data); b->data = nd; }
    b->data[b->len++] = c; }

static int pp_ws(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'; }
static int pp_id(char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(c>='0'&&c<='9'); }

static void skip_cond(const char *s, int *pos, int len) {
    int d = 0;
    while (*pos < len) {
        int ls = *pos; while (*pos < len && s[*pos] != '\n') (*pos)++;
        if (*pos < len) (*pos)++;
        int j; int hh = 0;
        for (j = ls; j < *pos && j < len; j++) { if (s[j] == '#') { hh = 1; break; } if (!pp_ws(s[j])) break; }
        if (!hh) continue;
        int p = j + 1; while (p < len && pp_ws(s[p])) p++;
        if (s[p]=='e'&&s[p+1]=='n'&&s[p+2]=='d'&&s[p+3]=='i'&&s[p+4]=='f') { if (d==0) return; d--; }
        else if (s[p]=='e'&&s[p+1]=='l'&&s[p+2]=='s'&&s[p+3]=='e') { if (d==0) return; }
        else if (s[p]=='i'&&s[p+1]=='f') d++;
    }
}

static char *pp_read(const char *path, int *out_len) {
    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return 0;
    int sz = __lseek(fd, 0, SEEK_END); __lseek(fd, 0, SEEK_SET);
    if (sz <= 0) { __close(fd); return 0; }
    char *b = (char *)tlibc_malloc(sz + 2);
    int n = __read(fd, b, sz); __close(fd);
    if (n != sz) { tlibc_free(b); return 0; }
    b[sz] = '\0'; *out_len = sz; return b;
}

static void pp_buf_impl(const char *s, int len, OutBuf *out, int depth, int *had_nl);
static void pp_buf(const char *s, int len, OutBuf *out, int depth);

/* 从路径中提取目录部分（不包括文件名和末尾斜杠） */
static void dirname_of(const char *full, int full_len, char *buf, int bufsz) {
    int last_slash = -1;
    int i;
    for (i = 0; i < full_len && full[i]; i++) {
        if (full[i] == '/') last_slash = i;
    }
    if (last_slash < 0) {
        buf[0] = '.';
        buf[1] = '/';
        buf[2] = '\0';
    } else {
        int j;
        for (j = 0; j < last_slash && j < bufsz - 1; j++)
            buf[j] = full[j];
        buf[j] = '\0';
    }
}

static int inc_path_added_source_dir = 0;
static char current_source_dir[1024];

static void do_include(const char *s, int *pos, int len, OutBuf *out, int depth) {
    while (*pos < len && s[*pos] != '"' && s[*pos] != '<') (*pos)++;
    if (*pos >= len) return;
    int delim = s[*pos]; (*pos)++;
    int fs = *pos; while (*pos < len && s[*pos] != delim) (*pos)++;
    int flen = *pos - fs; if (*pos < len) (*pos)++;
    if (flen <= 0) return;
    char fn[512]; int fi;
    for (fi = 0; fi < flen && fi < 500; fi++) { fn[fi] = s[fs + fi]; } fn[fi] = '\0';

    /* 对 #include "..."，先搜索源文件所在目录 */
    if (delim == '"' && current_source_dir[0] && !inc_path_added_source_dir) {
        /* 把源文件目录添加到 inc_paths 开头 */
        int pi;
        for (pi = inc_path_count; pi > 0; pi--)
            inc_paths[pi] = inc_paths[pi - 1];
        const char *dp = (const char *)tlibc_malloc(strlen(current_source_dir) + 1);
        { int ci; for (ci = 0; current_source_dir[ci]; ci++) ((char*)dp)[ci] = current_source_dir[ci]; ((char*)dp)[ci] = '\0'; }
        inc_paths[0] = dp;
        inc_path_count++;
        inc_path_added_source_dir = 1;
    }

    int fnd = 0;
    int pi; for (pi = 0; pi < inc_path_count; pi++) {
        char pth[1024]; int pj;
        for (pj = 0; inc_paths[pi][pj] && pj < 1000; pj++) pth[pj] = inc_paths[pi][pj];
        if (pj > 0 && pth[pj-1] != '/') pth[pj++] = '/';
        for (fi = 0; fi < flen && pj < 1000; fi++, pj++) pth[pj] = s[fs + fi];
        pth[pj] = '\0';
        int l2; char *fc = pp_read(pth, &l2);
        if (fc) { pp_buf(fc, l2, out, depth + 1); tlibc_free(fc); fnd = 1; break; }
    }
    if (!fnd) { __printf("tcc: cannot find '%s'\n", fn); }
}

static void get_name(const char *s, int start, int end, char *buf, int bufsz) {
    int i; int n = end - start;
    if (n > bufsz - 1) n = bufsz - 1;
    for (i = 0; i < n; i++) buf[i] = s[start + i];
    buf[n] = '\0';
}

static void do_directive(const char *s, int ls, int le, OutBuf *out, int depth) {
    int p = ls; while (p < le && pp_ws(s[p])) p++;
    if (p >= le || s[p] != '#') return;
    p++; while (p < le && pp_ws(s[p])) p++;
    int dw = p; while (p < le && !pp_ws(s[p]) && s[p] != '\n') p++;
    int dl = p - dw;

    if (dl == 7 && s[dw]=='i'&&s[dw+1]=='n'&&s[dw+2]=='c'&&s[dw+3]=='l'&&s[dw+4]=='u'&&s[dw+5]=='d'&&s[dw+6]=='e')
        { do_include(s, &p, le, out, depth); return; }

    if (dl == 6 && s[dw]=='d'&&s[dw+1]=='e'&&s[dw+2]=='f') {
        while (p < le && pp_ws(s[p])) p++;
        int ms = p; while (p < le && pp_id(s[p])) p++;
        if (p == ms) return;
        int mnl = p - ms;
        int cp = p; while (cp < le && pp_ws(s[cp])) cp++;
        if (cp == p && cp < le && s[cp] == '(') {
            /* 函数式宏（必须 ( 紧跟宏名，无空白）— 存储定义 */
            if (func_macro_count >= MAX_FUNC_MACROS) return;
            FuncMacro *fm = &func_macros[func_macro_count];
            char *mn2 = (char *)tlibc_malloc(mnl + 1);
            { int ci; for (ci = 0; ci < mnl; ci++) mn2[ci] = s[ms + ci]; mn2[mnl] = '\0'; }
            fm->name = mn2;
            fm->param_count = 0; fm->is_variadic = 0;
            cp++; /* 跳过 ( */
            while (cp < le && s[cp] != ')') {
                while (cp < le && pp_ws(s[cp])) cp++;
                if (cp >= le || s[cp] == ')') break;
                if (cp + 2 <= le && s[cp] == '.' && s[cp+1] == '.' && s[cp+2] == '.') {
                    fm->is_variadic = 1; cp += 3; break;
                }
                int ps = cp;
                while (cp < le && pp_id(s[cp])) cp++;
                if (cp > ps && fm->param_count < MAX_MACRO_PARAMS) {
                    char *pn = (char *)tlibc_malloc(cp - ps + 1);
                    { int ci; for (ci = 0; ci < cp - ps; ci++) pn[ci] = s[ps + ci]; pn[cp - ps] = '\0'; }
                    fm->params[fm->param_count++] = pn;
                }
                while (cp < le && pp_ws(s[cp])) cp++;
                if (cp < le && s[cp] == ',') { cp++; continue; }
            }
            if (cp < le && s[cp] == ')') cp++;
            while (cp < le && pp_ws(s[cp])) cp++;
            int vs = cp; int vl = le - cp;
            while (vl > 0 && pp_ws(s[vs+vl-1])) vl--;
            if (vl > 0) {
                char *rp = (char *)tlibc_malloc(vl + 1);
                { int ci; for (ci = 0; ci < vl; ci++) rp[ci] = s[vs + ci]; rp[vl] = '\0'; }
                fm->replacement = rp; fm->repl_len = vl;
            } else { fm->replacement = 0; fm->repl_len = 0; }
            func_macro_count++;
            return;
        }
        while (p < le && pp_ws(s[p])) p++;
        int vs = p; int vl = le - p;
        while (vl > 0 && pp_ws(s[vs+vl-1])) vl--;
        { int ci; for (ci = 0; ci < vl - 1; ci++) {
            if (s[vs+ci] == '/' && s[vs+ci+1] == '*') { vl = ci; break; }
            if (s[vs+ci] == '/' && s[vs+ci+1] == '/') { vl = ci; break; }
        } }
        char *n = (char *)tlibc_malloc(mnl+1); get_name(s, ms, ms+mnl, n, mnl+1);
        char *v = 0; if (vl > 0) { v = (char *)tlibc_malloc(vl+1); int ci; for (ci=0;ci<vl;ci++) v[ci]=s[vs+ci]; v[vl]='\0'; }
        add_macro(n, v, vl); return;
    }

    if (dl == 6 && s[dw]=='i'&&s[dw+1]=='f'&&s[dw+2]=='d') {
        while (p < le && pp_ws(s[p])) p++;
        int ms = p; while (p < le && pp_id(s[p])) p++;
        if (p > ms) { char mn[256]; get_name(s, ms, p, mn, 256); if (!macro_defined(mn)) skip_cond(s, &p, le); }
        return;
    }

    if (dl == 7 && s[dw]=='i'&&s[dw+1]=='f'&&s[dw+2]=='n') {
        while (p < le && pp_ws(s[p])) p++;
        int ms = p; while (p < le && pp_id(s[p])) p++;
        if (p > ms) { char mn[256]; get_name(s, ms, p, mn, 256); if (macro_defined(mn)) skip_cond(s, &p, le); }
        return;
    }

    if ((dl == 4 && s[dw]=='e'&&s[dw+1]=='l'&&s[dw+2]=='s'&&s[dw+3]=='e') ||
        (dl == 5 && s[dw]=='e'&&s[dw+1]=='n'&&s[dw+2]=='d')) return;

    if (dl == 6 && s[dw]=='u'&&s[dw+1]=='n') {
        while (p < le && pp_ws(s[p])) p++;
        int ms = p; while (p < le && pp_id(s[p])) p++;
        if (p > ms) { char mn[256]; get_name(s, ms, p, mn, 256); undef_macro(mn); }
        return;
    }

    if (dl == 5 && s[dw]=='e'&&s[dw+1]=='r'&&s[dw+2]=='r') {
        while (p < le && pp_ws(s[p])) { p++; } __printf("tcc: #error ");
        while (p < le) { __printf("%c", s[p++]); } __printf("\n"); return;
    }
}

static char *strip_all_comments(const char *src, int len, int *out_len);

static void pp_buf_impl(const char *s, int len, OutBuf *out, int depth, int *had_nl);

static void pp_buf(const char *s, int len, OutBuf *out, int depth) {
    if (depth > 32) return;
    if (depth == 0) {
        add_macro("__x86_64__", 0, 0); add_macro("X86_64_TLIBC", "1", 1);
    }
    int nlen; char *n = strip_all_comments(s, len, &nlen);
    pp_buf_impl(n, nlen, out, depth, NULL);
    tlibc_free(n);
}

static void pp_buf_impl(const char *s, int len, OutBuf *out, int depth, int *had_nl) {
    (void)had_nl;
    int i = 0;
    while (i < len) {
        if (s[i] == '#') {
            int is_directive = 0;
            if (i == 0) is_directive = 1;
            else { int bi = i;
                while (bi > 0 && (s[bi-1] == ' ' || s[bi-1] == '\t')) bi--;
                if (bi == 0 || s[bi-1] == '\n' || s[bi-1] == '\r') is_directive = 1; }
            if (is_directive) {
                int ls = i; int le = i;
                while (le < len && s[le] != '\n') {
                    if (s[le] == '\\') {
                        int nl = le + 1;
                        if (nl < len && s[nl] == '\r') nl++;
                        if (nl < len && s[nl] == '\n') le = nl + 1;
                        else le++;
                    } else { le++; }
                }
                do_directive(s, ls, le, out, depth);
                i = le; if (i < len && s[i] == '\n') i++;
                continue;
            }
        }

        /* ─── 对象宏展开 ─── */
        if ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || s[i] == '_') {
            int start = i;
            while (i < len && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
                   s[i] == '_' || (s[i] >= '0' && s[i] <= '9'))) i++;
            int id_len = i - start;
            int mi;
            int expanded = 0;
            for (mi = 0; mi < macro_count; mi++) {
                const char *mn = macros[mi].name;
                int j;
                for (j = 0; j < id_len; j++) if (mn[j] != s[start + j]) goto nomatch;
                if (mn[j] != '\0') goto nomatch;
                /* 找到了！输出宏值 */
                if (macros[mi].value && macros[mi].value_len > 0) {
                    /* 递归展开宏值中的宏（防止无限循环：depth > 64 时停止） */
                    if (depth < 64) {
                        pp_buf_impl(macros[mi].value, macros[mi].value_len, out, depth + 1, NULL);
                    } else {
                        int vi; for (vi = 0; vi < macros[mi].value_len; vi++) out_putc(out, macros[mi].value[vi]);
                    }
                }
                expanded = 1;
                break;
                nomatch:;
            }
            if (expanded) continue;
            /* 检查函数式宏：标识符后跟 ( */
            if (i < len && s[i] == '(' && func_macro_count > 0) {
                int id_match = 0;
                int fmi;
                for (fmi = 0; fmi < func_macro_count; fmi++) {
                    const char *fn = func_macros[fmi].name;
                    int j;
                    for (j = 0; j < id_len; j++) if (fn[j] != s[start + j]) goto fnm;
                    if (fn[j] != '\0') goto fnm;
                    id_match = 1;
                    /* 解析参数：从 i+1 开始，匹配 ) */
                    int ap = i + 1;
                    int adepth = 1;
                    int aprev = -1;
                    const char *arg_starts[MAX_MACRO_PARAMS];
                    int arg_lens[MAX_MACRO_PARAMS];
                    int arg_count = 0;
                    arg_starts[0] = s + ap;
                    while (adepth > 0 && ap < len) {
                        if (ap == aprev) break;
                        aprev = ap;
                        if (s[ap] == '"') {
                            ap++; while (ap < len && s[ap] != '"') {
                                if (s[ap] == '\\' && ap+1 < len) ap++;
                                ap++;
                            }
                            if (ap < len) ap++;
                            continue;
                        }
                        if (s[ap] == '(') adepth++;
                        if (s[ap] == ')') { adepth--; if (adepth == 0) break; }
                        if (adepth == 1 && s[ap] == ',' && arg_count < MAX_MACRO_PARAMS-1) {
                            arg_lens[arg_count] = (s + ap) - arg_starts[arg_count];
                            arg_count++;
                            arg_starts[arg_count] = s + ap + 1;
                        }
                        ap++;
                        if (ap >= len) break;
                    }
                    if (adepth == 0) {
                        arg_lens[arg_count] = (s + ap) - arg_starts[arg_count];
                        arg_count++;
                        /* 输出替换文本（直接输出，不递归调用 pp_buf_impl） */
                        if (func_macros[fmi].replacement) {
                            const char *rp = func_macros[fmi].replacement;
                            int rl = func_macros[fmi].repl_len;
                            int ri = 0;
                            while (ri < rl) {
                                /* 检查 __VA_ARGS__ */
                                if (func_macros[fmi].is_variadic && ri + 10 < rl &&
                                    rp[ri]=='_' && rp[ri+1]=='_' && rp[ri+2]=='V' &&
                                    rp[ri+3]=='A' && rp[ri+4]=='_' && rp[ri+5]=='A' &&
                                    rp[ri+6]=='R' && rp[ri+7]=='G' && rp[ri+8]=='S' &&
                                    rp[ri+9]=='_' && rp[ri+10]=='_') {
                                    ri += 11;
                                    int vi;
                                    for (vi = func_macros[fmi].param_count; vi < arg_count; vi++) {
                                        if (vi > func_macros[fmi].param_count) out_putc(out, ',');
                                        int vj; for (vj = 0; vj < arg_lens[vi]; vj++)
                                            out_putc(out, (arg_starts[vi])[vj]);
                                    }
                                    continue;
                                }
                                /* 跳过 #（stringify 运算符—TODO：支持真正字符串化） */
                                if (rp[ri]=='#' && ri+1 < rl && rp[ri+1] != '#') { ri++; continue; }
                                /* 跳过 ## 及其前面的逗号（GCC 扩展：,##__VA_ARGS__） */
                                if (ri+1 < rl && rp[ri]=='#' && rp[ri+1]=='#') {
                                    /* 检查 out 末尾是否有逗号，有则移除 */
                                    while (out->len > 0 && (out->data[out->len-1] == ' ' || out->data[out->len-1] == '\t'))
                                        out->len--;
                                    if (out->len > 0 && out->data[out->len-1] == ',')
                                        out->len--;
                                    ri += 2; continue;
                                }
                                /* 检查参数名 */
                                if (pp_id(rp[ri])) {
                                    int rs = ri;
                                    while (ri < rl && pp_id(rp[ri])) ri++;
                                    int matched = 0;
                                    int pi;
                                    for (pi = 0; pi < func_macros[fmi].param_count && pi < arg_count; pi++) {
                                        const char *pn = func_macros[fmi].params[pi];
                                        int jj;
                                        for (jj = 0; jj < ri - rs; jj++) if (pn[jj] != rp[rs+jj]) goto pnm;
                                        if (pn[jj] != '\0') goto pnm;
                                        int vj; for (vj = 0; vj < arg_lens[pi]; vj++)
                                            out_putc(out, (arg_starts[pi])[vj]);
                                        matched = 1;
                                        break;
                                        pnm:;
                                    }
                                    if (!matched) {
                                        int idx; for (idx = rs; idx < ri; idx++) out_putc(out, rp[idx]);
                                    }
                                    continue;
                                }
                                out_putc(out, rp[ri]); ri++;
                            }
                        }
                        i = ap + 1; /* 跳过 ) */
                    }
                    break;
                    fnm:;
                }
                if (id_match) continue;
            }
            /* 不是宏，原样输出 */
            { int idx; for (idx = start; idx < i; idx++) out_putc(out, s[idx]); }
            continue;
        }
        if (s[i] != 13) { out_putc(out, s[i]); } i++;
    }
}

/* 预处理前清除所有注释（替换为空格） */
static char *strip_all_comments(const char *src, int len, int *out_len) {
    OutBuf out = { 0, 0, 0 };
    int i = 0;
    while (i < len) {
        if (src[i] == '/' && i+1 < len) {
            if (src[i+1] == '/') {
                i += 2; while (i < len && src[i] != '\n') { out_putc(&out, ' '); i++; }
                continue;
            }
            if (src[i+1] == '*') {
                i += 2;
                while (i < len) {
                    if (src[i]=='*' && i+1<len && src[i+1]=='/') { i+=2; break; }
                    if (src[i] == '\n') out_putc(&out, '\n');
                    else out_putc(&out, ' ');
                    i++;
                }
                continue;
            }
        }
        out_putc(&out, src[i]);
        i++;
    }
    out_putc(&out, '\0');
    *out_len = out.len - 1;
    return out.data;
}

char *preprocess(const char *src, int len, const char *fname, int *out_len) {
    current_source_dir[0] = '\0';
    inc_path_added_source_dir = 0;
    if (fname) {
        { int fnl = 0; while (fname[fnl]) fnl++; dirname_of(fname, fnl, current_source_dir, 1024); }
    }
    int clean_len;
    char *clean = strip_all_comments(src, len, &clean_len);
    OutBuf out = { 0, 0, 0 };
    pp_buf(clean, clean_len, &out, 0);
    tlibc_free(clean);
    out_putc(&out, '\0');
    *out_len = out.len > 0 ? out.len - 1 : 0;
    return out.data;
}
