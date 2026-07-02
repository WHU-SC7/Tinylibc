/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * tcc_rt_start.S — tcc 独立运行时入口
 *
 * 替代 lib/init/start.S，跳过 TLS 和内存池初始化，直接调用 main。
 * 与 ld.script 的 ENTRY(__tlibc_start) 匹配。
 */

.text
.globl __tlibc_start
.type __tlibc_start, @function
__tlibc_start:
	/* x86_64 SysV ABI: 初始栈布局
	 *   (%rsp)     = argc
	 *   8(%rsp)    = argv[0]
	 *   ...        = argv[1..N]
	 *   8+argc*8   = NULL
	 *   16+argc*8  = envp[0]
	 */
	movq	(%rsp), %rdi		/* argc */
	leaq	8(%rsp), %rsi		/* argv */
	call	main
	mov	%rax, %rdi
	mov	$60, %rax		/* SYS_exit */
	syscall
