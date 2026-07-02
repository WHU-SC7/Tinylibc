/*
 * 067_ifdef.c — #ifdef / #ifndef 条件编译
 *
 * 验证：条件编译的基础功能
 * 预期：退出码 0
 */

#define FEATURE_ENABLED

int main(void) {
    int x = 0;

#ifdef FEATURE_ENABLED
    x = 10;
#else
    x = 20;
#endif
    if (x != 10) return 1;

#ifdef NOT_DEFINED
    x = 30;
#else
    x = 40;
#endif
    if (x != 40) return 2;

#ifndef NOT_DEFINED
    x = 50;
#else
    x = 60;
#endif
    if (x != 50) return 3;

#ifndef FEATURE_ENABLED
    x = 70;
#else
    x = 80;
#endif
    if (x != 80) return 4;

    return 0;
}
