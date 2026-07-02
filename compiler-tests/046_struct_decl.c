/*
 * 046_struct_decl.c — struct 声明与成员访问
 *
 * 验证：struct 类型定义、变量声明、成员读写
 * 预期：退出码 0
 */

struct Point {
    int x;
    int y;
};

int main(void) {
    struct Point p;

    p.x = 10;
    p.y = 20;

    if (p.x != 10) return 1;
    if (p.y != 20) return 2;

    /* 成员运算 */
    int sum = p.x + p.y;
    if (sum != 30) return 3;

    return 0;
}
