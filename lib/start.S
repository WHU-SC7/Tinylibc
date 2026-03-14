.globl __tlibc_start

__tlibc_start:
    # 此时 rsp 指向 argc
    mov     (%rsp), %rdi        # 第一个参数：argc
    lea     8(%rsp), %rsi       # 第二个参数：argv（跳过 argc）
    lea     16(%rsp, %rdi, 8), %rdx  # 第三个参数：envp（跳过 argc 和 argv 数组）
    
    call    main
    
    # 退出系统调用
    mov     $60, %rax           # sys_exit
    xor     %rdi, %rdi          # status = 0
    syscall