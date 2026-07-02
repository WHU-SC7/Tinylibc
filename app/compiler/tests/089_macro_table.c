/*
 * 089_macro_table.c — 模拟宏表结构体数组访问
 *
 * preproc.c 中 macros[count].name = name 模式。
 *
 * EXPECT: 42
 */

typedef struct { const char *name; int val; } Entry;
static Entry tbl[16];
static int count;

static void add(const char *name, int val) {
    if (count < 16) {
        tbl[count].name = name;
        tbl[count].val  = val;
        count++;
    }
}

static int find(const char *name) {
    int i;
    for (i = 0; i < count; i++) {
        const char *p = tbl[i].name;
        const char *q = name;
        int match = 1;
        while (*p && *q && *p == *q) { p++; q++; }
        if (*p == '\0' && *q == '\0') return tbl[i].val;
    }
    return -1;
}

int main(void) {
    add("FOO", 10);
    add("BAR", 20);
    if (find("FOO") != 10) return 1;
    if (find("BAR") != 20) return 2;
    if (find("BAZ") != -1) return 3;
    return 42;
}
