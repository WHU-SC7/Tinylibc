/*
 * 049_struct_func_arg.c — struct 作为函数参数
 *
 * 验证：struct 作为参数传递和成员读取
 * 预期：退出码 0
 */

struct Point {
    int x;
    int y;
};

int sum_coords(struct Point p) {
    return p.x + p.y;
}

int main(void) {
    struct Point p;
    p.x = 10;
    p.y = 20;

    int s = sum_coords(p);
    if (s != 30) return 1;

    return 0;
}
