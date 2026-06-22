#include "tlibc_test.h"

TEST_DEFINE_COUNTERS();

/* ========== strlen ========== */

void test_strlen_empty(void) {
    TEST_START("strlen empty");
    TEST_ASSERT_EQ(strlen(""), 0, "%d");
    TEST_PASS();
}

void test_strlen_basic(void) {
    TEST_START("strlen basic");
    TEST_ASSERT_EQ(strlen("h"), 1, "%d");
    TEST_ASSERT_EQ(strlen("hello"), 5, "%d");
    TEST_ASSERT_EQ(strlen("hello world"), 11, "%d");
    TEST_PASS();
}

void test_strlen_long(void) {
    TEST_START("strlen long (255 chars)");
    char buf[256];
    memset(buf, 'a', 255);
    buf[255] = '\0';
    TEST_ASSERT_EQ(strlen(buf), 255, "%d");
    TEST_PASS();
}

/* ========== strcpy ========== */

void test_strcpy_basic(void) {
    TEST_START("strcpy basic + return value");
    char buf[64];
    memset(buf, 0, 64);
    char *ret = strcpy(buf, "hello");
    TEST_ASSERT_STR_EQ(buf, "hello");
    /* strcpy 应返回 dest 指针，当前实现返回 src（BUG） */
    TEST_ASSERT_EQ((unsigned long)ret, (unsigned long)buf, "%p");
    TEST_PASS();
}

void test_strcpy_empty(void) {
    TEST_START("strcpy empty string");
    char buf[64];
    memset(buf, 'x', 64);
    char *ret = strcpy(buf, "");
    TEST_ASSERT_EQ(buf[0], '\0', "%d");
    TEST_ASSERT_EQ((unsigned long)ret, (unsigned long)buf, "%p");
    TEST_PASS();
}

/* ========== strcmp ========== */

void test_strcmp_equal(void) {
    TEST_START("strcmp equal");
    TEST_ASSERT_EQ(strcmp("", ""), 0, "%d");
    TEST_ASSERT_EQ(strcmp("hello", "hello"), 0, "%d");
    TEST_ASSERT_EQ(strcmp("a long string", "a long string"), 0, "%d");
    TEST_PASS();
}

void test_strcmp_diff(void) {
    TEST_START("strcmp different");
    TEST_ASSERT(strcmp("a", "b") < 0, "'a' < 'b'");
    TEST_ASSERT(strcmp("b", "a") > 0, "'b' > 'a'");
    TEST_ASSERT(strcmp("abcd", "abce") < 0, "'abcd' < 'abce'");
    TEST_PASS();
}

void test_strcmp_case(void) {
    TEST_START("strcmp case sensitivity");
    TEST_ASSERT(strcmp("ABC", "abc") < 0, "'ABC' < 'abc' (ASCII)");
    TEST_PASS();
}

void test_strcmp_prefix(void) {
    TEST_START("strcmp prefix mismatch");
    TEST_ASSERT(strcmp("abc", "abcd") != 0, "shorter vs longer");
    TEST_ASSERT(strcmp("abc", "abcd") < 0, "shorter < longer");
    TEST_PASS();
}

/* ========== strncmp ========== */

void test_strncmp_equal(void) {
    TEST_START("strncmp equal (n)");
    TEST_ASSERT_EQ(strncmp("abcde", "abcde", 5), 0, "%d");
    TEST_ASSERT_EQ(strncmp("abcde", "abcxx", 3), 0, "%d");
    TEST_PASS();
}

void test_strncmp_diff(void) {
    TEST_START("strncmp different (n)");
    TEST_ASSERT(strncmp("abcde", "abdde", 3) < 0, "'c' < 'd'");
    TEST_PASS();
}

void test_strncmp_nzero(void) {
    TEST_START("strncmp n=0");
    TEST_ASSERT_EQ(strncmp("anything", "nothing", 0), 0, "%d");
    TEST_PASS();
}

/* ========== strcat ========== */

