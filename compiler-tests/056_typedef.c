/*
 * 056_typedef.c — typedef 类型别名
 *
 * 验证：typedef 声明和使用
 * 预期：退出码 0
 */

typedef int integer;
typedef unsigned char byte;
typedef struct {
    int x;
    int y;
} Point;

int main(void) {
    integer a = 42;
    if (a != 42) return 1;

    byte b = 200;
    if (b != 200) return 2;

    Point p;
    p.x = 10;
    p.y = 20;
    if (p.x != 10) return 3;
    if (p.y != 20) return 4;

    typedef integer* int_ptr;
    int_ptr q;
    integer val = 100;
    q = &val;
    if (*q != 100) return 5;

    return 0;
}
