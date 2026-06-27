# Tinylibc 项目概览

Tinylibc 是一个轻量级、独立的部分 C 标准库实现，运行于 x86_64 Linux，完全不依赖 glibc。项目包含自定义的 libc、pthread 实现（带异步线程资源回收机制）、内存池，以及一套自托管的用户态程序（shell、vim、网络工具等）。

## 构建与运行

**前置依赖：** `x86_64-linux-gnu-gcc`、`x86_64-linux-gnu-ld`、`x86_64-linux-gnu-ar`

```bash
make all       # 两阶段编译：先编译 tmake + shell，再用 tmake -j 递归构建所有 app
make run       # 编译并进入 shell
make clean     # 删除 build/
make glibc     # 用 glibc 编译 app/paper/exp.c 做对比实验
make tlibc     # 用 Tinylibc 编译 app/paper/exp.c 做对比实验
make debug     # make all + strace
make disassemb # 反汇编 tlibc_x64 到 build/dis_tlibc

# 快速迭代（修改单个 app 后）
tmake -b ndiscover       # 只编译+链接 ndiscover，跳过 lib
tmake -j 4 -b cat        # 并行编译单个程序
tmake -b ndiscover       # 第二次调用：跳过已有 .o（增量）
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
| `tmake.c` | ~560 | 自托管构建工具：递归编译 app/ 下所有子目录的 .c 文件，支持 -j 并行、-b 单目标构建、增量跳过 .o |
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
| `tlibc_test.h` | 测试框架 |
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
| `Makefile` | 顶层构建，Phase 1 仅编译 boot 必需（lib + tmake + shell） |
| `ld.script` | 链接脚本：.text 起始于 0x400000，无复杂段布局 |
| `CLAUDE.md` | 本文件，项目手册 |
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

### 4. 系统调用方式
所有系统调用通过 `arch/syscall.h` → `arch/x86_64/syscall_arch.h` 中基于 `syscall` 指令的内联汇编实现。`syscall.h` 中的宏根据参数个数（0-6）自动派发到 `__syscallN`。

### 5. 内存池与线程异步回收
- 每个线程有自己的内存链表（`struct thread_mem_list`）
- 后台工作线程定期扫描已退出线程，回收其线程栈和未释放内存
- 初始化时预分配 100 个线程栈（每个 4MB，共 400MB）
- 使用自旋锁（atomic compare-exchange）保护全局内存列表

### 6. 构建方式
两阶段构建，可执行文件统一输出到 `build/output/`：
1. **Phase 1 (Makefile):** 只编译 boot 必需文件（lib/* + app/tmake.c + app/shell.c），创建 tlibc.a，链接 tmake、shell
2. **Phase 2 (tmake -j):** 递归编译 `app/` 下所有子目录，链接全部可执行文件并安装到 `~/tlibc/bin/`
3. **快速迭代:** `tmake -b ndiscover` 只编译+链接单个程序，跳过 lib；重复调用跳过已有 `.o`

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
- **多线程安全**：多线程间共享的全局变量应标注 `volatile`（如 `pre_alloc_stack`、`remain_thread_stack_num`）。mempool 的自旋锁操作见 `atomic.h`
- **缓冲区安全**：操作外部输入的字符串时使用带长度限制的函数（`snprintf`、`strncpy` + 手动 null 终止），避免裸 `strcpy`/`strcat`
- **常量宏**：通用的缓冲区大小使用 `TLIBC_BUF_SIZE`（在 `tlibc_everything.h` 中定义），避免在各文件中重复魔数 `1024*1024`
- **入口流程**：`start.S` → `tlibc_init()`（预分配栈、初始化内存池）→ `main()`
- **应用入口**：每个 app/*.c 文件实现 `int main(int argc, char *argv[])`
- **类型**：项目自定类型（无标准头文件），参见 `tlibc_types.h`

## Test

库函数单元测试，覆盖 `lib/*.c` 中各模块。不测试交互式 app（shell/vim/top）。

### 测试框架

`include/tlibc_test.h` 提供以下宏：

| 宏 | 用途 |
|----|------|
| `TEST_BEGIN("suite name")` | main 开头，输出 suite 名并初始化计数器 |
| `TEST_END()` | main 结尾，输出汇总 `N/M passed`，返回 0/1 |
| `TEST_START("case name")` | 开始一个用例，输出 `name ...` |
| `TEST_PASS()` | 标记当前用例通过 |
| `TEST_FAIL("msg")` | 标记当前用例失败并退出 |
| `TEST_ASSERT(cond, "msg")` | 断言条件为真 |
| `TEST_ASSERT_EQ(a, b, "fmt")` | 断言整数相等，fmt 是 `%d` `%lx` 等占位符 |
| `TEST_ASSERT_STR_EQ(a, b)` | 断言两字符串相等 |
| `TEST_DEFINE_COUNTERS()` | 在文件作用域定义 pass/fail 计数器（每个测试文件写一次） |

### 编写测试

```c
#include "tlibc_test.h"

TEST_DEFINE_COUNTERS();

void test_strlen_basic(void) {
    TEST_START("strlen basic");
    TEST_ASSERT_EQ(strlen("hello"), 5, "%d");
    TEST_PASS();
}

int main(void) {
    TEST_BEGIN("string tests");
    test_strlen_basic();
    return TEST_END();
}
```

- 每个 `.c` 文件是一个独立的测试 binary，有独立的 `main()`
- 每个测试用例是一个 `void func(void)` 函数
- 断言失败自动打印文件名、行号、期望值和实际值，并 return 提前退出该用例
- 命名：文件 `test_xxx.c`，用例 `test_xxx_yyy()`

### 编译运行

```bash
# 编译
x86_64-linux-gnu-gcc -c $(CFLAGS) \
  -o build/app/test/test_xxx.o app/test/test_xxx.c

# 链接（link 静态库 tlibc.a）
x86_64-linux-gnu-ld $(LDFLAGS) -T ld.script \
  -o build/output/test_xxx build/app/test/test_xxx.o build/lib/tlibc.a

# 运行（用重定向而非管道取退出码）
./build/output/test_xxx > /tmp/out.txt 2>&1
echo "Exit: $?"          # 0=全通过 1=有失败
```

**注意：** 退出码通过 `exit_group` syscall 传递，管道 `|` 会使 `$?` 取到右侧命令的退出码。用重定向 `>` 代替。

### 现有测试

```
app/test/test_string.c     — 35 用例：strlen/strcpy/strcmp/memcpy/strchr/itoa/strerror...
```

### 约定

- 每个测试只测一个模块（一个 `.c` 文件），用例名用英文
- 测试文件不 link 额外的库，只 link `tlibc.a`
- 不依赖外部测试框架（`tlibc_test.h` 已包含所需全部宏）
- 遇到 bug 优先写测试复现，再修代码

## Git 提交规范

### 提交信息格式

```
<type>: <中文标题>

<可选：body，一段或分点说明>

<可选：footer>
```

**标题行**（必填，中文，50 字符内）：
- 以类型标签开头，后跟冒号 + 空格，然后是一句中文总结
- 短语形式，不加句号
- 示例：`feat: 添加 tmake -j 并行编译支持`

**Body**（可选，72 字符折行）：
- 描述做了什么、为什么这么做
- 中文或英文均可，用要点 `- ` 或连续段落
- 涉及多处变更时建议分点

**Footer**（可选）：
- `Co-Authored-By: Claude <noreply@anthropic.com>` — Claude 生成的提交必须包含
- `BREAKING CHANGE: <描述>` — 破坏性变更时标注

### 类型标签

| 标签 | 中文 | English | 何时使用 |
|------|------|---------|----------|
| `feat` | 新功能 | new feature | 新增用户可见的功能（新 app、新命令、新参数） |
| `fix` | 修复 bug | bug fix | 修复行为错误、崩溃、逻辑缺陷 |
| `refactor` | 重构 | refactoring | 代码重组但不改变外部行为（重命名、函数拆分） |
| `perf` | 性能优化 | performance | 减少时间/内存/锁竞争，不改变语义 |
| `docs` | 文档 | documentation | 仅修改 CLAUDE.md / README / 注释（不含代码逻辑） |
| `style` | 格式清理 | code style | 清理死注释、格式化、空白，不影响运行 |
| `test` | 测试 | testing | 新增或修改测试用例 |
| `build` | 构建系统 | build system | Makefile、链接脚本、tmake 构建流程 |
| `chore` | 杂项 | maintenance | 不属上述的维护变更（gitignore、CI、配置） |

### 提交原则

1. **原子性**：一个提交只做一件事。修复 bug 和重构分开提交
2. **标题即摘要**：标题应该让读者不看 body 也知道变更性质
3. **Claude 提交**：
   - 任何由 Claude Code 生成代码的提交，必须在 footer 添加 `Co-Authored-By: Claude <noreply@anthropic.com>`
   - Claude 主导的文档变更（仅修改 markdown）不加此标记
   - 人类修改 Claude 生成的代码后提交时，若 Claude 的贡献仍为主要部分，保留该标记
4. **大小限制**：单个提交改动的文件数不宜超过 15 个。如需要跨越多个文件的同一项变更（如全局重命名），可在 body 中分组说明
5. **分支策略**：功能开发在 feature 分支，提交到 main 前建议先 rebase 保持线性历史

### 示例

```
feat: tmake 支持 -j N 并行编译

拆分 compile_file / link_app 为 _start/_wait 两阶段，
新增 parallel_compile_task / parallel_link_task，
g_max_jobs > 1 时自动切换到并行路径。

- add -j N / -j (auto detect) / --help
- 修复 3 处 waitpid(-1,...) 改为 waitpid(pid,...) 并行安全
- clock_gettime 分阶段计时，输出各阶段报告

Co-Authored-By: Claude <noreply@anthropic.com>
```

```
refactor: 统一 tlibc_ 前缀，消除符号冲突

将项目中所有非标准 C 函数（POSIX 封装、工具函数、mempool API）
重命名为 tlibc_ 前缀，新增兼容宏层。

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

```
perf: snprintf 用 tlibc_itoa 替代 %d 递归

benchmark 显示数字格式化快约 5 倍。
```

```
docs: 补充 CLAUDE_DETAILS.md 测试章节
```

### 共识

本规范对人和 LLM 同等重要：
- **对人类**——标题+标签让 changelog 一目了然，body 提供上下文
- **对 LLM**——标签提供了可 parse 的语义分类，`Co-Authored-By` 让 git log 可被 grep 以识别哪次提交是 Claude 生成的

### Git Hooks 系统

项目在 `.githooks/` 中管理 Git hooks，通过 `git config core.hooksPath .githooks` 激活：

| Hook | 文件 | 作用 |
|------|------|------|
| `commit-msg` | `.githooks/commit-msg` | 校验提交信息格式：`<type>: <中文标题>` |

`make all` 会在首次构建时自动配置 hooks。也可单独运行 `make init-hooks`。
绕过校验：`git commit --no-verify`。