void test_strcat_basic(void) {
    TEST_START("strcat basic");
    char buf[64];
    memset(buf, 0, 64);
    strcpy(buf, "hello ");
    strcat(buf, "world");
    TEST_ASSERT_STR_EQ(buf, "hello world");
    TEST_PASS();
}

/* ========== memcpy ========== */

void test_memcpy_basic(void) {
    TEST_START("memcpy basic");
    char src[] = "hello world";
    char dst[32];
    memset(dst, 0, 32);
    memcpy(dst, src, 12);
    TEST_ASSERT_EQ(dst[0], 'h', "%d");
    TEST_ASSERT_EQ(dst[4], 'o', "%d");
    TEST_ASSERT_EQ(dst[11], '\0', "%d");
    TEST_PASS();
}

void test_memcpy_zero(void) {
    TEST_START("memcpy zero bytes");
    char src[] = "hello";
    char dst[32];
    memset(dst, 0xAA, 32);
    memcpy(dst, src, 0);
    TEST_ASSERT_EQ((unsigned char)dst[0], 0xAA, "%02x");
    TEST_PASS();
}

void test_memcpy_partial(void) {
    TEST_START("memcpy partial overlap");
    char buf[32] = "abcdefghijklmnop";
    memcpy(buf + 4, buf, 4);   /* "abcd" → buf+4 */
    TEST_ASSERT_EQ(buf[4], 'a', "%d");
    TEST_ASSERT_EQ(buf[7], 'd', "%d");
    TEST_PASS();
}

/* ========== strchr ========== */

void test_strchr_found(void) {
    TEST_START("strchr found");
    char *s = "hello world";
    char *p = strchr(s, 'w');
    TEST_ASSERT(p != NULL, "strchr should find 'w'");
    TEST_ASSERT_STR_EQ(p, "world");
    TEST_PASS();
}

void test_strchr_notfound(void) {
    TEST_START("strchr not found");
    char *p = strchr("hello", 'z');
    TEST_ASSERT(p == NULL, "strchr('hello', 'z') should return NULL");
    TEST_PASS();
}

void test_strchr_first(void) {
    TEST_START("strchr first occurrence");
    char *p = strchr("hello", 'l');
    TEST_ASSERT(p != NULL, "strchr should find 'l'");
    TEST_ASSERT_STR_EQ(p, "llo");
    TEST_PASS();
}

void test_strchr_terminator(void) {
    TEST_START("strchr find null terminator");
    char *s = "hello";
    char *p = strchr(s, '\0');
    TEST_ASSERT(p != NULL, "strchr should find null terminator");
    TEST_ASSERT_EQ(*p, '\0', "%d");
    TEST_ASSERT_EQ((unsigned long)p, (unsigned long)(s + 5), "%p");
    TEST_PASS();
}

/* ========== strrchr ========== */

void test_strrchr_last(void) {
    TEST_START("strrchr last occurrence");
    char *s = "hello world";
    char *p = strrchr(s, 'l');
    TEST_ASSERT(p != NULL, "strrchr should find 'l'");
    TEST_ASSERT_STR_EQ(p, "ld");
    TEST_PASS();
}

void test_strrchr_notfound(void) {
    TEST_START("strrchr not found");
    char *p = strrchr("hello", 'z');
    TEST_ASSERT(p == NULL, "strrchr('hello', 'z') should return NULL");
    TEST_PASS();
}

void test_strrchr_single(void) {
    TEST_START("strrchr single char");
    char *p = strrchr("hello", 'h');
    TEST_ASSERT(p != NULL, "strrchr('hello', 'h')");
    TEST_ASSERT_STR_EQ(p, "hello");
    TEST_PASS();
}

/* ========== strncpy ========== */

void test_strncpy_basic(void) {
    TEST_START("strncpy basic");
    char buf[16];
    memset(buf, 0xAA, 16);
    strncpy(buf, "hello", 16);
    TEST_ASSERT_STR_EQ(buf, "hello");
    /* 剩余部分应填充 \0 */
    TEST_ASSERT_EQ(buf[5], '\0', "%d");
    TEST_PASS();
}

