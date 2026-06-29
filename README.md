# 1.Tinylibc
Tinylibc包括一个libc库和一批自定义用户程序，追求的是展示底层机制和自举

## 平台支持
1. 在x86_64主机的Linux上运行. Ubuntu和Debian都可以

## 快速运行

  依赖：x86_64-linux-gnu-gcc、x86_64-linux-gnu-ld、x86_64-linux-gnu-ar

  ```bash
  git clone <url> && cd Tinylibc
  make all       # 编译全部
  make run       # 编译后进入 shell
  ```

  构建产物输出到 build/output/，安装到 ~/tlibc/bin/。

  单程序快速迭代：
  ```bash
  tmake -b ndiscover     # 只编译一个程序，跳过 lib，秒级
  tmake -j               # 并行编译全部
  ```
# 2.项目结构

```
app/              用户程序
  ├── shell.c       交互式 shell
  ├── tmake.c       自托管构建工具
  ├── coreutils/    cat, cp, echo, hexdump, ls, mv, rm, touch 等
  ├── net/          ndiscover, webserv, sniffer, portscan, http, dnsquery
  ├── term/         vim, top, 贪吃蛇
  ├── compiler/     ELF 读写工具
  ├── test/         单元测试
  └── paper/        与 glibc 的对比实验

  lib/              核心库 → 静态库 tlibc.a
  ├── core/         syscall 包装（io, proc, mem, time, signal, sync）
  ├── stdio/        printf, snprintf
  ├── thread/       pthread 实现 + 内存池 + 异步回收
  ├── net/          socket 封装, DNS 解析
  ├── misc/         文件工具, 路径处理, 环境变量
  ├── init/         入口 _start → tlibc_init → main
  ├── string.c      strlen, strcpy, memcpy, strcmp 等
  ├── poll.c        poll/select/epoll 封装
  └── tty.c         终端 raw 模式, 按键处理

  include/          头文件
  ├── core.h 等     核心头文件
  ├── posix/        标准 POSIX 兼容层（string.h, unistd.h 等）
  └── tlibc/        项目自定义（打印宏、测试框架、类型定义）

  arch/             架构相关
  ├── x86_64/       当前支持
  └── riscv64/      待支持
```

# 3.项目理念

  透明。一个工具只展示一个机制。默认无参数运行就是它最常用的用法；
  参数不是配置清单，超过三个的情况极少。

  可读。库由实际需求驱动，不追求完整覆盖 POSIX。应用程序能在几分钟
  到十几分钟内读完。代码行数在几百行最合适。

  独立。从构建工具到运行时，最终要全部自包含。