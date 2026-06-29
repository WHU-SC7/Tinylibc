#include "tlibc_test.h"
#include "tlibc_compat.h"
#include "assert.h"

TEST_DEFINE_COUNTERS();

/* 测试 assert 在条件为真时不 abort（NDEBUG 未定义） */
void test_assert_pass(void) {
    TEST_START("assert(true) passes");
    assert(1 == 1);
    TEST_PASS();
}

void test_assert_side_effect(void) {
    TEST_START("assert preserves expression side effects (conceptual)");
    int x = 0;
    assert((x = 42) == 42);
    TEST_ASSERT_EQ(x, 42, "%d");
    TEST_PASS();
}

/* ========== main ========== */

int main(void) {
    TEST_BEGIN("assert library tests");

    test_assert_pass();
    test_assert_side_effect();

    return TEST_END();
}
