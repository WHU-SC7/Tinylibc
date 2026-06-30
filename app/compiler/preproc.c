#include "tcc.h"

#define MAX_MACROS 4096
typedef struct { const char *name; const char *value; int value_len; } Macro;
static Macro macros[MAX_MACROS];
static int macro_count;

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

static void pp_buf(const char *s, int len, OutBuf *out, int depth);

static void do_include(const char *s, int *pos, int len, OutBuf *out, int depth) {
    while (*pos < len && s[*pos] != '"' && s[*pos] != '<') (*pos)++;
    if (*pos >= len) return;
    int delim = s[*pos]; (*pos)++;
    int fs = *pos; while (*pos < len && s[*pos] != delim) (*pos)++;
    int flen = *pos - fs; if (*pos < len) (*pos)++;
    if (flen <= 0) return;
    char fn[512]; int fi;
    for (fi = 0; fi < flen && fi < 500; fi++) { fn[fi] = s[fs + fi]; } fn[fi] = '\0';
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
    if (!fnd) __printf("tcc: cannot find '%s'\n", fn);
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
        if (cp < le && s[cp] == '(') {
            /* 函数式宏 — 不展开，略过 */
            return;
        }
        while (p < le && pp_ws(s[p])) p++;
        int vs = p; int vl = le - p;
        while (vl > 0 && pp_ws(s[vs+vl-1])) vl--;
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

static void pp_buf(const char *s, int len, OutBuf *out, int depth) {
    if (depth > 32) return;
    if (depth == 0) {
        add_macro("__x86_64__", 0, 0); add_macro("X86_64_TLIBC", "1", 1);
    }
    int i = 0;
    while (i < len) {
        if (s[i] == '/' && i+1 < len && (s[i+1]=='/'||s[i+1]=='*')) {
            if (s[i+1] == '/') {
                i += 2; while (i < len && s[i] != '\n') i++;
            } else {
                i += 2; while (i < len) {
                    if (s[i]=='*' && i+1 < len && s[i+1]=='/') { i += 2; break; }
                    i++;
                }
            }
            continue;
        }
        if (s[i] == '#' && (i == 0 || s[i-1] == '\n')) {
            int ls = i; int le = i;
            while (le < len && s[le] != '\n') {
                if (s[le] == '\\' && le+1 < len && s[le+1] == '\n') le += 2;
                else le++;
            }
            do_directive(s, ls, le, out, depth);
            i = le; if (i < len && s[i] == '\n') i++;
            continue;
        }
        if (s[i] != 13) { out_putc(out, s[i]); } i++;
    }
}

char *preprocess(const char *src, int len, const char *fname, int *out_len) {
    (void)fname;
    OutBuf out = { 0, 0, 0 };
    pp_buf(src, len, &out, 0);
    out_putc(&out, '\0');
    *out_len = out.len > 0 ? out.len - 1 : 0;
    return out.data;
}
