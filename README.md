# 1.Tinylibc介绍
Tinylibc支持x64平台，不使用标准库，使用系统调用，静态编译出Linux命令行的可执行程序。现有自定义的vim，命令行的吃豆人游戏，基本的文件操作命令，简单的shell。
## 平台支持
1. 在x86_64主机的Linux上运行. Ubuntu和Debian都可以

2. 在qemu-system-riscv64使用SC7内核运行，见SC7项目的tlibc分支(https://github.com/WHU-SC7/SC7.git)

## 如何运行
在x64主机上，只需要x86_64-linux-gnu-gcc就可以编译
```bash
make all      # 编译生成可执行文件tlibc_x64
```
或者更简单的，使用make就可以。
```bash
make run      # 执行生成的可执行文件tlibc_x64
```
或者执行./build/tlibc_x64

riscv架构待更新
# 2.Tlibc项目结构
### 文件夹结构
app下是命令行程序的.c文件，包括shell, vim, 吃豆人游戏, 基本文件操作命令

lib下，core.c是库函数的实现，test.c包含入口函数和测试函数

下面文件夹中都是.h头文件

arch下是架构相关的头文件，目前有x86_64架构和riscv架构的

include下是一般的头文件

### 特殊文件

项目计划.md是现阶段的计划

ld.script是简单的链接脚本

Makefile用于构建

README.md就是本文件

tlibc_commit_log.md汇总记录了项目的详细提交记录
