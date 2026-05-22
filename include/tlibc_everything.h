#ifndef TLIBC_EVERYTHING_H
#define TLIBC_EVERYTHING_H

// Umbrella header for Tinylibc.
// Add all public headers from the include/ directory here.

#include "atomic.h"
#include "core.h"
#include "errno.h"
#include "init.h"
#include "mempool.h"
#include "mman.h"
#include "net.h"
#include "pthread.h"
#include "sig_num.h"
#include "socket.h"
#include "string.h"
#include "tlibc_compat.h"
#include "tlibc_ioctl.h"
#include "tlibc_print.h"
#include "tlibc_test.h"
#include "tlibc_types.h"
#include "tlibc.h"
#include "tty.h"

//新函数，先放在这
int tlibc_get_user_dir(char *buf, int buf_size);
int tlibc_get_file_len(char *path);
int tlibc_get_file_count(const char *dir_path);
int tlibc_get_file_name_list(const char *dir_path, uint64_t file_name_list, int file_count);
int tlibc_is_path_dir(const char *path);
int tlibc_is_path_file(const char *path);
int tlibc_rm_file(const char *path);
int tlibc_recursive_rm_dir(const char *path);

// 在 snprintf.h 中声明
int snprintf(char *str, size_t size, const char *format, ...);

//path.c
void cal_absolute_path(const char *path, const char *cwd, char *absolute_path); //计算绝对路径

#endif // TLIBC_EVERYTHING_H
