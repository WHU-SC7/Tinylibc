# Tinylibc

轻量级 C 标准库 + 自托管用户态程序，x86_64 Linux，无 glibc 依赖。

## Build

**前置依赖：** `x86_64-linux-gnu-gcc`、`x86_64-linux-gnu-ld`、`x86_64-linux-gnu-ar`

```bash
make all       # 两阶段：先 Makefile 编译 tmake+shell，再用 tmake 递归构建所有 app
make run       # 编译后进入 shell
make clean     # rm -rf build/
make init-hooks # 初始化 Git hooks（make all 自动配置，通常不需手动）
```

> 新机器只需 `git clone && make all` 即可。Git hooks（commit-msg 格式校验 + commit-msg 自动写入提交记录）由构建流程自动设置。

`app/tmake.c` 是自托管构建工具，支持 `-j [N]` 并行编译。

> 详细操作步骤（运行、调试、测试等）参见 `CLAUDE_DETAILS.md`。

## Test

库函数单元测试，详见 `CLAUDE_DETAILS.md` 的 Test 章节。

## 快速定位

### 核心库 `lib/*.c` → 静态库 `tlibc.a`

| 文件 | 内容 |
|------|------|
| `core.c` | 所有 syscall 包装：`__write`, `__read`, `__mmap`, `__fork`, `__futex` 等 |
| `string.c` | `strcpy`, `strlen`, `memcpy`, `strcmp`, `strerror` |
| `printf.c` | `__printf` / `__fprintf`（支持 `%d %s %x %f %l`） |
| `snprintf.c` | `snprintf` |
| `pthread.c` | `pthread_create` / `pthread_join` / `pthread_exit` |
| `mempool.c` | 内存池 + 后台线程异步回收线程资源 |
| `file.c` | `tlibc_get_file_count`, `tlibc_recursive_mkdir`, `tlibc_copy_file` 等 |
| `path.c` | 路径规范化 |
| `tty.c` | 终端大小、raw 模式、按键处理 |
| `net.c` | socket / connect / bind / listen / accept |
| `system.c` | `tlibc_get_user_dir`（解析 /etc/passwd） |
| `envp.c` | 环境变量处理 |
| `init.c` | `tlibc_init`：预分配线程栈 → 初始化内存池 → 调 main |
| `start.S` | 入口 `__tlibc_start` → `tlibc_init` → `main` |
| `clone.S` | clone() 系统调用汇编封装 |

### 应用 `app/*.c`

| 文件 | 内容 |
|------|------|
| `shell.c` | 交互式 shell |
| `tmake.c` | 自托管构建工具（并行编译、链接） |
| `coreutils/` | cat, cp, echo, ls, mkdir, mv, pwd, rm, rmdir, touch |
| `net/` | client, server, http, tclient, tserver |
| `term/` | vim, top, __game_pacman |
| `test/` | 内部测试 |
| `paper/` | 与 glibc 对比实验 |

### 头文件 `include/*.h`

| 文件 | 内容 |
|------|------|
| `core.h` | syscall 包装声明 + printf/fprintf 宏 |
| `tlibc.h` | O_*, DT_*, SEEK_*, timespec, sigaction, linux_dirent64 等 |
| `tlibc_types.h` | size_t, pid_t 等类型 |
| `tlibc_compat.h` | 兼容宏：`#define write __write` |
| `tlibc_everything.h` | 伞状头文件 + 工具函数声明 |
| `pthread.h` | pthread 结构体、clone/mmap 标志 |
| `mempool.h` | 内存池 API |
| `atomic.h` | x86_64 原子操作：lock cmpxchg / lock xadd |
| `tty.h` | winsize, TIOCGWINSZ, raw 模式 |
| `net.h` / `socket.h` | 网络 API |
| `errno.h` | 错误码 |
| `sig_num.h` | 信号编号 |

### 架构 `arch/`

- `arch/x86_64/` — 当前支持：syscall 内联汇编 (`__syscall0`–`6`), syscall 号, stat 结构
- `arch/syscall.h` — 自动根据参数个数分发到 `__syscallN`

## 关键约定

### 命名规则

| 前缀 | 用途 | 示例 |
|------|------|------|
| `__` | 直接 syscall 包装 | `__write()`, `__mmap()`, `__exit()` |
| `tlibc_` | 所有其他公开函数（POSIX 封装 + 自定义工具） | `tlibc_stat()`, `tlibc_copy_file()` |
| 裸 C 名 | 保留给标准等价函数 + pthread | `malloc`, `strlen`, `pthread_create` |

### 兼容宏
`tlibc_compat.h` 把短名映射为内部函数。应用代码直接用 `write()`、`openat()`、`stat()`。

### 错误处理
返回 Linux 内核风格负 errno（`-ENOENT`, `-EINVAL`）。

### Git 提交规范

```
<type>: <中文标题>
```

类型标签（中英双解，兼顾人 & LLM）：

| 标签 | 说明 |
|------|------|
| `feat` | 新功能 / new feature |
| `fix` | 修复 bug / bug fix |
| `refactor` | 重构 / refactoring |
| `perf` | 性能优化 / performance |
| `docs` | 文档 / documentation |
| `style` | 格式/注释清理 / code style |
| `test` | 测试 / testing |
| `build` | 构建系统 / build system |
| `chore` | 杂项 / maintenance |

- 标题用中文，一行；body 用中文或英文要点
- Claude 提交须在末尾加 `Co-Authored-By: Claude <noreply@anthropic.com>`
- 提交记录记入 `CLAUDE_COMMITS.md`，格式：``### HH:MM — type: title``（commit-msg 自动写入 + body 提取）
- 完整规范见 `CLAUDE_DETAILS.md`

### 编程规则
- 不要保留注释掉的代码块，直接删除
- 多线程共享的全局变量标 `volatile`（`pre_alloc_stack`, `remain_thread_stack_num`）
- 外部输入字符串用 `snprintf` / `strncpy` + 手动 null 终止，不用裸 `strcpy`/`strcat`
- 通用缓冲区大小用 `TLIBC_BUF_SIZE`（`tlibc_everything.h`），不硬编码魔数
- 无 FILE 结构体，所有 I/O 基于裸 fd
- 类型定义在 `tlibc_types.h`，枚举 `tlibc_`前缀 + `_t`后缀，值 `TLIBC_`前缀
