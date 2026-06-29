#include "tlibc_test.h"
#include "tlibc_compat.h"
#include "stdlib.h"

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

/* ========== strstr ========== */

void test_strstr_found_start(void) {
    TEST_START("strstr found at start");
    char *p = strstr("hello world", "hello");
    TEST_ASSERT(p != NULL, "strstr should find at start");
    TEST_ASSERT_STR_EQ(p, "hello world");
    TEST_PASS();
}

void test_strstr_found_mid(void) {
    TEST_START("strstr found in middle");
    char *p = strstr("hello world", "world");
    TEST_ASSERT(p != NULL, "strstr should find 'world'");
    TEST_ASSERT_STR_EQ(p, "world");
    TEST_PASS();
}

void test_strstr_notfound(void) {
    TEST_START("strstr not found");
    char *p = strstr("hello", "xyz");
    TEST_ASSERT(p == NULL, "strstr('hello', 'xyz') should return NULL");
    TEST_PASS();
}

void test_strstr_empty_needle(void) {
    TEST_START("strstr empty needle");
    char *s = "hello";
    char *p = strstr(s, "");
    TEST_ASSERT(p != NULL, "strstr with empty needle should return haystack");
    TEST_ASSERT_EQ((unsigned long)p, (unsigned long)s, "%p");
    TEST_PASS();
}

void test_strstr_overlap(void) {
    TEST_START("strstr overlapping");
    char *p = strstr("aaaab", "aaab");
    TEST_ASSERT(p != NULL, "strstr should handle overlapping patterns");
    TEST_ASSERT_STR_EQ(p, "aaab");
    TEST_PASS();
}

/* ========== strtok / strtok_r ========== */

void test_strtok_basic(void) {
    TEST_START("strtok basic");
    char buf[] = "a b c d";
    char *tok = strtok(buf, " ");
    TEST_ASSERT(tok != NULL, "first token");
    TEST_ASSERT_STR_EQ(tok, "a");
    tok = strtok(NULL, " ");
    TEST_ASSERT(tok != NULL, "second token");
    TEST_ASSERT_STR_EQ(tok, "b");
    tok = strtok(NULL, " ");
    TEST_ASSERT(tok != NULL, "third token");
    TEST_ASSERT_STR_EQ(tok, "c");
    tok = strtok(NULL, " ");
    TEST_ASSERT(tok != NULL, "fourth token");
    TEST_ASSERT_STR_EQ(tok, "d");
    tok = strtok(NULL, " ");
    TEST_ASSERT(tok == NULL, "no more tokens");
    TEST_PASS();
}

void test_strtok_multidelim(void) {
    TEST_START("strtok multiple delimiters");
    char buf[] = "a,,b,  c";
    char *tok = strtok(buf, ", ");
    TEST_ASSERT_STR_EQ(tok, "a");
    tok = strtok(NULL, ", ");
    TEST_ASSERT_STR_EQ(tok, "b");
    tok = strtok(NULL, ", ");
    TEST_ASSERT_STR_EQ(tok, "c");
    tok = strtok(NULL, ", ");
    TEST_ASSERT(tok == NULL, "no more tokens");
    TEST_PASS();
}

void test_strtok_nodelim(void) {
    TEST_START("strtok no delimiter found");
    char buf[] = "hello";
    char *tok = strtok(buf, " ");
    TEST_ASSERT(tok != NULL, "single token");
    TEST_ASSERT_STR_EQ(tok, "hello");
    tok = strtok(NULL, " ");
    TEST_ASSERT(tok == NULL, "no more tokens");
    TEST_PASS();
}

void test_strtok_r_basic(void) {
    TEST_START("strtok_r basic");
    char buf[] = "x y z";
    char *save;
    char *tok = strtok_r(buf, " ", &save);
    TEST_ASSERT_STR_EQ(tok, "x");
    tok = strtok_r(NULL, " ", &save);
    TEST_ASSERT_STR_EQ(tok, "y");
    tok = strtok_r(NULL, " ", &save);
    TEST_ASSERT_STR_EQ(tok, "z");
    tok = strtok_r(NULL, " ", &save);
    TEST_ASSERT(tok == NULL, "no more tokens");
    TEST_PASS();
}

