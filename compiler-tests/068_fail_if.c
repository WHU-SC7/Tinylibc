/*
 * 068_fail_if.c — #if 常量表达式（未实现）
 *
 * 验证：#if expr 是否被支持
 * 已知限制：tcc 只支持 #ifdef/#ifndef，不支持 #if
 * 预期：此测试编译时报错（无法展开 #if 1）
 */

#if 1
int main(void) {
    return 0;
}
#else
#error should not see this
#endif
