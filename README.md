# 1.Tlibc介绍
Tlibc是支持x64和riscv64架构的libc. 个人学习用途，目前支持基本文件操作和shell
## 平台支持
1. 在x86_64主机上运行. 目前在ubuntu22.04.1开发和验证

2. 在qemu-system-riscv64使用SC7内核运行，见SC7项目(https://github.com/WHU-SC7/SC7.git)

未来将支持:
在VisionFive2开发版上运行Tlibc程序（理论上很简单，SC7就支持在板上运行
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
### c文件
主目录下，有3个c文件: core.c包装系统调用并提供库函数, test.c的测试函数是一般运行时的入口函数, app.c是shell

### 文件夹结构
app下是.c文件，是各个命令(如cat)的实现

下面文件夹中都是.h头文件

arch下是架构相关的头文件，目前只有riscv架构的

internal下是Tlibc内部使用的头文件

external，目前感觉跟internal没有区别

### 特殊文件
tlibc_commit_log.md汇总记录了项目的详细提交记录

项目计划.md是一阶段计划，已初步完成

Makefile用于构建
