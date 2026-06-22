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

#define DEFAULT_LS_BUF_SIZE 1024*1024

//新函数，先放在这
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

// 在 snprintf.h 中声明
int snprintf(char *str, size_t size, const char *format, ...);

//path.c
void tlibc_cal_absolute_path(const char *path, const char *cwd, char *absolute_path); //计算绝对路径

//envp.c
extern char **global_envp;
int tlibc_envp_count(char *envp[]);
void tlibc_print_all_env_vars(char *envp[]);
char *get_env_var(char *envp[], const char *name);


#endif // TLIBC_EVERYTHING_H