/* ========== strspn ========== */

void test_strspn_all(void) {
    TEST_START("strspn all match");
    size_t n = strspn("aaaa", "a");
    TEST_ASSERT_EQ(n, 4UL, "%zu");
    TEST_PASS();
}

void test_strspn_partial(void) {
    TEST_START("strspn partial match");
    size_t n = strspn("abcde", "abc");
    TEST_ASSERT_EQ(n, 3UL, "%zu");
    TEST_PASS();
}

void test_strspn_none(void) {
    TEST_START("strspn no match");
    size_t n = strspn("xyz", "a");
    TEST_ASSERT_EQ(n, 0UL, "%zu");
    TEST_PASS();
}

void test_strspn_empty(void) {
    TEST_START("strspn empty string");
    size_t n = strspn("", "abc");
    TEST_ASSERT_EQ(n, 0UL, "%zu");
    TEST_PASS();
}

/* ========== strcspn ========== */

void test_strcspn_none(void) {
    TEST_START("strcspn no reject chars");
    size_t n = strcspn("hello", "xyz");
    TEST_ASSERT_EQ(n, 5UL, "%zu");
    TEST_PASS();
}

void test_strcspn_some(void) {
    TEST_START("strcspn some reject");
    size_t n = strcspn("hello world", " ");
    TEST_ASSERT_EQ(n, 5UL, "%zu");
    TEST_PASS();
}

void test_strcspn_first(void) {
    TEST_START("strcspn reject at first char");
    size_t n = strcspn("abc", "a");
    TEST_ASSERT_EQ(n, 0UL, "%zu");
    TEST_PASS();
}

/* ========== strpbrk ========== */

void test_strpbrk_found(void) {
    TEST_START("strpbrk found");
    char *p = strpbrk("hello world", "wr");
    TEST_ASSERT(p != NULL, "strpbrk should find 'w'");
    TEST_ASSERT_EQ(*p, 'w', "%c");
    TEST_PASS();
}

void test_strpbrk_notfound(void) {
    TEST_START("strpbrk not found");
    char *p = strpbrk("hello", "xyz");
    TEST_ASSERT(p == NULL, "strpbrk should return NULL");
    TEST_PASS();
}

void test_strpbrk_empty(void) {
    TEST_START("strpbrk empty accept set");
    char *p = strpbrk("hello", "");
    TEST_ASSERT(p == NULL, "strpbrk with empty accept should return NULL");
    TEST_PASS();
}

/* ========== memcmp ========== */

void test_memcmp_equal(void) {
    TEST_START("memcmp equal");
    TEST_ASSERT_EQ(memcmp("abc", "abc", 3), 0, "%d");
    TEST_PASS();
}

void test_memcmp_diff(void) {
    TEST_START("memcmp different");
    TEST_ASSERT(memcmp("abc", "abd", 3) != 0, "should differ");
    TEST_ASSERT(memcmp("abc", "abd", 3) < 0, "'c' < 'd'");
    TEST_PASS();
}

void test_memcmp_zero(void) {
    TEST_START("memcmp zero bytes");
    TEST_ASSERT_EQ(memcmp("abc", "xyz", 0), 0, "%d");
    TEST_PASS();
}

/* ========== strtol ========== */

void test_strtol_decimal(void) {
    TEST_START("strtol decimal");
    char *end;
    long val = strtol("12345", &end, 10);
    TEST_ASSERT_EQ(val, 12345L, "%ld");
    TEST_ASSERT_EQ(*end, '\0', "%d");
    TEST_PASS();
}

void test_strtol_hex(void) {
    TEST_START("strtol hex 0x");
    char *end;
    long val = strtol("0xFF", &end, 0);
    TEST_ASSERT_EQ(val, 255L, "%ld");
    TEST_ASSERT_EQ(*end, '\0', "%d");
    TEST_PASS();
}

void test_strtol_octal(void) {
    TEST_START("strtol octal");
    char *end;
    long val = strtol("077", &end, 0);
    TEST_ASSERT_EQ(val, 63L, "%ld");  /* 0+7*8+7=63 */
    TEST_ASSERT_EQ(*end, '\0', "%d");
    TEST_PASS();
}

