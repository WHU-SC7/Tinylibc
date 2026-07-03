/*
 * 060_extern_decl.c — extern 声明
 *
 * 机制：extern 声明 + 同文件定义，验证编译器和链接器正确处理 extern 符号。
 * 系统调用：无
 *
 * 用法：
 *   单文件编译链接即可
 */

extern int external_var;

void set_external(int x) {
    external_var = x;
}

int main(void) {
    external_var = 42;
    if (external_var != 42) return 1;

    set_external(100);
    if (external_var != 100) return 2;

    return 0;
}

/* extern 变量的定义 */
int external_var = 0;
