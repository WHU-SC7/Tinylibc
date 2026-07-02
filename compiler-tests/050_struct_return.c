/*
 * 050_struct_return.c — struct 作为函数返回值
 *
 * 验证：返回 struct 并访问其成员
 * 注意：tcc 可能处理有问题（struct 用寄存器或临时栈传递）
 * 预期：退出码 0
 */

struct Point {
    int x;
    int y;
};

struct Point make_point(int a, int b) {
    struct Point p;
    p.x = a;
    p.y = b;
    return p;
}

int main(void) {
    struct Point p = make_point(10, 20);

    if (p.x != 10) return 1;
    if (p.y != 20) return 2;

    return 0;
}
