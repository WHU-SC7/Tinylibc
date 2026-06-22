# Claude Code 提交记录

本文件记录通过 Claude Code 生成代码的 git 提交，按时间倒序排列。
所有 Claude 提交在 git log 中可通过 `Co-Authored-By: Claude` 检索。

> 查看 diff：`git show <hash>` 或 `git log <hash>~1..<hash> -p`

## 2026-06-22

### `a479ed6` 15:38 — build: tmake 支持 -j N 并行编译+链接+计时报告

**编译与链接并行化：**
- 拆分 `compile_file()` 为 `compile_file_start() / compile_file_wait()`
- 拆分 `link_app()` 为 `link_app_start() / link_app_wait()`
- 新增 `parallel_compile_task()`：最多 N 个 gcc 子进程同时编译
- 新增 `parallel_link_task()`：最多 N 个 ld 子进程同时链接
- `compile_task() / link_task()` 在 `g_max_jobs > 1` 时自动切换到并行路径
- 修复 3 处 `waitpid(-1,...)` 改为 `waitpid(pid,...)`，并行安全

**参数解析：**
- `-j N`：指定并行任务数
- `-j`（无参）：自动检测 CPU 核数（读取 `/proc/cpuinfo`）
- 默认串行（不加 `-j` 行为不变）
- `--help / -h`：显示帮助

**性能报告：**
- `clock_gettime(CLOCK_MONOTONIC)` 分阶段计时
- 输出各阶段耗时、文件数、文件大小

---

### `d65db26` 12:31 — docs: 补充 CLAUDE.md 阶段 3-4 约定，修复章节编号

- 在 CLAUDE.md 中补充命名约定、编程规则
- 修复章节编号连续性
- 添加 `CLAUDE_COMMITS.md` 引用

---

### `e7d9fda` 12:27 — fix: 添加路径缓冲区长度参数，统一魔数常量 TLIBC_BUF_SIZE

- `lib/path.c`：为 `tlibc_cal_absolute_path` 添加 `max_len` 参数，用 `snprintf` 替代无长度检查的 `strcpy`
- `include/tlibc_everything.h`：新增 `TLIBC_BUF_SIZE (1024*1024)` 统一宏
- `lib/file.c` + `app/shell.c`：将重复的 `1024*1024` 本地常量替换为 `TLIBC_BUF_SIZE`

---

### `6a75096` 12:23 — refactor: 移除重复类型定义，标记竞态变量 volatile

- `include/atomic.h`：移除重复的 `uint64_t`/`uint32_t` 类型定义，改为 `#include "tlibc_types.h"`
- `include/init.h` + `lib/init.c`：`pre_alloc_stack` 和 `remain_thread_stack_num` 添加 `volatile` 限定

---

### `e3977fd` 12:20 — style: 清理中文分析注释、死代码块，标准化 TODO

- `lib/path.c`：精简 150+ 行中文自省分析注释为英文 TODO，保留已知边界条件标注
- `lib/pthread.c`：移除 pthread_join 中 30+ 行注释掉的旧实现，用英文说明异步回收设计
- `lib/net.c`：将"莫名其妙的问题,等待审阅"转为 TODO
- `lib/core.c`：清理"!!!"、中文 Doxygen 为英文 NOTE
- `lib/init.c`：将竞态条件中文警告转为英文 NOTE
- `include/pthread.h`：清理过期中文注释

---

### `fa1cfd9` 12:07 — refactor: 统一 tlibc_ 前缀，消除符号冲突

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
