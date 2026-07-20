/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * test_scanf.c — scanf / sscanf 综合测试
 *
 * 覆盖：
 *   - 基本格式：%d %x %s %c %u %i %n %%
 *   - 宽度限定、赋值抑制、长度修饰（%ld / %lld）
 *   - 命令解析模式（fb_console 场景：line/color/circle/text/rect）
 *   - 边界：空字符串、INT_MIN/MAX、空白处理、NULL 输入
 *
 * 用法：build/output/test_scanf
 */

#include "tlibc_everything.h"
#include "tlibc_test.h"
#include "string.h"

TEST_DEFINE_COUNTERS();

/* ════════════════════════════════════════════════════════════════ */
/*  %d                                                             */
/* ════════════════════════════════════════════════════════════════ */

static void test_d_positive(void)
{
    int v;
    TEST_START("%d positive");
    TEST_ASSERT_EQ(sscanf("42", "%d", &v), 1, "%d");
    TEST_ASSERT_EQ(v, 42, "%d");
    TEST_PASS();
}

static void test_d_negative(void)
{
    int v;
    TEST_START("%d negative");
    TEST_ASSERT_EQ(sscanf("-42", "%d", &v), 1, "%d");
    TEST_ASSERT_EQ(v, -42, "%d");
    TEST_PASS();
}

static void test_d_zero(void)
{
    int v;
    TEST_START("%d zero");
    TEST_ASSERT_EQ(sscanf("0", "%d", &v), 1, "%d");
    TEST_ASSERT_EQ(v, 0, "%d");
    TEST_PASS();
}

static void test_d_intmax(void)
{
    int v;
    TEST_START("%d INT_MAX");
    TEST_ASSERT_EQ(sscanf("2147483647", "%d", &v), 1, "%d");
    TEST_ASSERT_EQ(v, 2147483647, "%d");
    TEST_PASS();
}

static void test_d_intmin(void)
{
    int v;
    TEST_START("%d INT_MIN");
    TEST_ASSERT_EQ(sscanf("-2147483648", "%d", &v), 1, "%d");
    TEST_ASSERT_EQ(v, -2147483647 - 1, "%d");
    TEST_PASS();
}

static void test_d_leading_ws(void)
{
    int v;
    TEST_START("%d leading whitespace");
    TEST_ASSERT_EQ(sscanf("   42", "%d", &v), 1, "%d");
    TEST_ASSERT_EQ(v, 42, "%d");
    TEST_PASS();
}

static void test_d_plus(void)
{
    int v;
    TEST_START("%d + sign");
    TEST_ASSERT_EQ(sscanf("+99", "%d", &v), 1, "%d");
    TEST_ASSERT_EQ(v, 99, "%d");
    TEST_PASS();
}

static void test_d_width(void)
{
    int v;
    TEST_START("%d width 3");
    TEST_ASSERT_EQ(sscanf("12345", "%3d", &v), 1, "%d");
    TEST_ASSERT_EQ(v, 123, "%d");
    TEST_PASS();
}

static void test_d_suppress(void)
{
    TEST_START("%d suppression %%*d");
    TEST_ASSERT_EQ(sscanf("42", "%*d"), 0, "%d");
    TEST_PASS();
}

/* ════════════════════════════════════════════════════════════════ */
/*  %x                                                             */
/* ════════════════════════════════════════════════════════════════ */

static void test_x_basic(void)
{
    unsigned int u;
    TEST_START("%x basic");
    TEST_ASSERT_EQ(sscanf("ff", "%x", &u), 1, "%d");
    TEST_ASSERT_EQ(u, 255, "%u");
    TEST_PASS();
}

static void test_x_uppercase(void)
{
    unsigned int u;
    TEST_START("%x uppercase input");
    TEST_ASSERT_EQ(sscanf("FF", "%x", &u), 1, "%d");
    TEST_ASSERT_EQ(u, 255, "%u");
    TEST_PASS();
}

static void test_x_0x_prefix(void)
{
    unsigned int u;
    TEST_START("%x with 0x prefix");
    TEST_ASSERT_EQ(sscanf("0x1a", "%x", &u), 1, "%d");
    TEST_ASSERT_EQ(u, 26, "%u");
    TEST_PASS();
}

static void test_x_width(void)
{
    unsigned int u;
    TEST_START("%x width 3");
    TEST_ASSERT_EQ(sscanf("abc123", "%3x", &u), 1, "%d");
    TEST_ASSERT_EQ(u, 0xabc, "%x");
    TEST_PASS();
}

