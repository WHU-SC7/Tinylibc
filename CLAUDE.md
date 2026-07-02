# Tinylibc

轻量级 C 标准库 + 自托管用户态程序，x86_64 Linux，无 glibc 依赖。

## 项目理念

**透明。** 一个工具只展示一个机制。默认无参数运行就是它最常用的用法；
参数不是配置清单，超过三个的情况极少。

**可读。** 库由实际需求驱动，不追求完整覆盖 POSIX。应用程序能在几分钟
到十几分钟内读完。代码行数在几百行最合适。

**独立。** 从构建工具到运行时，全部自包含。

## Build

**前置依赖：** `x86_64-linux-gnu-gcc`、`x86_64-linux-gnu-ld`、`x86_64-linux-gnu-ar`

```bash
make all       # 两阶段：先 Makefile 编译 tmake+shell，再用 tmake -j 递归构建所有 app
make run       # 编译后进入 shell
make clean     # rm -rf build/
make debug     # make all + strace
make disassemb # 反汇编 tlibc_x64 到 build/dis_tlibc
make glibc     # 用 glibc 编译 app/paper/exp.c 做对比实验
make tlibc     # 用 Tinylibc 编译 app/paper/exp.c 做对比实验
make setcap    # 手动设置 CAP_NET_RAW（ndiscover/netprobe/sniffer 免 sudo）
make init-hooks # 初始化 Git hooks（make all 自动配置，通常不需手动）
make check-hooks # 检查 Git hooks 配置状态
```

> 新机器只需 `git clone && make all` 即可。Git hooks（commit-msg 格式校验）由构建流程自动设置。
> `make all` 构建后会自动打印 CAP_NET_RAW 安装提示，按提示操作可免 sudo 运行网络探测工具。

### 快速迭代

```bash
tmake -b ndiscover     # 只编译+链接 ndiscover（跳过 lib，秒级完成）
tmake -b shell         # 只编译 shell（根级 app/*.c 也支持）
tmake -j 4 -b cat      # 并行编译单个程序
tmake -j               # 自动检测 CPU 核数并行编译全部
```

`app/tmake.c` 是自托管构建工具，支持 `-j [N]` 并行编译和 `-b <程序名>` 单应用构建。
不传 N 时自动检测 CPU 核数；第二次调用 `tmake -b` 会跳过未修改的源文件（增量构建）。

### 构建产物

可执行文件统一输出到 `build/output/`，安装到 `~/tlibc/bin/`。

> 详细操作步骤（运行、调试、测试等）参见 `CLAUDE_DETAILS.md`。

## Test

库函数单元测试，详见 `CLAUDE_DETAILS.md` 的 Test 章节。

## 快速定位

### 核心库 `lib/` → 静态库 `tlibc.a`

| 文件 | 内容 |
|------|------|
| `core/` | syscall 包装按领域拆分：`io.c`（文件I/O/fs）、`proc.c`（进程）、`mem.c`（内存/malloc）、`time.c`（时间）、`signal.c`（信号）、`sync.c`（futex） |
| `string.c` | `strcpy`, `strlen`, `memcpy`, `strcmp`, `strerror`，`__memset`，`__memmove` |
| `stdio/` | `printf.c`（`__printf`/`__fprintf`）、`snprintf.c`；支持 `%.Nf`/`%.Ns`/`%.Nd`/`%.Nu`/`%.Nx` 精度格式 |
| `thread/` | `pthread.c`（pthread_create/join/exit）、`mempool.c`（内存池+后台回收）、`clone.S`（clone 汇编） |
| `net/` | `socket.c`（socket/connect/bind/listen/accept/shutdown）、`dns.c`（DNS 解析） |
| `poll.c` | I/O 多路复用：poll/ppoll/select/pselect/epoll POSIX 封装 |
| `tty.c` | 终端大小、raw 模式（保存/恢复 termios、完整 cfmakeraw 风格）、光标定位、按键处理（方向键→自定义键值、EPIPE 防护） |
| `misc/` | `file.c`（文件工具函数）、`path.c`（路径规范化）、`envp.c`（环境变量）、`system.c`（`tlibc_get_user_dir`） |
| `procfs.c` | `/proc` 解析公共库：`tlibc_list_pids`、`tlibc_read_proc_status`、`tlibc_read_proc_stat` |
| `init/` | `init.c`（`tlibc_init`：预分配线程栈→初始化内存池→调`main`）、`start.S`（入口 `__tlibc_start`→`tlibc_init`→`main`） |
| `math/` | `math.c`（sqrt/fabs/ceil/floor/round/trunc/fmod + sin/cos/tan/atan/atan2 + exp/log/log10/pow + isqrt，泰勒/牛顿 ~1e-10 精度） |

