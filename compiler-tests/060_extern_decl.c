/*
 * 060_extern_decl.c — extern 声明
 *
 * 验证：extern 变量声明（链接时解析）
 * 预期：退出码 0
 */

extern int external_var;

void set_external(int x) {
    external_var = x;
}

int main(void) {
    /* external_var 由链接提供 */
    external_var = 42;
    if (external_var != 42) return 1;

    set_external(100);
    if (external_var != 100) return 2;

    return 0;
}
