/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * test_math.c — 数学函数单元测试
 *
 * 测试策略：
 *   - 基础函数（sqrt/fabs/ceil/floor/trunc/round/fmod）用精确期望值
 *   - 超越函数（sin/cos/exp/log/pow）用近似值，容差 1e-10
 *   - 验证特殊值（0, 1, π 相关）
 */

#include "tlibc_everything.h"
#include "tlibc_test.h"
#include "math.h"

TEST_DEFINE_COUNTERS();

/* ── 浮点近似断言 ──────────────────────────────────── */

#define ASSERT_NEAR(val, expected, eps) \
    do { \
        double diff_ = (double)(val) - (double)(expected); \
        if (diff_ < 0) diff_ = -diff_; \
        if (diff_ > (double)(eps)) { \
            __printf(_T_RED "FAIL" _T_RESET "\n    %s:%d: " \
                     "%s = %f, expected %f (diff %f > %f)\n", \
                     __FILE__, __LINE__, \
                     #val, (double)(val), (double)(expected), diff_, (double)(eps)); \
            __test_failed++; \
            return; \
        } \
    } while (0)

#define ASSERT_DBL_EQ(val, expected)  ASSERT_NEAR(val, expected, 1e-10)

/* ================================================================== */
/*  sqrt                                                               */
/* ================================================================== */

static void test_sqrt_basic(void)
{
    TEST_START("sqrt basic");
    ASSERT_DBL_EQ(sqrt(0.0), 0.0);
    ASSERT_DBL_EQ(sqrt(1.0), 1.0);
    ASSERT_DBL_EQ(sqrt(4.0), 2.0);
    ASSERT_DBL_EQ(sqrt(9.0), 3.0);
    ASSERT_DBL_EQ(sqrt(100.0), 10.0);
    TEST_PASS();
}

static void test_sqrt_irrational(void)
{
    TEST_START("sqrt irrational");
    ASSERT_NEAR(sqrt(2.0), 1.4142135623730951, 1e-14);
    ASSERT_NEAR(sqrt(3.0), 1.7320508075688772, 1e-14);
    ASSERT_NEAR(sqrt(0.25), 0.5, 1e-14);
    ASSERT_NEAR(sqrt(10000.0), 100.0, 1e-12);
    TEST_PASS();
}

static void test_sqrt_special(void)
{
    TEST_START("sqrt special");
    /* 负数返回 0 */
    ASSERT_DBL_EQ(sqrt(-1.0), 0.0);
    ASSERT_DBL_EQ(sqrt(-0.0), 0.0);
    TEST_PASS();
}

/* ================================================================== */
/*  fabs                                                               */
/* ================================================================== */

static void test_fabs(void)
{
    TEST_START("fabs");
    ASSERT_DBL_EQ(fabs(0.0), 0.0);
    ASSERT_DBL_EQ(fabs(1.0), 1.0);
    ASSERT_DBL_EQ(fabs(-1.0), 1.0);
    ASSERT_DBL_EQ(fabs(3.14159), 3.14159);
    ASSERT_DBL_EQ(fabs(-3.14159), 3.14159);
    TEST_PASS();
}

/* ================================================================== */
/*  ceil / floor / round / trunc                                       */
/* ================================================================== */

static void test_ceil(void)
{
    TEST_START("ceil");
    ASSERT_DBL_EQ(ceil(0.0), 0.0);
    ASSERT_DBL_EQ(ceil(1.0), 1.0);
    ASSERT_DBL_EQ(ceil(1.3), 2.0);
    ASSERT_DBL_EQ(ceil(1.7), 2.0);
    ASSERT_DBL_EQ(ceil(-1.3), -1.0);
    ASSERT_DBL_EQ(ceil(-1.7), -1.0);
    ASSERT_DBL_EQ(ceil(-0.3), 0.0);
    ASSERT_DBL_EQ(ceil(0.3), 1.0);
    TEST_PASS();
}

static void test_floor(void)
{
    TEST_START("floor");
    ASSERT_DBL_EQ(floor(0.0), 0.0);
    ASSERT_DBL_EQ(floor(1.0), 1.0);
    ASSERT_DBL_EQ(floor(1.3), 1.0);
    ASSERT_DBL_EQ(floor(1.7), 1.0);
    ASSERT_DBL_EQ(floor(-1.3), -2.0);
    ASSERT_DBL_EQ(floor(-1.7), -2.0);
    ASSERT_DBL_EQ(floor(-0.3), -1.0);
    ASSERT_DBL_EQ(floor(0.3), 0.0);
    TEST_PASS();
}

static void test_round(void)
{
    TEST_START("round");
    ASSERT_DBL_EQ(round(0.0), 0.0);
    ASSERT_DBL_EQ(round(1.0), 1.0);
    ASSERT_DBL_EQ(round(1.3), 1.0);
    ASSERT_DBL_EQ(round(1.5), 2.0);
    ASSERT_DBL_EQ(round(1.7), 2.0);
    ASSERT_DBL_EQ(round(-1.3), -1.0);
    ASSERT_DBL_EQ(round(-1.5), -2.0);
    ASSERT_DBL_EQ(round(-1.7), -2.0);
    ASSERT_DBL_EQ(round(0.3), 0.0);
    ASSERT_DBL_EQ(round(0.5), 1.0);
    ASSERT_DBL_EQ(round(-0.3), 0.0);
    ASSERT_DBL_EQ(round(-0.5), -1.0);
    TEST_PASS();
}

static void test_trunc(void)
{
    TEST_START("trunc");
    ASSERT_DBL_EQ(trunc(0.0), 0.0);
    ASSERT_DBL_EQ(trunc(1.0), 1.0);
    ASSERT_DBL_EQ(trunc(1.3), 1.0);
    ASSERT_DBL_EQ(trunc(1.7), 1.0);
    ASSERT_DBL_EQ(trunc(-1.3), -1.0);
    ASSERT_DBL_EQ(trunc(-1.7), -1.0);
    ASSERT_DBL_EQ(trunc(-0.3), 0.0);
    ASSERT_DBL_EQ(trunc(0.3), 0.0);
    TEST_PASS();
}

/* ================================================================== */
/*  fmod                                                               */
/* ================================================================== */

static void test_fmod(void)
{
    TEST_START("fmod");
    ASSERT_DBL_EQ(fmod(5.0, 2.0), 1.0);
    ASSERT_DBL_EQ(fmod(10.0, 3.0), 1.0);
    ASSERT_DBL_EQ(fmod(10.0, 5.0), 0.0);
    ASSERT_DBL_EQ(fmod(7.5, 2.5), 0.0);
    ASSERT_DBL_EQ(fmod(-7.5, 2.5), -0.0);
    ASSERT_DBL_EQ(fmod(3.14159, 1.0), 0.14159);
    TEST_PASS();
}

/* ================================================================== */
/*  isqrt                                                              */
/* ================================================================== */

static void test_isqrt(void)
{
    TEST_START("isqrt");
    TEST_ASSERT_EQ(isqrt(0), 0, "%lld");
    TEST_ASSERT_EQ(isqrt(1), 1, "%lld");
    TEST_ASSERT_EQ(isqrt(4), 2, "%lld");
    TEST_ASSERT_EQ(isqrt(10), 3, "%lld");
    TEST_ASSERT_EQ(isqrt(16), 4, "%lld");
    TEST_ASSERT_EQ(isqrt(99), 9, "%lld");
    TEST_ASSERT_EQ(isqrt(100), 10, "%lld");
    TEST_ASSERT_EQ(isqrt(1000000), 1000, "%lld");
    TEST_ASSERT_EQ(isqrt(2147395599), 46339, "%lld");  /* floor(sqrt(INT_MAX)) */
    TEST_ASSERT_EQ(isqrt(-1), 0, "%lld");
    TEST_PASS();
}

/* ================================================================== */
/*  sin / cos / tan                                                    */
/* ================================================================== */

static void test_sin(void)
{
    TEST_START("sin");
    ASSERT_NEAR(sin(0.0), 0.0, 1e-14);
    ASSERT_NEAR(sin(M_PI_2), 1.0, 1e-10);
    ASSERT_NEAR(sin(M_PI), 0.0, 1e-10);
    ASSERT_NEAR(sin(2.0 * M_PI), 0.0, 1e-10);
    ASSERT_NEAR(sin(M_PI_4), 0.7071067811865476, 1e-10);
    ASSERT_NEAR(sin(-M_PI_2), -1.0, 1e-10);
    TEST_PASS();
}

static void test_cos(void)
{
    TEST_START("cos");
    ASSERT_NEAR(cos(0.0), 1.0, 1e-14);
    ASSERT_NEAR(cos(M_PI_2), 0.0, 1e-10);
    ASSERT_NEAR(cos(M_PI), -1.0, 1e-10);
    ASSERT_NEAR(cos(2.0 * M_PI), 1.0, 1e-10);
    ASSERT_NEAR(cos(M_PI_4), 0.7071067811865476, 1e-10);
    ASSERT_NEAR(cos(-M_PI), -1.0, 1e-10);
    TEST_PASS();
}

static void test_tan(void)
{
    TEST_START("tan");
    ASSERT_NEAR(tan(0.0), 0.0, 1e-14);
    ASSERT_NEAR(tan(M_PI_4), 1.0, 1e-10);
    ASSERT_NEAR(tan(-M_PI_4), -1.0, 1e-10);
    /* tan(0.1) * cos(0.1) ≈ sin(0.1) */
    ASSERT_NEAR(tan(0.1) * cos(0.1), sin(0.1), 1e-10);
    TEST_PASS();
}

static void test_sin_cos_identity(void)
{
    TEST_START("sin²+cos²=1");
    ASSERT_NEAR(sin(0.5) * sin(0.5) + cos(0.5) * cos(0.5), 1.0, 1e-10);
    ASSERT_NEAR(sin(1.0) * sin(1.0) + cos(1.0) * cos(1.0), 1.0, 1e-10);
    ASSERT_NEAR(sin(2.0) * sin(2.0) + cos(2.0) * cos(2.0), 1.0, 1e-10);
    TEST_PASS();
}

/* ================================================================== */
/*  atan / atan2                                                       */
/* ================================================================== */

static void test_atan(void)
{
    TEST_START("atan");
    ASSERT_NEAR(atan(0.0), 0.0, 1e-14);
    ASSERT_NEAR(atan(1.0), M_PI_4, 1e-10);
    ASSERT_NEAR(atan(-1.0), -M_PI_4, 1e-10);
    ASSERT_NEAR(atan(0.5773502691896258), 0.5235987755982989, 1e-10);  /* atan(1/√3) = π/6 */
    ASSERT_NEAR(atan(1000.0), M_PI_2 - 0.001, 1e-6);
    ASSERT_NEAR(atan(-1000.0), -M_PI_2 + 0.001, 1e-6);
    TEST_PASS();
}

static void test_atan2(void)
{
    TEST_START("atan2");
    ASSERT_NEAR(atan2(0.0, 1.0), 0.0, 1e-14);
    ASSERT_NEAR(atan2(1.0, 0.0), M_PI_2, 1e-10);
    ASSERT_NEAR(atan2(-1.0, 0.0), -M_PI_2, 1e-10);
    ASSERT_NEAR(atan2(0.0, -1.0), M_PI, 1e-10);
    ASSERT_NEAR(atan2(1.0, 1.0), M_PI_4, 1e-10);
    ASSERT_NEAR(atan2(-1.0, -1.0), -3.0 * M_PI_4, 1e-10);
    ASSERT_NEAR(atan2(0.0, 0.0), 0.0, 1e-14);
    TEST_PASS();
}

/* ================================================================== */
/*  exp                                                                */
/* ================================================================== */

static void test_exp(void)
{
    TEST_START("exp");
    ASSERT_NEAR(exp(0.0), 1.0, 1e-14);
    ASSERT_NEAR(exp(1.0), M_E, 1e-10);
    ASSERT_NEAR(exp(-1.0), 1.0 / M_E, 1e-10);
    ASSERT_NEAR(exp(2.0), 7.389056098930650, 1e-8);
    ASSERT_NEAR(exp(-2.0), 0.1353352832366127, 1e-10);
    ASSERT_NEAR(exp(0.5), 1.6487212707001282, 1e-10);
    TEST_PASS();
}

/* ================================================================== */
/*  log / log10                                                        */
/* ================================================================== */

static void test_log(void)
{
    TEST_START("log");
    ASSERT_NEAR(log(1.0), 0.0, 1e-14);
    ASSERT_NEAR(log(M_E), 1.0, 1e-10);
    ASSERT_NEAR(log(10.0), M_LN10, 1e-10);
    ASSERT_NEAR(log(100.0), 2.0 * M_LN10, 1e-8);
    ASSERT_NEAR(log(0.5), -M_LN2, 1e-10);
    ASSERT_NEAR(log(2.0), M_LN2, 1e-10);
    TEST_PASS();
}

static void test_log_special(void)
{
    TEST_START("log special");
    /* log(≤0) 返回 -HUGE_VAL */
    ASSERT_DBL_EQ(log(0.0), -HUGE_VAL);
    ASSERT_DBL_EQ(log(-1.0), -HUGE_VAL);
    TEST_PASS();
}

static void test_log10(void)
{
    TEST_START("log10");
    ASSERT_NEAR(log10(1.0), 0.0, 1e-14);
    ASSERT_NEAR(log10(10.0), 1.0, 1e-10);
    ASSERT_NEAR(log10(100.0), 2.0, 1e-10);
    ASSERT_NEAR(log10(1000.0), 3.0, 1e-10);
    ASSERT_NEAR(log10(0.1), -1.0, 1e-10);
    TEST_PASS();
}

/* ================================================================== */
/*  pow                                                                */
/* ================================================================== */

static void test_pow_int(void)
{
    TEST_START("pow (integer)");
    ASSERT_DBL_EQ(pow(0.0, 0.0), 1.0);
    ASSERT_DBL_EQ(pow(0.0, 5.0), 0.0);
    ASSERT_DBL_EQ(pow(2.0, 0.0), 1.0);
    ASSERT_DBL_EQ(pow(2.0, 3.0), 8.0);
    ASSERT_DBL_EQ(pow(2.0, 10.0), 1024.0);
    ASSERT_DBL_EQ(pow(3.0, 4.0), 81.0);
    ASSERT_DBL_EQ(pow(10.0, 5.0), 100000.0);
    TEST_PASS();
}

static void test_pow_frac(void)
{
    TEST_START("pow (fractional)");
    ASSERT_NEAR(pow(4.0, 0.5), 2.0, 1e-10);
    ASSERT_NEAR(pow(9.0, 0.5), 3.0, 1e-10);
    ASSERT_NEAR(pow(27.0, 1.0/3.0), 3.0, 1e-8);
    ASSERT_NEAR(pow(M_E, 1.0), M_E, 1e-10);
    ASSERT_NEAR(pow(10.0, 0.3010), 2.0, 2e-4);
    TEST_PASS();
}

static void test_pow_neg(void)
{
    TEST_START("pow (negative exponent)");
    ASSERT_NEAR(pow(2.0, -1.0), 0.5, 1e-10);
    ASSERT_NEAR(pow(2.0, -2.0), 0.25, 1e-10);
    ASSERT_NEAR(pow(10.0, -3.0), 0.001, 1e-10);
    TEST_PASS();
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */

int main(void)
{
    TEST_BEGIN("math library tests");

    /* Phase 1 — 基础运算 */
    test_sqrt_basic();
    test_sqrt_irrational();
    test_sqrt_special();
    test_fabs();
    test_ceil();
    test_floor();
    test_round();
    test_trunc();
    test_fmod();
    test_isqrt();

    /* Phase 2 — 三角 */
    test_sin();
    test_cos();
    test_tan();
    test_sin_cos_identity();
    test_atan();
    test_atan2();

    /* Phase 2 — 指数/对数/幂 */
    test_exp();
    test_log();
    test_log_special();
    test_log10();
    test_pow_int();
    test_pow_frac();
    test_pow_neg();

    return TEST_END();
}