### 应用 `app/*.c`

| 文件 | 内容 |
|------|------|
| `shell.c` | 交互式 shell（PATH 查找、tab 补全、配置文件） |
| `tmake.c` | 自托管构建工具（并行编译 `-j`、单目标 `-b`、增量构建） |
| `coreutils/` | cat, cp, echo, fcount, free, grep, hexdump, ls, mkdir, mv, ps, pwd, rm, rmdir, touch, df |
| `net/` | ndiscover（网络发现）, webserv（HTTP 服务器）, sniffer（抓包）, netprobe（延迟探测）, ping（ICMP 探测）, portscan（端口扫描）, http（HTTP 客户端）, dnsquery（DNS 查询） |
| `net/old/` | 旧版：ssh, sshd, tclient, tserver, client, server（保留但不再编译） |
| `term/` | vim, top, __game_pacman, template |
| `compiler/` | **tcc**（自托管 C 编译器，9/9 自编译通过）：lex/parse/preproc/cgen/cgen_expr/cgen_asm/elf_write | tpp（独立预处理器） |（详见下方编译器章节） |
| `elf/` | elf_reader（ELF 文件读取器）, elf_maker（ELF 生成器）, elfdump（ELF 反汇编工具） |
| `test/` | test_string, test_smoke（冒烟测试）, test_iomux（poll/select/epoll 测试）, test_printf（格式化、精度测试）, test_filelist（文件列表测试）, thread（线程测试）, tlibc_free（释放测试）, passwd, quene, test_math（sqrt/sin/cos/exp/log/pow 等 23 个用例） |
| `paper/` | exp, mempool, memtest, pthread（与 glibc 对比实验） |

### 自托管 C 编译器 `app/compiler/`（~5,500 行，33 次提交）

自举 C 编译器，编译 Tinylibc 自身全部库源文件（27/27）并链接运行。
入口 `tcc.c`（206 行）读取源文件 → 预处理 → 词法 → 解析 → 代码生成 → ELF 输出。

| 文件 | 行数 | 职责 |
|------|------|------|
| `tcc.c` | 206 | 编译器入口：参数解析、读文件、编排编译流水线 |
| `tcc.h` | 382 | 核心类型：Token/AST 节点/Arena 分配器/符号表/类型系统/局部变量 |
| `preproc.c` | 691 | C 预处理器：宏定义/展开（对象宏+函数宏+递归+##+stringify）、#include、#if/#ifdef/#ifndef 条件编译、续行符/注释清理 |
| `lex.c` | 547 | 词法分析器：字符流 → Token 流，覆盖 C 全部关键字和运算符 |
| `parse.c` | 1,759 | 递归下降解析器：Token 流 → AST，完整运算符优先级链（赋值→后缀），含 struct/union/enum/typedef/goto/label/switch |
| `cgen.c` | 666 | x86_64 代码生成器：AST → 机器码，管理函数帧/局部变量/控制流跳转标签 |
| `cgen_expr.c` | 731 | 表达式代码生成：二元/一元运算、函数调用（x86_64 ABI 传参）、浮点 SSE |
| `cgen_asm.c` | 128 | 内联汇编：识别 ~15 种已知 __asm__ 模板（syscall/lock xadd/lock cmpxchg/TLS）并发射机器码 |
| `elf_write.c` | 301 | ELF64 可重定位目标文件写入器：接收代码缓冲区和符号表，生成 .o 文件 |
| `tpp.c` | 48 | 独立预处理器：读 C 源文件 → preprocess() → 输出 .i 文件，用于调试宏展开 |
| `tmakelist` | 5 | 多文件编译规则：tcc 列出所有模块依赖，tpp 只依赖 preproc |

