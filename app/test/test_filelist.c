/* SPDX-License-Identifier: MIT
 *
 * test_filelist.c — 文件/目录查询函数测试
 *
 * 覆盖:
 *   tlibc_get_file_count     — 当前目录文件计数
 *   tlibc_get_file_name_list — 获取文件名列表
 *   tlibc_is_path_dir        — 判断路径是否为目录
 *   tlibc_is_path_file       — 判断路径是否为常规文件
 *
 * 编译: tmake -b test_filelist
 * 运行: build/output/test_filelist
 */

#include "tlibc_everything.h"
#include "tlibc_test.h"

/* 最大测试文件数 */
#define MAX_FILES 512

TEST_DEFINE_COUNTERS();

/* ── 辅助宏（TEST_ASSERT 只接受 2 参，此宏用于需要格式化的断言）── */
#define CHECK(cond, msg) do { if (!(cond)) { \
    __printf(_T_RED "FAIL" _T_RESET "\n    %s:%d: %s\n", __FILE__, __LINE__, msg); \
    __test_failed++; return; } } while(0)

static void
test_file_count(void)
{
    int cnt;

    TEST_START("tlibc_get_file_count — 当前目录");

    cnt = tlibc_get_file_count(".");
    CHECK(cnt > 0, "当前目录应有文件");

    cnt = tlibc_get_file_count("/nonexistent_dir_xyz");
    CHECK(cnt < 0, "不存在的目录应返回负值");

    TEST_PASS();
}

static void
test_file_name_list(void)
{
    int cnt, got;

    TEST_START("tlibc_get_file_name_list — 获取文件名");

    cnt = tlibc_get_file_count(".");
    CHECK(cnt > 0 && cnt <= MAX_FILES, "文件数合理");

    char **names = (char **)tlibc_malloc(cnt * sizeof(char *));
    char  *buf   = (char *)tlibc_malloc(cnt * 256);
    CHECK(names && buf, "内存分配成功");

    for (int i = 0; i < cnt; i++)
        names[i] = buf + i * 256;

    got = tlibc_get_file_name_list(".", (uint64_t)names, cnt);
    CHECK(got > 0, "应获取到文件名");
    CHECK(got <= cnt, "返回数不超过申请数");

    /* 验证每个名字非空 */
    for (int i = 0; i < got; i++)
        CHECK(names[i][0] != '\0', "文件名不应为空");

    tlibc_free(names);
    tlibc_free(buf);
    TEST_PASS();
}

static void
test_is_path_dir(void)
{
    TEST_START("tlibc_is_path_dir — 判断目录");

    CHECK(tlibc_is_path_dir(".") == 1, ". 是目录");
    CHECK(tlibc_is_path_dir("/")  == 1, "/ 是目录");
    CHECK(tlibc_is_path_dir("/tmp") == 1, "/tmp 是目录");
    CHECK(tlibc_is_path_dir("/nonexistent_xyz_123") <= 0, "不存在路径返回<=0");

    TEST_PASS();
}

static void
test_is_path_file(void)
{
    TEST_START("tlibc_is_path_file — 判断文件");

    CHECK(tlibc_is_path_file("/etc/passwd") == 1, "/etc/passwd 是文件");
    CHECK(tlibc_is_path_file("/nonexistent_file_xyz") < 0, "不存在文件返回<0");
    CHECK(tlibc_is_path_file(".") != 1, "目录不是文件");

    TEST_PASS();
}

static void
test_file_list_excludes_dirs(void)
{
    int cnt, got;
    char full[512];

    TEST_START("tlibc_get_file_name_list — 排除目录");

    cnt = tlibc_get_file_count(".");
    CHECK(cnt > 0, "文件数>0");

    char **names = (char **)tlibc_malloc(cnt * sizeof(char *));
    char  *buf   = (char *)tlibc_malloc(cnt * 256);
    CHECK(names && buf, "内存分配成功");

    for (int i = 0; i < cnt; i++)
        names[i] = buf + i * 256;

    got = tlibc_get_file_name_list(".", (uint64_t)names, cnt);
    CHECK(got > 0, "应获取到文件名");

    for (int i = 0; i < got; i++) {
        snprintf(full, sizeof(full), "./%s", names[i]);
        CHECK(tlibc_is_path_dir(full) != 1, "返回的名字不应包含目录");
    }

    tlibc_free(names);
    tlibc_free(buf);
    TEST_PASS();
}

int main(void)
{
    TEST_BEGIN("文件/目录查询测试");

    test_file_count();
    test_file_name_list();
    test_is_path_dir();
    test_is_path_file();
    test_file_list_excludes_dirs();

    return TEST_END();
}
