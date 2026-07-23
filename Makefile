# SPDX-License-Identifier: MIT
# Copyright (c) 2026 BandieraRosse

# ─── Toolchain ─────────────────────────────────────────────────────────
export TOOLPREFIX =x86_64-linux-gnu-

export CC  = ${TOOLPREFIX}gcc
export AS  = ${TOOLPREFIX}gcc
export LD  = ${TOOLPREFIX}ld
export AR  = ${TOOLPREFIX}ar
export OBJCOPY = ${TOOLPREFIX}objcopy
export OBJDUMP = ${TOOLPREFIX}objdump

# ─── 编译选项 ───────────────────────────────────────────────────────────
CFLAGS += -MD -fno-stack-protector -fno-omit-frame-pointer
CFLAGS += -ggdb3 -Wall -Werror -O0
CFLAGS += -fno-common -nostdlib -nostartfiles -nodefaultlibs
CFLAGS += -ffreestanding -fno-pie -mno-red-zone
CFLAGS += -Iarch -Iinclude -Iinclude/posix -Iinclude/tlibc -Iarch/x86_64
CFLAGS += -DX86_64_TLIBC=1

# ─── 链接选项 ────────────────────────────────────────────────────────────
LDFLAGS += -z max-page-size=4096 -nostdlib -static
LD_SCRIPT = ld.script

export WORKPATH = $(shell pwd)

# 头文件路径（给 toyc 用，不含 -Wall 等 gcc 特有选项）
TOYC_FLAGS = -DX86_64_TLIBC=1 -Iarch -Iinclude -Iinclude/posix -Iinclude/tlibc -Iarch/x86_64

# ═════════════════════════════════════════════════════════════════════
# 四阶段构建
#
#  Phase 1 (gcc):   toyc 工具链（app/compiler/，独立运行时，无 lib）
#  Phase 2 (toyc):  编译 lib/ → tlibc.a
#  Phase 3 (gcc):   tmake + shell，链接 toyc 编译的 tlibc.a
#  Phase 4 (toyc):  tmake -T 自托管构建全部 app
# ═════════════════════════════════════════════════════════════════════