**编译流水线：** `tcc input.c` → read_file → preprocess → lexer_init → parser_init → parse_program → cgen_init → cgen_program → elf_write_object → `.o` 文件

**已实现的语言特性：** int/char/short/long/void/double 基本类型、struct/union/enum、typedef、指针/数组、const/volatile/register/static/extern 限定符、if/else/while/for/do-while/switch/case/default、goto/label、return/break/continue、函数调用（含可变参数）、sizeof、逗号表达式、字符串字面量、内联汇编（__asm__）、__builtin_va_list/start/arg/end、浮点 SSE 代码生成、double 字面量（含科学计数法）

**预处理器特性：** 对象宏 + 函数式宏（含递归展开、参数预展开）、## 粘贴、# stringify、__VA_ARGS__、#include、#if/#ifdef/#ifndef/#endif、续行符 `\`、注释过滤（`//` / `/* */`）、CRLF 行尾处理

**关键常量：** Arena 16MB、代码缓冲区 256KB、符号表 8192 条、重定位 16384 条、字符串池 256KB、宏 4096 个

### 头文件（三层结构：核心 → POSIX → 项目自定义）

**Layer 1 — 核心 `include/`**（与架构/硬编码相关）

| 文件 | 内容 |
|------|------|
| `core.h` | 所有 syscall 包装声明 + 核心公共头文件引用 |
| `elf.h` | ELF64 格式定义：Elf64_Ehdr/Shdr/Sym/Rela，被编译器 (tcc) 和 elf 工具 (elf_reader/elf_maker/elfdump) 共用 |
| `procfs.h` | `/proc` 解析 API：`tlibc_list_pids`, `tlibc_read_proc_status`, `tlibc_read_proc_stat` |
| `atomic.h` | x86_64 原子操作：lock cmpxchg / lock xadd |
| `mempool.h` | 内存池 API |
| `net.h` | socket / bind / listen / accept 等网络 API |
| `terminal_esc.h` | ANSI/VT100 转义序列宏：CURSOR_HOME, CLEAR_SCREEN, ALT_SCREEN |
| `tty.h` | winsize, TIOCGWINSZ, raw 模式 API, 自定义键值 (KEY_UP/DOWN/LEFT/RIGHT), 光标定位 |

**Layer 2 — POSIX 兼容层 `include/posix/`**（标准 POSIX 语义）

| 文件 | 内容 |
|------|------|
| `dirent.h` | DT_* 类型常量, `struct linux_dirent64` |
| `errno.h` | 错误码 (EPERM, ENOENT, ESRCH 等) |
| `fcntl.h` | O_RDONLY / O_WRONLY / O_RDWR, O_CREAT, AT_FDCWD |
| `mman.h` | PROT_READ / PROT_WRITE, MAP_PRIVATE / MAP_ANONYMOUS |
| `poll.h` | POLLIN/OUT/ERR/HUP 标志, `struct pollfd`, `poll()` / `ppoll()` 声明 |
| `pthread.h` | pthread_t, pthread_attr_t, pthread_mutex_t, create/join/exit/lock |
| `sched.h` | CLONE_VM / CLONE_THREAD / CLONE_SETTLS 等 clone 标志 |
| `signal.h` | 信号编号 (SIGHUP–SIGSYS), sigset_t, struct sigaction |
| `socket.h` | struct sockaddr_in, in_addr, AF_INET / AF_INET6 |
| `math.h` | sqrt/fabs/ceil/floor/round/trunc/fmod, sin/cos/tan/atan/atan2, exp/log/log10/pow, isqrt 整数平方根 |
| `string.h` | strlen, strcpy, strcmp, memcpy, strchr, itoa 等声明 |
| `sys/epoll.h` | EPOLL_CTL_ADD/DEL/MOD, `struct epoll_event`, `epoll_create1/ctl/wait` |
| `sys/select.h` | fd_set, FD_SET/CLR/ISSET/ZERO, `select()` / `pselect()` 声明 |
| `termios.h` | struct termios, TCGETS/TCSETS, ICANON/ECHO/ISIG/CS8 等标志 |
| `time.h` | struct timespec, CLOCK_REALTIME / CLOCK_MONOTONIC |
| `unistd.h` | SEEK_SET/CUR/END, STDIN/STDOUT/STDERR, PIPE_READ/WRITE, O_NONBLOCK |

