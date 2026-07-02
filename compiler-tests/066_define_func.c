/*
 * 066_define_func.c — #define 函数宏
 *
 * 验证：带参数的宏展开
 * 预期：退出码 0
 */

#define ADD(a, b) ((a) + (b))
#define SQUARE(x) ((x) * (x))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int main(void) {
    if (ADD(3, 4) != 7)      return 1;
    if (SQUARE(5) != 25)     return 2;
    if (MAX(10, 20) != 20)   return 3;
    if (MAX(30, 10) != 30)   return 4;

    /* 宏参数中的表达式 */
    if (SQUARE(3 + 1) != 16) return 5;  /* ((3+1)*(3+1)) */

    /* 宏嵌套 */
    if (ADD(SQUARE(2), SQUARE(3)) != 13) return 6;  /* (4)+(9) */

    return 0;
}
