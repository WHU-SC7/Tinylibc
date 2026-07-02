/*
 * 088_global_ptr_array.c — 全局指针数组访问
 *
 * preproc.c 中 inc_paths[count] = path 的模式。
 * 验证全局指针数组的读写（下标 + 赋值）。
 *
 * EXPECT: 42
 */

static const char *arr[8];
static int count;

static void add(const char *s) {
    if (count < 8) arr[count++] = s;
}

int main(void) {
    add("hello");
    add("world");
    if (count != 2) return 1;
    /* arr[0], arr[1] 应该指向不同字符串 */
    if (arr[0] == arr[1]) return 2;
    return 42;
}
