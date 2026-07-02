/*
 * 055_enum.c — enum 声明与使用
 *
 * 验证：enum 常量定义和使用
 * 预期：退出码 0
 */

enum Color {
    RED,
    GREEN,
    BLUE
};

enum Status {
    OK = 0,
    ERR_NOT_FOUND = -1,
    ERR_ACCESS = -2
};

int main(void) {
    enum Color c;

    c = RED;
    if (c != 0) return 1;

    c = GREEN;
    if (c != 1) return 2;

    c = BLUE;
    if (c != 2) return 3;

    /* 指定值的 enum */
    if (OK != 0)         return 4;
    if (ERR_NOT_FOUND != -1) return 5;
    if (ERR_ACCESS != -2)    return 6;

    /* switch on enum */
    c = GREEN;
    int result = 0;
    switch (c) {
        case RED:
            result = 10;
            break;
        case GREEN:
            result = 20;
            break;
        case BLUE:
            result = 30;
            break;
    }
    if (result != 20) return 7;

    return 0;
}