/* ════════════════════════════════════════════════════════════════ */
/*  %s                                                             */
/* ════════════════════════════════════════════════════════════════ */

static void test_s_one_word(void)
{
    char buf[64];
    TEST_START("%s one word");
    TEST_ASSERT_EQ(sscanf("hello", "%s", buf), 1, "%d");
    TEST_ASSERT_STR_EQ(buf, "hello");
    TEST_PASS();
}

static void test_s_stops_at_space(void)
{
    char buf[64];
    TEST_START("%s stops at space");
    TEST_ASSERT_EQ(sscanf("hello world", "%s", buf), 1, "%d");
    TEST_ASSERT_STR_EQ(buf, "hello");
    TEST_PASS();
}

static void test_s_width(void)
{
    char buf[64];
    TEST_START("%s width 5");
    TEST_ASSERT_EQ(sscanf("abcdefgh", "%5s", buf), 1, "%d");
    TEST_ASSERT_STR_EQ(buf, "abcde");
    TEST_PASS();
}

/* ════════════════════════════════════════════════════════════════ */
/*  %c                                                             */
/* ════════════════════════════════════════════════════════════════ */

static void test_c_single(void)
{
    char c;
    TEST_START("%c single");
    TEST_ASSERT_EQ(sscanf("A", "%c", &c), 1, "%d");
    TEST_ASSERT_EQ(c, 'A', "%c");
    TEST_PASS();
}

static void test_c_no_skip_ws(void)
{
    char c;
    TEST_START("%c does not skip whitespace");
    TEST_ASSERT_EQ(sscanf("  ", "%c", &c), 1, "%d");
    TEST_ASSERT_EQ(c, ' ', "%c");
    TEST_PASS();
}

static void test_c_consecutive(void)
{
    char c1, c2, c3;
    TEST_START("%c consecutive — keeps spaces");
    TEST_ASSERT_EQ(sscanf("A B C", "%c%c%c", &c1, &c2, &c3), 3, "%d");
    TEST_ASSERT_EQ(c1, 'A', "%c");
    TEST_ASSERT_EQ(c2, ' ', "%c");
    TEST_ASSERT_EQ(c3, 'B', "%c");
    TEST_PASS();
}

static void test_c_width(void)
{
    char buf[4] = {0};
    TEST_START("%c width 3");
    TEST_ASSERT_EQ(sscanf("ABC", "%3c", buf), 1, "%d");
    TEST_ASSERT_EQ(buf[0], 'A', "%c");
    TEST_ASSERT_EQ(buf[1], 'B', "%c");
    TEST_ASSERT_EQ(buf[2], 'C', "%c");
    TEST_PASS();
}

/* ════════════════════════════════════════════════════════════════ */
/*  %u                                                             */
/* ════════════════════════════════════════════════════════════════ */

static void test_u_basic(void)
{
    unsigned int u;
    TEST_START("%u basic");
    TEST_ASSERT_EQ(sscanf("42", "%u", &u), 1, "%d");
    TEST_ASSERT_EQ(u, 42, "%u");
    TEST_PASS();
}

static void test_u_large(void)
{
    unsigned int u;
    TEST_START("%u large");
    TEST_ASSERT_EQ(sscanf("3000000000", "%u", &u), 1, "%d");
    TEST_ASSERT_EQ(u, 3000000000u, "%u");
    TEST_PASS();
}

/* ════════════════════════════════════════════════════════════════ */
/*  %i  (auto-detect base)                                         */
/* ════════════════════════════════════════════════════════════════ */

static void test_i_decimal(void)
{
    int v;
    TEST_START("%i decimal");
    TEST_ASSERT_EQ(sscanf("42", "%i", &v), 1, "%d");
    TEST_ASSERT_EQ(v, 42, "%d");
    TEST_PASS();
}

static void test_i_hex(void)
{
    int v;
    TEST_START("%i hex 0x2a");
    TEST_ASSERT_EQ(sscanf("0x2a", "%i", &v), 1, "%d");
    TEST_ASSERT_EQ(v, 42, "%d");
    TEST_PASS();
}

static void test_i_octal(void)
{
    int v;
    TEST_START("%i octal 052");
    TEST_ASSERT_EQ(sscanf("052", "%i", &v), 1, "%d");
    TEST_ASSERT_EQ(v, 42, "%d");
    TEST_PASS();
}

/* ════════════════════════════════════════════════════════════════ */
/*  %n  / %%                                                        */
/* ════════════════════════════════════════════════════════════════ */

