# Claude Code 提交记录

本文件记录通过 Claude Code 进行的项目变更。每次提交在 `git log` 中通过 `Co-Authored-By` 标识识别。

## 2026-06-22

### 第一阶段：函数命名规范化

**范围：** 重构非标准 C 函数的命名，统一使用 `tlibc_` 前缀，避免未来与标准库或第三方库链接时的符号冲突。

**A 组 — POSIX 接口封装：**
- `getuid()` → `tlibc_getuid()`
- `chmod()` → `tlibc_chmod()`
- `stat()` → `tlibc_stat()`
- `usleep()` → `tlibc_usleep()`
- `timespec_get()` → `tlibc_timespec_get()`
- 通过 `tlibc_compat.h` 添加兼容宏，应用代码无需修改

**B 组 — 自定义工具函数：**
- `copy_file/copy_exe_file` → `tlibc_copy_file/tlibc_copy_exe_file`
- `cal_absolute_path` → `tlibc_cal_absolute_path`
- `envp_count/print_all_env_vars` → `tlibc_envp_count/tlibc_print_all_env_vars`
- `general_input_process` → `tlibc_general_input_process`
- `print_int` → `tlibc_print_int`

**C 组 — 内存池函数和枚举：**
- `mem_pool_init` → `tlibc_mem_pool_init`
- 所有 mempool 函数统一 `tlibc_` 前缀
- `enum Thread_state` → `enum tlibc_thread_state_t`
- 枚举值加 `TLIBC_TS_` 前缀

**其他：**
- 移除 `tlibc_test.h` 中的 7 个死声明（未实现、未使用）
- `printf.c` 中 5 个内部函数改为 `static` 避免符号泄漏
- 新建 `CLAUDE.md` 项目手册
- 为重命名函数添加英文注释

### 第二阶段：注释清理

**范围：** 清理中文分析注释、死代码块、未解决备注，标准化为英文 TODO/NOTE 格式。

- `lib/path.c`：精简 150+ 行中文自省分析注释为英文 TODO，保留已知边界条件标注
- `lib/pthread.c`：移除 pthread_join 中 30+ 行注释掉的旧实现，用英文说明异步回收设计
- `lib/net.c`：将"莫名其妙的问题,等待审阅"转为 TODO
- `lib/core.c`：清理"!!!"、中文 Doxygen 为英文 NOTE
- `lib/init.c`：将竞态条件中文警告转为英文 NOTE
- `include/pthread.h`：清理过期中文注释

### 第三阶段：死代码与结构清理

**范围：** 消除重复类型定义、标注竞态变量。

- `include/atomic.h`：移除重复的 `uint64_t`/`uint32_t` 类型定义，改为 `#include "tlibc_types.h"`
- `include/init.h` + `lib/init.c`：`pre_alloc_stack` 和 `remain_thread_stack_num` 添加 `volatile` 限定
