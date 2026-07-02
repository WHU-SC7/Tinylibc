/*
 * 048_struct_ptr.c — struct 指针成员访问 p->member
 *
 * 验证：通过指针访问 struct 成员
 * 预期：退出码 0
 */

struct Point {
    int x;
    int y;
};

int main(void) {
    struct Point p;
    struct Point *ptr;

    ptr = &p;
    ptr->x = 10;
    ptr->y = 20;

    if (ptr->x != 10) return 1;
    if (ptr->y != 20) return 2;

    /* 成员通过指针被修改后，原变量也变化 */
    if (p.x != 10) return 3;
    if (p.y != 20) return 4;

    return 0;
}
