# Tinylibc 项目概览

Tinylibc 是一个轻量级、独立的部分 C 标准库实现，运行于 x86_64 Linux，完全不依赖 glibc。项目包含自定义的 libc、pthread 实现（带异步线程资源回收机制）、内存池，以及一套自托管的用户态程序（shell、vim、网络工具等）。

## 构建与运行

**前置依赖：** `x86_64-linux-gnu-gcc`、`x86_64-linux-gnu-ld`、`x86_64-linux-gnu-ar`

```bash
make all       # 两阶段编译：先编译 tmake + shell，再用 tmake 递归编译所有 app
make run       # 编译并进入 shell
make clean     # 删除 build/
make glibc     # 用 glibc 编译 app/paper/exp.c 做对比实验
make tlibc     # 用 Tinylibc 编译 app/paper/exp.c 做对比实验
make debug     # make all + strace
make disassemb # 反汇编 tlibc_x64 到 build/dis_tlibc
```

## 目录结构

### `/lib/` — 核心库源码（被编译为静态库 `tlibc.a`）

| 文件 | 行数 | 职责 |
|------|------|------|
| `start.S` | 14 | 入口点 `__tlibc_start`，调用 `tlibc_init` 后执行 main |
| `clone.S` | 27 | clone() 系统调用的汇编封装 |
| `core.c` | 380 | 所有系统调用包装函数：`__write`、`__read`、`__openat`、`__mmap`、`__fork`、`__futex` 等 |
| `string.c` | 262 | strcpy/strncpy/strcmp/strcat/strchr/memcpy/itoa/strerror 等 |
| `printf.c` | 296 | `__printf`/`__fprintf` 实现，支持 `%d` `%s` `%x` `%f` `%l` |
| `snprintf.c` | 253 | snprintf 实现 |
| `mempool.c` | 325 | 内存池 + 后台工作线程异步回收线程资源 |
| `pthread.c` | 154 | pthread_create/pthread_join/pthread_exit |
| `file.c` | 386 | 文件工具函数：递归删除/创建目录、复制文件、目录列表等 |
| `path.c` | 201 | 路径规范化（绝对路径计算、./../ 处理） |
| `tty.c` | 144 | 终端控制：获取终端大小、设置 raw 模式、按键处理 |
| `net.c` | 128 | socket/connect/bind/listen/accept/recv 等网络 syscall 封装 |
| `system.c` | 72 | 系统函数：get_user_dir（解析 /etc/passwd） |
| `envp.c` | 50 | 环境变量处理 |
| `init.c` | 38 | 初始化：预分配线程栈、初始化内存池、调用 main |

**总行数：约 2,730 行**

### `/app/` — 用户态程序

| 文件 | 行数 | 职责 |
|------|------|------|
| `shell.c` | 771 | 交互式 shell：命令解析、PATH 查找、tab 补全、程序执行 |
| `tmake.c` | 441 | 自托管构建工具：递归编译 `/app/` 下所有子目录的 .c 文件 |
| `coreutils/` | ~290 | cat、cp、echo、ls、mkdir、mv、pwd、rm、rmdir、touch |
| `net/` | ~530 | client、server(多线程)、http、tclient、tserver |
| `term/` | ~1,484 | vim(基础查看器)、top、吃豆人游戏、模板 |
| `test/` | ~360 | 内部测试、fcount、float、madv、passwd、queue、snprintf |
| `paper/` | ~215 | 与 glibc 对比实验：exp、mempool、memtest、pthread |
| `compiler/` | 76 | ELF 读写器 |

### `/include/` — 头文件

| 文件 | 职责 |
|------|------|
| `core.h` | 系统调用包装声明 + printf/fprintf 宏定义 |
| `tlibc.h` | O_*、DT_*、SEEK_*、timespec、sigaction、linux_dirent64 等通用常量/结构 |
| `tlibc_types.h` | size_t、pid_t、uid_t、int64_t 等类型定义 |
| `tlibc_compat.h` | 兼容宏层：`#define write __write`、`#define fork __fork` 等 |
| `tlibc_everything.h` | 伞状头文件（include 所有头文件）+ 工具函数声明 |
| `tlibc_print.h` | 彩色打印宏：PRINT_COLOR、LOG、panic |
| `tlibc_ioctl.h` | termios 结构体、ioctl 常量、ANSI 转义序列 |
| `tlibc_test.h` | 测试函数声明 |
| `string.h` | 字符串函数声明 |
| `pthread.h` | pthread 结构体、clone/mmap 标志、pthread_create/join/exit |
| `mempool.h` | 内存池 API |
| `net.h` | 网络 API 声明 |
| `socket.h` | sockaddr_in、in_addr、AF_INET 等 socket 类型 |
| `mman.h` | madvise 常量 |
| `atomic.h` | x86_64 原子操作：atomic_fetch_add、atomic_compare_exchange（lock cmpxchg） |
| `errno.h` | 错误码定义 |
| `sig_num.h` | 信号编号 |
| `tty.h` | winsize、TIOCGWINSZ、自定义键值、终端 raw 模式函数 |
| `init.h` | 预分配栈全局变量 |

