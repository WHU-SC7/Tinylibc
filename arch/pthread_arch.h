#ifndef __PTHREAD_ARCH_H
#define __PTHREAD_ARCH_H

/*
 * 架构相关的线程本地存储（TLS）访问。
 *
 * 返回当前线程的 struct pthread 指针。
 *
 * x86_64:   fs 段寄存器指向 TLS 区域，%fs:0 处存放指向自身的指针
 * aarch64:  tpidr_el0 系统寄存器保存 TLS 基址
 * 其他架构：添加对应的 #elif 分支
 */

struct pthread;   /* 前向声明，完整定义在 pthread.h */

static inline struct pthread *__tlibc_thread_self(void)
{
    struct pthread *self;
    __asm__("mov %%fs:0, %0" : "=r"(self));
    return self;
}

#endif /* __PTHREAD_ARCH_H */
