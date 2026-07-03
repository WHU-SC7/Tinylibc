/*
 * Test: anonymous struct typedef member offset
 *
 * Verifies that b->data accesses the correct offset (0) even when
 * another struct (e.g., epoll_event) has a member named "data" at a
 * different offset (4).
 *
 * .EXPECT: 0
 */

typedef struct { char *data; int len; int cap; } OutBuf;
typedef struct { int events; void *data; } epoll_event;

void test_arrow(void) {
    OutBuf buf;
    OutBuf *b = &buf;
    b->data = (char*)0xDEAD;
    b->len = 42;
    b->cap = 100;
    if ((long)b->data != (long)0xDEAD) __exit(1);
    if (b->len != 42) __exit(2);
    if (b->cap != 100) __exit(3);
}

void test_dot(void) {
    OutBuf buf;
    buf.data = (char*)0xBEEF;
    buf.len = 99;
    buf.cap = 200;
    if ((long)buf.data != (long)0xBEEF) __exit(4);
    if (buf.len != 99) __exit(5);
    if (buf.cap != 200) __exit(6);
}

void test_multiple_anonymous(void) {
    typedef struct { int x; int y; } Point;
    typedef struct { long id; } Record;

    Point p;
    p.x = 10;
    p.y = 20;
    if (p.x != 10) __exit(7);
    if (p.y != 20) __exit(8);

    Record r;
    r.id = 999;
    if (r.id != 999) __exit(9);
}

int main(void) {
    test_arrow();
    test_dot();
    test_multiple_anonymous();
    return 0;
}