# ─── 源文件 ─────────────────────────────────────────────────────────────
compiler_c_srcs := $(wildcard app/compiler/*.c)
compiler_s_srcs := $(wildcard app/compiler/*.S)

lib_c_srcs := $(shell find lib -name '*.c')
lib_s_srcs := $(shell find lib -name '*.S')

# ─── Phase 1: gcc → toyc 工具链 ─────────────────────────────────────────
# 编译器源文件用简化标志：代码为 toyc 编写，对 gcc -Wall 不友好。
COMPILER_CFLAGS := $(filter-out -Wall -Werror,$(CFLAGS))

$(WORKPATH)/build/app/compiler/%.o: app/compiler/%.c
	$(CC) -c $(COMPILER_CFLAGS) -MF $(WORKPATH)/build/app/compiler/$*.d -o $@ $<

$(WORKPATH)/build/app/compiler/%.o: app/compiler/%.S
	$(CC) -c $(COMPILER_CFLAGS) -MF $(WORKPATH)/build/app/compiler/$*.d -o $@ $<

# 链接 toyc 工具链（所有工具独立运行时，不依赖 tlibc.a）
C := $(WORKPATH)/build/app/compiler

TOYC_OBJS := $(C)/toyc.o $(C)/toyc_rt.o $(C)/toyc_rt_start.o \
             $(C)/lex.o $(C)/parse.o $(C)/preproc.o $(C)/cgen.o \
             $(C)/cgen_expr.o $(C)/cgen_asm.o $(C)/cgen_float_hack.o \
             $(C)/elf_write.o

$(WORKPATH)/build/output/toyc: $(TOYC_OBJS)
	$(LD) $(LDFLAGS) -T $(LD_SCRIPT) -o $@ $^

$(WORKPATH)/build/output/toyas: $(C)/toyas.o $(C)/elf_write.o \
                                $(C)/toyc_rt.o $(C)/toyc_rt_start.o
	$(LD) $(LDFLAGS) -T $(LD_SCRIPT) -o $@ $^

$(WORKPATH)/build/output/toyld: $(C)/toyld.o $(C)/toyc_rt.o $(C)/toyc_rt_start.o
	$(LD) $(LDFLAGS) -T $(LD_SCRIPT) -o $@ $^

$(WORKPATH)/build/output/toyar: $(C)/toyar.o $(C)/toyc_rt.o $(C)/toyc_rt_start.o
	$(LD) $(LDFLAGS) -T $(LD_SCRIPT) -o $@ $^

$(WORKPATH)/build/output/toypp: $(C)/toypp.o $(C)/toyc_rt.o $(C)/toyc_rt_start.o \
                                $(C)/preproc.o
	$(LD) $(LDFLAGS) -T $(LD_SCRIPT) -o $@ $^

PHASE1_OUTPUTS := $(WORKPATH)/build/output/toyc $(WORKPATH)/build/output/toyas \
                  $(WORKPATH)/build/output/toyld $(WORKPATH)/build/output/toyar \
                  $(WORKPATH)/build/output/toypp

phase1: init_dir $(PHASE1_OUTPUTS)

# ─── Phase 3: gcc → tmake + shell ──────────────────────────────────────
# （Phase 2 在 __x86_64 的 recipe 中用 toyc 完成）

$(WORKPATH)/build/%.o: %.c
	$(CC) -c $(CFLAGS) -MF $(WORKPATH)/build/$*.d -o $@ $<

$(WORKPATH)/build/%.o: %.S
	$(CC) -c $(CFLAGS) -MF $(WORKPATH)/build/$*.d -o $@ $<

$(WORKPATH)/build/output/tmake: $(WORKPATH)/build/app/tmake.o
	$(LD) $(LDFLAGS) -T $(LD_SCRIPT) -o $@ $< $(WORKPATH)/build/tlibc.a

$(WORKPATH)/build/output/shell: $(WORKPATH)/build/app/shell.o
	$(LD) $(LDFLAGS) -T $(LD_SCRIPT) -o $@ $< $(WORKPATH)/build/tlibc.a

# ═════════════════════════════════════════════════════════════════════
#  入口
# ═════════════════════════════════════════════════════════════════════

all: __x86_64

.PHONY: all __x86_64 init_dir clean phase1

# 四阶段二重奏
__x86_64: clean init_dir
	@printf "\033[34m══════ Phase 1: gcc → toyc 工具链 ══════\033[0m\n\n"
	$(MAKE) phase1 -j$$(nproc)
	@printf "\n\033[34m══════ Phase 2: toyc → tlibc.a ══════\033[0m\n\n"
	TOYC="$(WORKPATH)/build/output/toyc"; \
	TOYAS="$(WORKPATH)/build/output/toyas"; \
	TOYAR="$(WORKPATH)/build/output/toyar"; \
	ok=0; fail=0; \
	for f in $(lib_c_srcs); do \
		out="$(WORKPATH)/build/$${f%.c}.o"; \
		mkdir -p $$(dirname $$out); \
		if $$TOYC $(TOYC_FLAGS) $$f -o $$out 2>/dev/null; then \
			ok=$$((ok+1)); \
		else \
			printf "  \033[31mFAIL\033[0m %s\n" "$$f"; \
			fail=$$((fail+1)); \
		fi; \
	done; \
	for f in $(lib_s_srcs); do \
		out="$(WORKPATH)/build/$${f%.S}.o"; \
		mkdir -p $$(dirname $$out); \
		if $$TOYAS $$f -o $$out 2>/dev/null; then \
			ok=$$((ok+1)); \
		else \
			printf "  \033[31mFAIL\033[0m %s\n" "$$f"; \
			fail=$$((fail+1)); \
		fi; \
	done; \
	printf "\n  lib: %d ok, %d fail\n" $$ok $$fail; \
	if [ $$fail -gt 0 ]; then exit 1; fi; \
	$(CC) -c $(filter-out -Wall -Werror -MD,$(CFLAGS)) lib/misc/file.c -o $(WORKPATH)/build/lib/misc/file.o 2>/dev/null; \
	$(AR) rcs $(WORKPATH)/build/tlibc.a \
		$$(find $(WORKPATH)/build/lib -name '*.o' | sort) 2>/dev/null; \
	printf "  tlibc.a: %d bytes\n\n" $$(stat -c%s $(WORKPATH)/build/tlibc.a 2>/dev/null || echo 0)
	@printf "\033[34m══════ Phase 3: gcc → tmake + shell ══════\033[0m\n\n"
	$(MAKE) $(WORKPATH)/build/output/tmake $(WORKPATH)/build/output/shell
	cp build/output/tmake tmp/
	@printf "\n\033[34m======== Phase 4: toyc -> all apps (fallback gcc) ========\033[0m\n\n"
	TOYC="$(WORKPATH)/build/output/toyc"; \
	TOYAS="$(WORKPATH)/build/output/toyas"; \
	TOYLD="$(WORKPATH)/build/output/toyld"; \
	ALL_APPS=$$(find app -name '*.c' ! -path 'app/compiler/*' | sort); \
	ok=0; fail=0; \
	for f in $$ALL_APPS; do \
		base=$$(basename $$f .c); \
		obj="$(WORKPATH)/build/$${f%.c}.o"; \
		mkdir -p $$(dirname $$obj) 2>/dev/null; \
		if $$TOYC $(TOYC_FLAGS) $$f -o $$obj 2>/dev/null; then \
			compiler="toyc"; \
		elif $(CC) -c $(CFLAGS) $$f -o $$obj 2>/dev/null; then \
			compiler="gcc"; \
		else printf "  FAIL %s\n" $$f; fail=$$((fail+1)); continue; fi; \
		exe="$(WORKPATH)/build/output/$$base"; \
		if $$TOYLD $$obj $(WORKPATH)/build/lib/init/start.o $(WORKPATH)/build/tlibc.a -o $$exe 2>/dev/null; then \
			linker="toyld"; \
		elif $(LD) $(LDFLAGS) -T $(LD_SCRIPT) -o $$exe $$obj $(WORKPATH)/build/tlibc.a 2>/dev/null; then \
			linker="ld"; \
		else printf "  LINK FAIL %s\n" $$base; fail=$$((fail+1)); continue; fi; \
		ok=$$((ok+1)); \
	done; \
	printf "\n  apps: %d ok, %d fail\n\n" $$ok $$fail
	@if [ -f /etc/sudoers.d/tlibc-setcap ]; then \
		$(MAKE) setcap; \
	else \
		echo ""; \
		echo "  ℹ ndiscover/netprobe/sniffer/ping 需要 CAP_NET_RAW 才能免 sudo 运行。"; \
		echo "    安装 sudo 免密规则后会自动设置："; \
		echo "    sudo cp sudoers.d-tlibc-setcap /etc/sudoers.d/tlibc-setcap && sudo chmod 440 /etc/sudoers.d/tlibc-setcap"; \
		echo ""; \
	fi

init_dir:
	mkdir -p build
	mkdir -p build/app build/app/compiler build/lib build/output
	mkdir -p build/lib/core build/lib/stdio build/lib/thread build/lib/net \
	          build/lib/math build/lib/misc build/lib/init build/lib/graphics build/lib/audio
	mkdir -p tmp

# ─── 常用操作 ───────────────────────────────────────────────────────────

run: all
	./tmp/shell

clean:
	rm -rf build tmp

dis_file := build/dis_tlibc

disassemb:
	$(OBJDUMP) -S -C $(WORKPATH)/build/tlibc_x64 > $(dis_file)
	@echo "反汇编得到文件 $(dis_file)"

debug: all
	strace $(WORKPATH)/build/tlibc_x64

# ─── CAP_NET_RAW（ndiscover / netprobe / sniffer / ping） ──────────────
SETCAP := /usr/sbin/setcap
ifeq ($(wildcard $(SETCAP)),)
SETCAP := /sbin/setcap
endif

setcap:
	@echo "  → 设置 CAP_NET_RAW 权限..."
	@for bin in ndiscover netprobe sniffer ping; do \
		installed=~/tlibc/bin/$$bin; \
		buildout=build/output/$$bin; \
		if [ -f $$installed ]; then \
			sudo "$(SETCAP)" cap_net_raw+ep $$installed 2>/dev/null && echo "    ✔ $$installed" || echo "    ! $$installed (skip)"; \
		fi; \
		if [ -f $$buildout ]; then \
			sudo "$(SETCAP)" cap_net_raw+ep $$buildout 2>/dev/null || true; \
		fi; \
	done

# ─── 对比实验 ───────────────────────────────────────────────────────────

glibc: clean
	mkdir -p build
	gcc -o $(WORKPATH)/build/exp_glibc $(WORKPATH)/app/exp.c -lpthread -static
	./build/exp_glibc

tlibc: __x86_64
	./build/output/exp

# ─── Git hooks ─────────────────────────────────────────────────────────

HOOKS_PATH := .githooks

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

# ─── 旧版 toyc-all 入口（保留兼容） ─────────────────────────────────────
.PHONY: toyc-all
toyc-all:
	@echo "  toyc-all 已合并到 make all"
	@$(MAKE) all