void test_strtol_negative(void) {
    TEST_START("strtol negative");
    long val = strtol("-42", (void *)0, 10);
    TEST_ASSERT_EQ(val, -42L, "%ld");
    TEST_PASS();
}

void test_strtol_auto(void) {
    TEST_START("strtol auto-detect base=0 => decimal");
    long val = strtol("99", (void *)0, 0);
    TEST_ASSERT_EQ(val, 99L, "%ld");
    TEST_PASS();
}

void test_strtol_endptr(void) {
    TEST_START("strtol endptr tracking");
    char *end;
    long val = strtol("123abc", &end, 10);
    TEST_ASSERT_EQ(val, 123L, "%ld");
    TEST_ASSERT_STR_EQ(end, "abc");
    TEST_PASS();
}

/* ========== strtoul ========== */

void test_strtoul_std_basic(void) {
    TEST_START("strtoul basic");
    unsigned long val = strtoul("999", (void *)0, 10);
    TEST_ASSERT_EQ(val, 999UL, "%lu");
    TEST_PASS();
}

void test_strtoul_std_hex(void) {
    TEST_START("strtoul hex");
    unsigned long val = strtoul("0x100", (void *)0, 0);
    TEST_ASSERT_EQ(val, 256UL, "%lu");
    TEST_PASS();
}

/* ========== atoi / atol ========== */

void test_atoi_basic(void) {
    TEST_START("atoi basic");
    TEST_ASSERT_EQ(atoi("42"), 42, "%d");
    TEST_ASSERT_EQ(atoi("0"), 0, "%d");
    TEST_ASSERT_EQ(atoi("-1"), -1, "%d");
    TEST_PASS();
}

void test_atol_basic(void) {
    TEST_START("atol basic");
    TEST_ASSERT_EQ(atol("123456789"), 123456789L, "%ld");
    TEST_ASSERT_EQ(atol("0"), 0L, "%ld");
    TEST_ASSERT_EQ(atol("-999"), -999L, "%ld");
    TEST_PASS();
}

/* ========== abs / labs ========== */

void test_abs_positive(void) {
    TEST_START("abs positive");
    TEST_ASSERT_EQ(abs(42), 42, "%d");
    TEST_PASS();
}

void test_abs_negative(void) {
    TEST_START("abs negative");
    TEST_ASSERT_EQ(abs(-42), 42, "%d");
    TEST_PASS();
}

void test_abs_zero(void) {
    TEST_START("abs zero");
    TEST_ASSERT_EQ(abs(0), 0, "%d");
    TEST_PASS();
}

void test_labs_basic(void) {
    TEST_START("labs basic");
    TEST_ASSERT_EQ(labs(-123456L), 123456L, "%ld");
    TEST_ASSERT_EQ(labs(123456L), 123456L, "%ld");
    TEST_ASSERT_EQ(labs(0L), 0L, "%ld");
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

    test_strstr_found_start();
    test_strstr_found_mid();
    test_strstr_notfound();
    test_strstr_empty_needle();
    test_strstr_overlap();

    test_strtok_basic();
    test_strtok_multidelim();
    test_strtok_nodelim();
    test_strtok_r_basic();

    test_strspn_all();
    test_strspn_partial();
    test_strspn_none();
    test_strspn_empty();

    test_strcspn_none();
    test_strcspn_some();
    test_strcspn_first();

    test_strpbrk_found();
    test_strpbrk_notfound();
    test_strpbrk_empty();

    test_memcmp_equal();
    test_memcmp_diff();
    test_memcmp_zero();

    test_strtol_decimal();
    test_strtol_hex();
    test_strtol_octal();
    test_strtol_negative();
    test_strtol_auto();
    test_strtol_endptr();

    test_strtoul_std_basic();
    test_strtoul_std_hex();

    test_atoi_basic();
    test_atol_basic();

    test_abs_positive();
    test_abs_negative();
    test_abs_zero();
    test_labs_basic();

    test_strtoul_basic();
    test_strtoul_large();

    return TEST_END();
}