void test_strncpy_truncate(void) {
    TEST_START("strncpy truncate (no null pad when src longer than n)");
    char buf[4];
    memset(buf, 0xBB, 4);
    strncpy(buf, "hello world", 4);
    TEST_ASSERT_EQ(buf[0], 'h', "%d");
    TEST_ASSERT_EQ(buf[1], 'e', "%d");
    TEST_ASSERT_EQ(buf[2], 'l', "%d");
    TEST_ASSERT_EQ(buf[3], 'l', "%d");  /* src 比 n 长，不补 \0 */
    TEST_PASS();
}

/* ========== itoa ========== */

void test_itoa_decimal(void) {
    TEST_START("itoa decimal");
    char buf[32];
    itoa(12345, buf, 10);
    TEST_ASSERT_STR_EQ(buf, "12345");
    TEST_PASS();
}

void test_itoa_zero(void) {
    TEST_START("itoa zero");
    char buf[32];
    itoa(0, buf, 10);
    TEST_ASSERT_STR_EQ(buf, "0");
    TEST_PASS();
}

void test_itoa_negative(void) {
    TEST_START("itoa negative");
    char buf[32];
    itoa(-42, buf, 10);
    TEST_ASSERT_STR_EQ(buf, "-42");
    TEST_PASS();
}

void test_itoa_hex(void) {
    TEST_START("itoa hex");
    char buf[32];
    itoa(255, buf, 16);
    TEST_ASSERT_STR_EQ(buf, "FF");
    TEST_PASS();
}

void test_itoa_octal(void) {
    TEST_START("itoa octal");
    char buf[32];
    itoa(64, buf, 8);
    TEST_ASSERT_STR_EQ(buf, "100");
    TEST_PASS();
}

/* ========== strerror ========== */

void test_strerror_zero(void) {
    TEST_START("strerror(0)");
    TEST_ASSERT_STR_EQ(strerror(0), "Success");
    TEST_PASS();
}

void test_strerror_known(void) {
    TEST_START("strerror known errno");
    TEST_ASSERT_STR_EQ(strerror(2), "No such file or directory");
    TEST_ASSERT_STR_EQ(strerror(13), "Permission denied");
    TEST_PASS();
}

void test_strerror_unknown(void) {
    TEST_START("strerror unknown errno");
    char *msg = strerror(999);
    TEST_ASSERT(msg != NULL, "strerror(999) should return something");
    TEST_PASS();
}

/* ========== tlibc_strtoul ========== */

void test_strtoul_basic(void) {
    TEST_START("strtoul basic");
    TEST_ASSERT_EQ(tlibc_strtoul("12345"), 12345UL, "%lu");
    TEST_ASSERT_EQ(tlibc_strtoul("0"), 0UL, "%lu");
    TEST_ASSERT_EQ(tlibc_strtoul("1"), 1UL, "%lu");
    TEST_PASS();
}

void test_strtoul_large(void) {
    TEST_START("strtoul large");
    TEST_ASSERT_EQ(tlibc_strtoul("999999999999"), 999999999999UL, "%lu");
    TEST_PASS();
}

/* ========== main ========== */

int main(void) {
    TEST_BEGIN("string library tests");

    test_strlen_empty();
    test_strlen_basic();
    test_strlen_long();

    test_strcpy_basic();
    test_strcpy_empty();

    test_strcmp_equal();
    test_strcmp_diff();
    test_strcmp_case();
    test_strcmp_prefix();

    test_strncmp_equal();
    test_strncmp_diff();
    test_strncmp_nzero();

    test_strcat_basic();

    test_memcpy_basic();
    test_memcpy_zero();
    test_memcpy_partial();

    test_strchr_found();
    test_strchr_notfound();
    test_strchr_first();
    test_strchr_terminator();

    test_strrchr_last();
    test_strrchr_notfound();
    test_strrchr_single();

    test_strncpy_basic();
    test_strncpy_truncate();

    test_itoa_decimal();
    test_itoa_zero();
    test_itoa_negative();
    test_itoa_hex();
    test_itoa_octal();

    test_strerror_zero();
    test_strerror_known();
    test_strerror_unknown();

    test_strtoul_basic();
    test_strtoul_large();

    return TEST_END();
}