### `/arch/` — 架构相关

| 路径 | 内容 |
|------|------|
| `arch/x86_64/` | 当前支持的架构：syscall 内联汇编（__syscall0-6）、syscall 号、stat 结构体 |
| `arch/riscv64/` | 待支持的架构：stat 结构体、syscall 号 |
| `arch/syscall.h` | 通用 syscall 分发宏：根据参数个数自动选择 __syscall0~6 |

### 其他文件

| 文件 | 职责 |
|------|------|
| `Makefile` | 顶层构建，两阶段流程 |
| `ld.script` | 链接脚本：.text 起始于 0x400000，无复杂段布局 |
| `项目计划.md` | 开发计划 |

## 关键架构决策

### 1. 无 FILE 结构体
所有 I/O 基于裸文件描述符。`printf` 是 `__printf` 的宏，直接调用 `__write`。

### 2. 命名规则

| 前缀 | 适用范围 | 示例 |
|------|---------|------|
| `tlibc_` | 所有公开 API（自定义工具函数、POSIX 接口封装） | `tlibc_stat()`, `tlibc_chmod()`, `tlibc_copy_file()` |
| `__` | 直接 syscall 包装（每函数=一个 syscall）。C 标准 reserved identifier，永不冲突 | `__write()`, `__mmap()`, `__exit()` |
| 裸标准 C 名 | 保留给 C 标准等价函数（`malloc`、`strlen`、`memcpy`、`snprintf`）及 pthread 标准（`pthread_create`） | 外部库链接时需要这些符号 |

### 3. 兼容宏层
`tlibc_compat.h` 将标准/短名称映射为 `tlibc_` 或 `__` 前缀的内部函数：
```c
#define write(fd, buf, len)  __write(fd, buf, len)
#define stat(path, buf)      tlibc_stat(path, buf)
#define getuid()             tlibc_getuid()
#define fork()               __fork()
#define mmap(...)            __mmap(__VA_ARGS__)
```

在应用代码中，直接使用 `openat()`、`write()`、`stat()` 等短名称，它们通过宏展开为内部函数。如需在库代码内直接调用而不触发宏，使用 `__` 或 `tlibc_` 前缀版本。

### 3. 系统调用方式
所有系统调用通过 `arch/syscall.h` → `arch/x86_64/syscall_arch.h` 中基于 `syscall` 指令的内联汇编实现。`syscall.h` 中的宏根据参数个数（0-6）自动派发到 `__syscallN`。

### 4. 内存池与线程异步回收
- 每个线程有自己的内存链表（`struct thread_mem_list`）
- 后台工作线程定期扫描已退出线程，回收其线程栈和未释放内存
- 初始化时预分配 100 个线程栈（每个 4MB，共 400MB）
- 使用自旋锁（atomic compare-exchange）保护全局内存列表

### 5. 构建方式
两阶段构建：
1. Makefile 直接编译 `app/tmake.c` 和 `app/shell.c`，链接 `tlibc.a`
2. `tmake` 扫描 `/app/` 下所有子目录，递归编译每个 `.c` 文件

## 编程约定

- **命名**：
  - 直接 syscall 包装用 `__` 前缀（`__write()`），C 标准保留标识符，永不冲突
  - **所有其他公开函数必须使用 `tlibc_` 前缀**（包括 POSIX 名如 `stat`/`chmod`，以及自定义工具函数）
  - 裸标准 C 名（`malloc`、`strlen`、`memcpy`、`snprintf`、`pthread_create`）保留以支持外部库链接
  - 枚举类型用 `tlibc_` 前缀 + `_t` 后缀（如 `tlibc_thread_state_t`），枚举值用 `TLIBC_` 前缀
- **兼容宏**：需要短名的函数在 `tlibc_compat.h` 中添加 `#define shortname tlibc_longname`。不要在其他地方定义此类宏
- **错误处理**：返回 Linux 内核风格负 errno（例如 `-ENOENT`、`-EINVAL`）
- **注释**：添加注释时用英文。禁止添加注释掉的死代码块
- **入口流程**：`start.S` → `tlibc_init()`（预分配栈、初始化内存池）→ `main()`
- **应用入口**：每个 app/*.c 文件实现 `int main(int argc, char *argv[])`
- **类型**：项目自定类型（无标准头文件），参见 `tlibc_types.h`