**Layer 3 — 项目自定义 `include/tlibc/`**（tlibc 私有语义）

| 文件 | 内容 |
|------|------|
| `tlibc_compat.h` | 兼容宏：`#define write __write`, `#define fork __fork` |
| `tlibc_everything.h` | 伞状头文件（include 全部）+ 工具函数声明 |
| `tlibc_print.h` | 彩色打印宏：PRINT_COLOR, LOG, panic, 256 色 SET_ROW_COLOR |
| `tlibc_test.h` | 测试框架：TEST_START, TEST_ASSERT, TEST_BEGIN/END |
| `tlibc_types.h` | size_t, pid_t, uid_t, off_t, ssize_t 等基础类型 |

> `tlibc_everything.h` 是伞状头文件，新写 app 只需 `#include "tlibc_everything.h"`。
> `tlibc_test.h` 需要显式包含，不在伞状头中。

### 架构 `arch/`

- `arch/x86_64/` — 当前支持：syscall 内联汇编 (`__syscall0`–`6`), syscall 号, stat 结构
- `arch/riscv64/` — 待支持（syscall 号 + stat 结构体）
- `arch/syscall.h` — 自动根据参数个数分发到 `__syscallN`
- `arch/pthread_arch.h` — 架构相关 TLS 访问（`__tlibc_thread_self`：x86_64 用 `%fs:0`，aarch64 用 `tpidr_el0`）

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
- 完整规范见 `CLAUDE_DETAILS.md`

### 注释规范

每个程序的文件头部必须包含以下两部分：

**1. 文件头注释** — 程序功能、展现的机制、使用的系统调用、参数用法。

```c
/*
 * cat — 连接文件并输出到标准输出
 *
 * 机制：openat + read + write 循环，逐文件输出。
 * 系统调用：openat, read, write, close
 *
 * 用法：
 *   cat file          # 输出文件内容到终端
 *   cat file1 file2   # 依次输出多个文件
 */
```

**2. 索引** — 紧跟文件头，列出关键函数和调用链，帮助读者快速定位核心逻辑。不标注行号（代码会更新，行号易过时）。

```c
/*
 * 索引：
 *   main             入口：循环处理每个文件
 *     dump_file      打开 → 读取 → 写入循环
 *     resolve_path   处理 "-" 为 stdin
 */
 */
```

索引不要求覆盖全部函数，只标注体现核心机制的那几个。如果程序逻辑简单（一个 main 函数就是全部），可以省略索引。

**3. 许可证声明** — 每个源文件头部用 SPDX 标准格式标注 MIT License。

```c
/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */
```

放在文件头注释之前或之后均可。新增文件必须包含此声明，已有文件逐步补充。

### 编程规则
- 不要保留注释掉的代码块，直接删除
- 多线程共享的全局变量标 `volatile`
- 外部输入字符串用 `snprintf` / `strncpy` + 手动 null 终止，不用裸 `strcpy`/`strcat`
- 通用缓冲区大小用 `TLIBC_BUF_SIZE`（`tlibc_everything.h`），不硬编码魔数
- 无 `FILE` 结构体，所有 I/O 基于裸 fd
- 类型定义在 `tlibc_types.h`，枚举 `tlibc_`前缀 + `_t`后缀，值 `TLIBC_`前缀
