#ifndef TLIBC_EVERYTHING_H
#define TLIBC_EVERYTHING_H

// Umbrella header for Tinylibc.
// Note: tlibc_test.h must be included explicitly for test binaries.

#include "assert.h"
#include "atomic.h"
#include "core.h"
#include "ctype.h"
#include "dirent.h"
#include "errno.h"
#include "fcntl.h"
#include "mempool.h"
#include "mman.h"
#include "net.h"
#include "pthread.h"
#include "sched.h"
#include "signal.h"
#include "socket.h"
#include "stdlib.h"
#include "string.h"
#include "termios.h"
#include "terminal_esc.h"
#include "time.h"
#include "tlibc_compat.h"   /* opt-in compat macros */
#include "tlibc_print.h"
#include "tlibc_types.h"
#include "tty.h"
#include "unistd.h"

#define TLIBC_BUF_SIZE (1024*1024)
#define DEFAULT_LS_BUF_SIZE TLIBC_BUF_SIZE

//工具函数声明
int tlibc_get_user_dir(char *buf, int buf_size);
int tlibc_get_file_len(char *path);
int tlibc_get_file_count(const char *dir_path);
int tlibc_get_file_name_list(const char *dir_path, uint64_t file_name_list, int file_count);
int tlibc_get_dir_count(const char *dir_path);
int tlibc_get_dir_name_list(const char *dir_path, uint64_t dir_name_list, int dir_count);
int tlibc_is_path_dir(const char *path);
int tlibc_is_path_file(const char *path);
int tlibc_rm_file(const char *path);
int tlibc_recursive_rm_dir(const char *path);
int tlibc_recursive_mkdir(const char *path);
int tlibc_get_file_num(const char *dir_path);
int tlibc_recursive_count_file(const char *path);
int tlibc_copy_file(char *src_path, char *dest_path);
int tlibc_copy_exe_file(char *src_path, char *dest_path);

// snprintf
int snprintf(char *str, size_t size, const char *format, ...);

// scanf/sscanf
int sscanf(const char *str, const char *fmt, ...);
int vsscanf(const char *str, const char *fmt, __builtin_va_list args);

//path.c
void tlibc_cal_absolute_path(const char *path, const char *cwd, char *absolute_path, size_t max_len);

//envp.c
extern char **global_envp;
int tlibc_envp_count(char *envp[]);
void tlibc_print_all_env_vars(char *envp[]);
char *get_env_var(char *envp[], const char *name);

#endif // TLIBC_EVERYTHING_H
