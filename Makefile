#首先是x64的编译选项

export TOOLPREFIX =x86_64-linux-gnu-

export CC  = ${TOOLPREFIX}gcc
export AS  = ${TOOLPREFIX}gcc
#使用gcc作为LD会自动添加标准库文件: _init _start, 运行时支持等。为了只包含我们的代码，使用ld
export LD  = ${TOOLPREFIX}ld
export OBJCOPY = ${TOOLPREFIX}objcopy
export OBJDUMP = ${TOOLPREFIX}objdump
export AR  = ${TOOLPREFIX}ar

#自动生成依赖文件，支持增量编译
CFLAGS += -MD
#关闭栈保护机制，危险的字符串和指针操作会触发机制导致编译失败。开启可以查看哪里有危险的操作
CFLAGS += -fno-stack-protector
#!!!只需要添加上面两个选项就可以通过编译

#维护帧指针，可以栈回溯
CFLAGS += -fno-omit-frame-pointer
#带gdb调试信息，启用所有警告，警告是错误，不优化
CFLAGS += -ggdb3 -Wall -Werror -O0 
#禁止重复定义全局变量
CFLAGS += -fno-common
#不使用标准库。-nostdlib包含后两者的效果
CFLAGS += -nostdlib -nostartfiles -nodefaultlibs
#比-nostdlib,不仅不使用标准库，还影响一些宏
CFLAGS += -ffreestanding
#生成绝对地址代码，而不是位置无关代码
CFLAGS += -fno-pie
#禁用Red Zone, Red Zone是栈指针(%rsp)下方128字节的安全区域. 函数可以在不调整栈指针的情况下使用这个区域存储局部变量。
CFLAGS += -mno-red-zone


#头文件
CFLAGS += -Iarch -Iinclude
CFLAGS += -Iarch/x86_64
#宏定义
CFLAGS += -DX86_64_TLIBC=1

#链接，段按4k大小对其
LDFLAGS += -z max-page-size=4096
#不链接标准库
LDFLAGS += -nostdlib
#静态链接
LDFLAGS += -static
#链接脚本
LD_SCRIPT = ld.script

#可执行文件
Tlibc_exe = $(WORKPATH)/build/tlibc_x64

export WORKPATH = $(shell pwd)