static void test_n_with_s(void)
{
    char buf[32];
    int n;
    TEST_START("%n after %%s");
    sscanf("hello world", "%s%n", buf, &n);
    TEST_ASSERT_EQ(n, 5, "%d");
    TEST_ASSERT_STR_EQ(buf, "hello");
    TEST_PASS();
}

static void test_n_with_d(void)
{
    int v, n;
    TEST_START("%n after %%d");
    TEST_ASSERT_EQ(sscanf("42", "%d%n", &v, &n), 1, "%d");
    TEST_ASSERT_EQ(v, 42, "%d");
    TEST_ASSERT_EQ(n, 2, "%d");
    TEST_PASS();
}

static void test_literal_pct(void)
{
    int v;
    TEST_START("%% literal");
    TEST_ASSERT_EQ(sscanf("50%", "%d%%", &v), 1, "%d");
    TEST_ASSERT_EQ(v, 50, "%d");
    TEST_PASS();
}

/* ════════════════════════════════════════════════════════════════ */
/*  命令分发场景 (模拟 fb_console)                                  */
/* ════════════════════════════════════════════════════════════════ */

static void test_cmd_line(void)
{
    int x1, y1, x2, y2;
    TEST_START("line 4 × %%d");
    TEST_ASSERT_EQ(sscanf("line 10 20 100 200",
                   "line %d %d %d %d", &x1, &y1, &x2, &y2), 4, "%d");
    TEST_ASSERT_EQ(x1, 10, "%d");
    TEST_ASSERT_EQ(y1, 20, "%d");
    TEST_ASSERT_EQ(x2, 100, "%d");
    TEST_ASSERT_EQ(y2, 200, "%d");
    TEST_PASS();
}

static void test_cmd_color(void)
{
    unsigned int color;
    TEST_START("color %%x");
    TEST_ASSERT_EQ(sscanf("color ff00ff", "color %x", &color), 1, "%d");
    TEST_ASSERT_EQ(color, 0xff00ff, "%x");
    TEST_PASS();
}

static void test_cmd_circle(void)
{
    int x1, y1, r;
    TEST_START("circle 3 × %%d");
    TEST_ASSERT_EQ(sscanf("circle 50 60 30",
                   "circle %d %d %d", &x1, &y1, &r), 3, "%d");
    TEST_ASSERT_EQ(x1, 50, "%d");
    TEST_ASSERT_EQ(y1, 60, "%d");
    TEST_ASSERT_EQ(r,  30, "%d");
    TEST_PASS();
}

static void test_cmd_text(void)
{
    int x1, y1;
    char str[64];
    TEST_START("text %%d %%d %%63s");
    TEST_ASSERT_EQ(sscanf("text 10 20 HelloWorld",
                   "text %d %d %63s", &x1, &y1, str), 3, "%d");
    TEST_ASSERT_EQ(x1, 10, "%d");
    TEST_ASSERT_EQ(y1, 20, "%d");
    TEST_ASSERT_STR_EQ(str, "HelloWorld");
    TEST_PASS();
}

static void test_cmd_rect(void)
{
    int x1, y1, x2, y2;
    TEST_START("rect 4 × %%d");
    TEST_ASSERT_EQ(sscanf("rect 0 0 799 599",
                   "rect %d %d %d %d", &x1, &y1, &x2, &y2), 4, "%d");
    TEST_ASSERT_EQ(x1, 0, "%d");
    TEST_ASSERT_EQ(y1, 0, "%d");
    TEST_ASSERT_EQ(x2, 799, "%d");
    TEST_ASSERT_EQ(y2, 599, "%d");
    TEST_PASS();
}

/* ════════════════════════════════════════════════════════════════ */
/*  多字段组合                                                      */
/* ════════════════════════════════════════════════════════════════ */

static void test_mixed_d_s_x(void)
{
    int a;
    unsigned int x;
    char s[64];
    TEST_START("%d %%s %%x mixed");
    TEST_ASSERT_EQ(sscanf("42 hello ff", "%d %s %x", &a, s, &x), 3, "%d");
    TEST_ASSERT_EQ(a, 42, "%d");
    TEST_ASSERT_STR_EQ(s, "hello");
    TEST_ASSERT_EQ(x, 255, "%u");
    TEST_PASS();
}

static void test_multi_field_suppress(void)
{
    int a, c;
    TEST_START("multi-field %%*d suppress");
    TEST_ASSERT_EQ(sscanf("10 20 30", "%d %*d %d", &a, &c), 2, "%d");
    TEST_ASSERT_EQ(a, 10, "%d");
    TEST_ASSERT_EQ(c, 30, "%d");
    TEST_PASS();
}

