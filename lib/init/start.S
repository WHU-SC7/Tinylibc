.globl __tlibc_start
.extern tlibc_init

__tlibc_start:
    # 此时 rsp 指向 argc
    mov     (%rsp), %rdi        
    lea     8(%rsp), %rsi       
    lea     16(%rsp, %rdi, 8), %rdx  
    
    call    tlibc_init
    
    # 退出系统调用 —— 用 main() 的返回值作退出码
    mov     %rax, %rdi          # status = tlibc_init 的返回值（即 main 的返回值）
    mov     $60, %rax           # sys_exit
    syscall