# Phase 1: 仅编译 tmake(+shell) 自举所需的最小集，tmake Phase 2 处理其余 app
x64_c_srcs := $(wildcard lib/*.c app/tmake.c app/shell.c)
x64_c_objs := $(patsubst %.c,$(WORKPATH)/build/%.o,$(x64_c_srcs))

x64_s_srcs := $(wildcard lib/*.S) 
x64_s_objs := $(patsubst %.S,$(WORKPATH)/build/%.o,$(x64_s_srcs))

all: __x86_64

#先建立目录，再多线程编译。否则可能出错
__x86_64: clean init_dir
	make app -j
	cp build/output/tmake tmp
	cp build/output/shell tmp
	./tmp/tmake -j
	@if [ -f /etc/sudoers.d/tlibc-setcap ]; then \
		$(MAKE) setcap; \
	else \
		echo ""; \
		echo "  ℹ ndiscover/netprobe/sniffer 需要 CAP_NET_RAW 才能免 sudo 运行。"; \
		echo "    安装 sudo 免密规则后会自动设置："; \
		echo "    sudo cp sudoers.d-tlibc-setcap /etc/sudoers.d/tlibc-setcap && sudo chmod 440 /etc/sudoers.d/tlibc-setcap"; \
		echo ""; \
	fi

init_dir:
	mkdir -p build
	mkdir -p build/app build/lib build/output
	mkdir -p tmp
	@echo $(x64_c_srcs)
	@echo $(x64_c_objs)
	@echo $(x64_s_srcs)
	@echo $(x64_s_objs)

$(x64_c_objs): $(WORKPATH)/build/%.o: %.c
	$(CC) -c $(CFLAGS) -MF $(WORKPATH)/build/$*.d -o $@ $<

$(x64_s_objs): $(WORKPATH)/build/%.o: %.S
	$(CC) -c $(CFLAGS) -MF $(WORKPATH)/build/$*.d -o $@ $<

run: __x86_64
	./tmp/shell

clean:
	rm -rf build

dis_file := build/dis_tlibc
disassemb:
	$(OBJDUMP) -S -C $(Tlibc_exe) > $(dis_file)
	@echo "反汇编得到文件"$(dis_file)

debug: all
	strace $(Tlibc_exe)

# ─── Raw socket 权限 (CAP_NET_RAW) ─────────────────────
# ndiscover, netprobe, sniffer 使用 AF_PACKET/SOCK_RAW 需要此权限。
# 构建后自动尝试设置，如需手动操作：
#   sudo setcap cap_net_raw+ep ~/tlibc/bin/ndiscover
#   sudo setcap cap_net_raw+ep ~/tlibc/bin/netprobe
#   sudo setcap cap_net_raw+ep ~/tlibc/bin/sniffer
SETCAP := /usr/sbin/setcap
ifeq ($(wildcard $(SETCAP)),)
SETCAP := /sbin/setcap
endif

setcap:
	@echo "  → 设置 CAP_NET_RAW 权限..."
	@for bin in ndiscover netprobe sniffer; do \
		installed=~/tlibc/bin/$$bin; \
		buildout=build/output/$$bin; \
		if [ -f $$installed ]; then \
			sudo "$(SETCAP)" cap_net_raw+ep $$installed 2>/dev/null && echo "    ✔ $$installed" || echo "    ! $$installed (skip)"; \
		fi; \
		if [ -f $$buildout ]; then \
			sudo "$(SETCAP)" cap_net_raw+ep $$buildout 2>/dev/null || true; \
		fi; \
	done

# 源文件定义
lib_c_srcs := $(wildcard lib/*.c)
lib_s_srcs := $(wildcard lib/*.S)
lib_srcs := $(lib_c_srcs) $(lib_s_srcs)

# 目标文件定义
lib_c_objs := $(patsubst lib/%.c,$(WORKPATH)/build/lib/%.o,$(lib_c_srcs))
lib_s_objs := $(patsubst lib/%.S,$(WORKPATH)/build/lib/%.o,$(lib_s_srcs))
lib_objs := $(lib_c_objs) $(lib_s_objs)

app_srcs := app/tmake.c app/shell.c
app_objs := $(patsubst app/%.c,$(WORKPATH)/build/app/%.o,$(app_srcs))
app_exe := $(patsubst app/%.c,$(WORKPATH)/build/output/%,$(app_srcs))

# 静态库文件名
lib_static := $(WORKPATH)/build/tlibc.a

# 创建静态库
$(lib_static): $(lib_objs)
	ar rcs $@ $^

# 链接出每个应用程序
$(WORKPATH)/build/output/%: $(WORKPATH)/build/app/%.o $(lib_static)
	$(LD) $(LDFLAGS) -T $(LD_SCRIPT) -o $@ $< $(lib_static)

app: $(app_exe)

glibc: clean
	mkdir -p build
	gcc -o $(WORKPATH)/build/exp_glibc $(WORKPATH)/app/exp.c -lpthread -static
	./build/exp_glibc

tlibc: __x86_64
	./build/output/exp
# ─── Git hooks（新机器自动设置）────────────────────────────

HOOKS_PATH := .githooks

# 构建时自动配置 hooks（如果尚未设置）
__x86_64: git-hooks-setup

git-hooks-setup:
	@CURRENT=$$(git config core.hooksPath 2>/dev/null); \
	if [ "$$CURRENT" != "$(HOOKS_PATH)" ]; then \
		echo "  → 初始化 Git hooks ($(HOOKS_PATH))..."; \
		git config core.hooksPath $(HOOKS_PATH); \
		chmod +x .githooks/* 2>/dev/null; \
		echo "  ✔ commit-msg 格式校验"; \
	fi

.PHONY: init-hooks check-hooks git-hooks-setup

init-hooks: git-hooks-setup
	@echo "  ✔ Git hooks 已就绪"
	@ls .githooks/ 2>/dev/null | while read f; do \
		echo "    active hook: $$f"; \
	done
	@echo "    绕过: git commit --no-verify"

check-hooks:
	@CURRENT=$$(git config core.hooksPath 2>/dev/null); \
	if [ -z "$$CURRENT" ]; then \
		echo "  ✖ Git hooks 未配置"; \
		echo "    make all 会自动设置，或运行 'make init-hooks'"; \
	elif [ "$$CURRENT" = "$(HOOKS_PATH)" ]; then \
		echo "  ✔ Git hooksPath = $$CURRENT"; \
		ls .githooks/ 2>/dev/null | while read f; do \
			echo "    active hook: $$f"; \
		done; \
	else \
		echo "  ! Git hooksPath = $$CURRENT (非项目默认)"; \
		echo "    项目期望: $(HOOKS_PATH)"; \
	fi