static void test_multi_d(void)
{
    int a, b, c;
    TEST_START("three %%d fields");
    TEST_ASSERT_EQ(sscanf("10 20 30", "%d %d %d", &a, &b, &c), 3, "%d");
    TEST_ASSERT_EQ(a, 10, "%d");
    TEST_ASSERT_EQ(b, 20, "%d");
    TEST_ASSERT_EQ(c, 30, "%d");
    TEST_PASS();
}

static void test_multi_x(void)
{
    unsigned int x, y;
    TEST_START("two %%x fields");
    TEST_ASSERT_EQ(sscanf("ff aa", "%x %x", &x, &y), 2, "%d");
    TEST_ASSERT_EQ(x, 255, "%u");
    TEST_ASSERT_EQ(y, 170, "%u");
    TEST_PASS();
}

/* ════════════════════════════════════════════════════════════════ */
/*  边界情况                                                        */
/* ════════════════════════════════════════════════════════════════ */

static void test_edge_empty(void)
{
    int v;
    TEST_START("empty string returns -1");
    TEST_ASSERT_EQ(sscanf("", "%d", &v), -1, "%d");
    TEST_PASS();
}

static void test_edge_ws_only(void)
{
    int v;
    TEST_START("whitespace only — matching failure (0)");
    TEST_ASSERT_EQ(sscanf("   ", "%d", &v), 0, "%d");
    TEST_PASS();
}

static void test_edge_null_str(void)
{
    int v;
    TEST_START("NULL string returns -1");
    TEST_ASSERT_EQ(sscanf(NULL, "%d", &v), -1, "%d");
    TEST_PASS();
}

static void test_edge_no_match(void)
{
    int v;
    TEST_START("abc not a number");
    TEST_ASSERT_EQ(sscanf("abc", "%d", &v), 0, "%d");
    TEST_PASS();
}

static void test_edge_partial_match(void)
{
    int v;
    TEST_START("digits then letters — partial");
    TEST_ASSERT_EQ(sscanf("42abc", "%d", &v), 1, "%d");
    TEST_ASSERT_EQ(v, 42, "%d");
    TEST_PASS();
}

/* ════════════════════════════════════════════════════════════════ */
/*  长度修饰: %ld / %lld                                           */
/* ════════════════════════════════════════════════════════════════ */

static void test_ld_long(void)
{
    long v;
    TEST_START("%ld long");
    TEST_ASSERT_EQ(sscanf("1234567890", "%ld", &v), 1, "%d");
    TEST_ASSERT_EQ(v, 1234567890L, "%ld");
    TEST_PASS();
}

static void test_lld_longlong(void)
{
    long long v;
    TEST_START("%lld long long");
    TEST_ASSERT_EQ(sscanf("9876543210", "%lld", &v), 1, "%d");
    TEST_ASSERT_EQ(v, 9876543210LL, "%lld");
    TEST_PASS();
}

/* ════════════════════════════════════════════════════════════════ */
/*  main                                                            */
/* ════════════════════════════════════════════════════════════════ */

int main(void)
{
    TEST_BEGIN("scanf / sscanf");

    /* %d */
    test_d_positive();
    test_d_negative();
    test_d_zero();
    test_d_intmax();
    test_d_intmin();
    test_d_leading_ws();
    test_d_plus();
    test_d_width();
    test_d_suppress();

    /* %x */
    test_x_basic();
    test_x_uppercase();
    test_x_0x_prefix();
    test_x_width();

    /* %s */
    test_s_one_word();
    test_s_stops_at_space();
    test_s_width();

    /* %c */
    test_c_single();
    test_c_no_skip_ws();
    test_c_consecutive();
    test_c_width();

    /* %u */
    test_u_basic();
    test_u_large();

    /* %i */
    test_i_decimal();
    test_i_hex();
    test_i_octal();

    /* %n / %% */
    test_n_with_s();
    test_n_with_d();
    test_literal_pct();

    /* command dispatch */
    test_cmd_line();
    test_cmd_color();
    test_cmd_circle();
    test_cmd_text();
    test_cmd_rect();

    /* mixed */
    test_mixed_d_s_x();
    test_multi_field_suppress();
    test_multi_d();
    test_multi_x();

    /* edge */
    test_edge_empty();
    test_edge_ws_only();
    test_edge_null_str();
    test_edge_no_match();
    test_edge_partial_match();

    /* length modifiers */
    test_ld_long();
    test_lld_longlong();

    return TEST_END();
}
