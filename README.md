# 1.Tinylibc介绍
Tinylibc是轻量级的、独立的部分 C 标准库实现。对Pthread的机制进行创新性修改，设计了新的线程资源异步回收的机制。不仅实现了异步地回收线程栈；而且能自动异步地回收线程未释放的内存

## 平台支持
1. 在x86_64主机的Linux上运行. Ubuntu和Debian都可以

## 实验例程
使用下面两个命令，可以比较Tinylibc与Glibc的性能
```bash
make glibc      # 使用Glibc编译exp.c并运行
```
```bash
make tlibc      # 使用本项目Tinylibc编译exp.c并运行
```

## 如何运行
在x64主机上，只需要x86_64-linux-gnu-gcc就可以编译
```bash
make all      # 编译生成所有用户程序
```
```bash
make run      # 编译并运行shell
```
进入 shell 后会进入 ~/tlibc/bin/（tmake 安装目录），目录下对每个 app 下的 .c 文件都编译了可执行程序

可执行文件也统一输出在 `build/output/` 下。

riscv架构待更新
# 2.Tlibc项目结构
### 文件夹结构
app下是命令行程序的.c文件，包括shell, vim, 吃豆人游戏, 基本文件操作命令

lib下按领域分组存放核心库函数（core/、stdio/、thread/、net/、misc/、init/），编译为静态库 tlibc.a

下面文件夹中都是.h头文件

arch下是架构相关的头文件，目前有x86_64架构和riscv架构的

include下是一般的头文件

### 特殊文件

项目计划.md是现阶段的计划

ld.script是简单的链接脚本

Makefile用于构建

README.md就是本文件

tlibc_commit_log.md汇总记录了项目的详细提交记录
