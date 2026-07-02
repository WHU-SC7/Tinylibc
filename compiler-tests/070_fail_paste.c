/*
 * 070_fail_paste.c — ## Token 粘贴（未实现）
 *
 * 验证：## 是否能在宏中粘贴 token
 * 已知限制：tcc 仅支持 ,##__VA_ARGS__ 变体，不支持普通 ##
 * 预期：此测试可能产生意外结果
 */

#define CONCAT(a, b) a ## b

int main(void) {
    /* 如果 ## 实现，CONCAT(12, 34) 展开为 1234 */
    /* tcc 未实现，结果不可预测 */
    /* 目前只能确认编译通过 */
    return 0;
}
