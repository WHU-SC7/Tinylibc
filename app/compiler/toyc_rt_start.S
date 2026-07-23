# SPDX-License-Identifier: MIT
# Copyright (c) 2026 BandieraRosse
#
# toyc_rt_start.S — toyc 独立运行时入口
#
# 替代 lib/init/start.S，跳过 TLS 和内存池初始化，直接调用 main。
# 与 ld.script 的 ENTRY(__tlibc_start) 匹配。
#
# 注：toyas 汇编器（compiler/toyas.c）使用，仅支持 tas 识别的指令格式。
# mov/lea 自动检测 64 位寄存器，无需 movq/leaq 后缀。

.text
.globl __tlibc_start
.type __tlibc_start, @function
__tlibc_start:
	# x86_64 SysV ABI: initial stack layout
	#   (%rsp)     = argc
	#   8(%rsp)    = argv[0]
	#   ...        = argv[1..N]
	#   8+argc*8   = NULL
	#   16+argc*8  = envp[0]
	mov	(%rsp), %rdi		# argc
	lea	8(%rsp), %rsi		# argv
	call	main
	mov	%rax, %rdi
	mov	$60, %rax		# SYS_exit
	syscall
