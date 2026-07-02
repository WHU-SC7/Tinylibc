/*
 * 047_struct_assign.c — struct 成员赋值
 *
 * 验证：struct 成员赋值不影响其他成员
 * 预期：退出码 0
 */

struct Data {
    int a;
    int b;
    int c;
};

int main(void) {
    struct Data d;

    d.a = 1;
    d.b = 2;
    d.c = 3;

    if (d.a != 1) return 1;
    if (d.b != 2) return 2;
    if (d.c != 3) return 3;

    /* 修改一个成员，检查其他成员 */
    d.b = 100;
    if (d.a != 1)   return 4;
    if (d.b != 100) return 5;
    if (d.c != 3)   return 6;

    return 0;
}
