/*
 * 065_define_obj.c — #define 对象宏
 *
 * 验证：简单宏替换
 * 预期：退出码 0
 */

#define VAL 42
#define NEG -1
#define EXPR (3 + 4)

int main(void) {
    if (VAL != 42) return 1;
    if (NEG != -1) return 2;
    if (EXPR != 7) return 3;

#define PI 314
#define SCALE 100
    int r = PI * 2 / SCALE;
    if (r != 6) return 4;  /* 314*2/100 = 6.28 → 6 */

    return 0;
}
