#include "tlibc_test.h"
#include "tlibc_compat.h"
#include "ctype.h"

TEST_DEFINE_COUNTERS();

/* ========== isalpha ========== */

void test_isalpha_lower(void) {
    TEST_START("isalpha lowercase");
    TEST_ASSERT(isalpha('a'), "'a' is alpha");
    TEST_ASSERT(isalpha('z'), "'z' is alpha");
    TEST_PASS();
}

void test_isalpha_upper(void) {
    TEST_START("isalpha uppercase");
    TEST_ASSERT(isalpha('A'), "'A' is alpha");
    TEST_ASSERT(isalpha('Z'), "'Z' is alpha");
    TEST_PASS();
}

void test_isalpha_non(void) {
    TEST_START("isalpha non-alpha");
    TEST_ASSERT(!isalpha('0'), "'0' not alpha");
    TEST_ASSERT(!isalpha(' '), "' ' not alpha");
    TEST_ASSERT(!isalpha(0),   "NUL not alpha");
    TEST_PASS();
}

/* ========== isdigit ========== */

void test_isdigit_true(void) {
    TEST_START("isdigit true");
    TEST_ASSERT(isdigit('0'), "'0' is digit");
    TEST_ASSERT(isdigit('9'), "'9' is digit");
    TEST_PASS();
}

void test_isdigit_false(void) {
    TEST_START("isdigit false");
    TEST_ASSERT(!isdigit('a'), "'a' not digit");
    TEST_ASSERT(!isdigit('/'), "'/' not digit");
    TEST_PASS();
}

/* ========== isalnum ========== */

void test_isalnum_true(void) {
    TEST_START("isalnum true");
    TEST_ASSERT(isalnum('a'),  "'a' is alnum");
    TEST_ASSERT(isalnum('Z'),  "'Z' is alnum");
    TEST_ASSERT(isalnum('5'),  "'5' is alnum");
    TEST_PASS();
}

void test_isalnum_false(void) {
    TEST_START("isalnum false");
    TEST_ASSERT(!isalnum(' '), "' ' not alnum");
    TEST_ASSERT(!isalnum('.'), "'.' not alnum");
    TEST_PASS();
}

/* ========== isprint ========== */

void test_isprint_true(void) {
    TEST_START("isprint true");
    TEST_ASSERT(isprint(' '),  "' ' is print");
    TEST_ASSERT(isprint('~'),  "'~' is print");
    TEST_ASSERT(isprint('a'),  "'a' is print");
    TEST_PASS();
}

void test_isprint_false(void) {
    TEST_START("isprint false");
    TEST_ASSERT(!isprint(0x1b), "ESC not print");
    TEST_ASSERT(!isprint('\0'), "NUL not print");
    TEST_ASSERT(!isprint(0x7f), "DEL not print");
    TEST_PASS();
}

/* ========== isgraph ========== */

void test_isgraph_true(void) {
    TEST_START("isgraph true");
    TEST_ASSERT(isgraph('a'), "'a' is graph");
    TEST_ASSERT(isgraph('~'), "'~' is graph");
    TEST_PASS();
}

void test_isgraph_false(void) {
    TEST_START("isgraph false");
    TEST_ASSERT(!isgraph(' '), "' ' not graph");
    TEST_ASSERT(!isgraph('\0'), "NUL not graph");
    TEST_PASS();
}

/* ========== isspace ========== */

void test_isspace_true(void) {
    TEST_START("isspace true");
    TEST_ASSERT(isspace(' '),  "space");
    TEST_ASSERT(isspace('\t'), "tab");
    TEST_ASSERT(isspace('\n'), "newline");
    TEST_ASSERT(isspace('\r'), "CR");
    TEST_ASSERT(isspace('\f'), "FF");
    TEST_ASSERT(isspace('\v'), "VT");
    TEST_PASS();
}

void test_isspace_false(void) {
    TEST_START("isspace false");
    TEST_ASSERT(!isspace('a'), "'a' not space");
    TEST_ASSERT(!isspace(0),   "NUL not space");
    TEST_PASS();
}

/* ========== toupper / tolower ========== */

void test_toupper_lower(void) {
    TEST_START("toupper from lowercase");
    TEST_ASSERT_EQ(toupper('a'), 'A', "%c");
    TEST_ASSERT_EQ(toupper('z'), 'Z', "%c");
    TEST_PASS();
}

void test_toupper_other(void) {
    TEST_START("toupper non-lowercase unchanged");
    TEST_ASSERT_EQ(toupper('A'), 'A', "%c");
    TEST_ASSERT_EQ(toupper('0'), '0', "%c");
    TEST_PASS();
}

void test_tolower_upper(void) {
    TEST_START("tolower from uppercase");
    TEST_ASSERT_EQ(tolower('A'), 'a', "%c");
    TEST_ASSERT_EQ(tolower('Z'), 'z', "%c");
    TEST_PASS();
}

void test_tolower_other(void) {
    TEST_START("tolower non-uppercase unchanged");
    TEST_ASSERT_EQ(tolower('a'), 'a', "%c");
    TEST_ASSERT_EQ(tolower('0'), '0', "%c");
    TEST_PASS();
}

/* ========== main ========== */

int main(void) {
    TEST_BEGIN("ctype library tests");

    test_isalpha_lower();
    test_isalpha_upper();
    test_isalpha_non();

    test_isdigit_true();
    test_isdigit_false();

    test_isalnum_true();
    test_isalnum_false();

    test_isprint_true();
    test_isprint_false();

    test_isgraph_true();
    test_isgraph_false();

    test_isspace_true();
    test_isspace_false();

    test_toupper_lower();
    test_toupper_other();

    test_tolower_upper();
    test_tolower_other();

    return TEST_END();
}
