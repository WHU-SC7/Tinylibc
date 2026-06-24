//为了理解，这个文件要从下往上阅读！
#ifndef _SYSCALL_H_
#define _SYSCALL_H_

#include "syscall_arch.h" // __提供syscall0等一系列的汇编内联函数

#define __scc(X) ((long)(X))

#define __syscall1(n, a) __syscall1(n, __scc(a))
#define __syscall2(n, a, b) __syscall2(n, __scc(a), __scc(b))
#define __syscall3(n, a, b, c) __syscall3(n, __scc(a), __scc(b), __scc(c))
#define __syscall4(n, a, b, c, d)                                              \
	__syscall4(n, __scc(a), __scc(b), __scc(c), __scc(d))
#define __syscall5(n, a, b, c, d, e)                                           \
	__syscall5(n, __scc(a), __scc(b), __scc(c), __scc(d), __scc(e))
#define __syscall6(n, a, b, c, d, e, f)                                        \
	__syscall6(n, __scc(a), __scc(b), __scc(c), __scc(d), __scc(e),        \
		   __scc(f))

// 取第九个参数(一定是数字！)
#define __SYSCALL_NARGS_X(a,b,c,d,e,f,g,h,n,...) n
// 根据8个数字的数字序列(7,6,5,4,3,2,1,0)，计算参数个数。
// __VA_ARGS__是传入的syscall的参数，包括调用号。每多一个参数，__SYSCALL_NARGS_X指向的数字就会+1
#define __SYSCALL_NARGS(...) \
	__SYSCALL_NARGS_X(__VA_ARGS__,7,6,5,4,3,2,1,0,)

// 宏拼接，比如__SYSCALL_CONCAT(__syscall,4)会生成__syscall4
// !必须使用两层展开，只使用#define __SYSCALL_CONCAT(a,b)会编译出错！
#define __SYSCALL_CONCAT_X(a, b) a##b
#define __SYSCALL_CONCAT(a, b) __SYSCALL_CONCAT_X(a, b)

// 这里的b固定是__syscall。__SYSCALL_DISP会根据参数个数(假设是4)把__syscall变成对应的__syscall4
#define __SYSCALL_DISP(b,...) \
	__SYSCALL_CONCAT(b, __SYSCALL_NARGS(__VA_ARGS__)) \
	(__VA_ARGS__)

// #define syscall(...) __SYSCALL_DISP(__syscall,__VA_ARGS__)
#define __syscall(...) __SYSCALL_DISP(__syscall, __VA_ARGS__)
#define syscall(...) __syscall(__VA_ARGS__)

// syscall(SYS_write,fd,buf,len)会被替换成syscall4(SYS_write,fd,buf,len). syscall4是内联汇编函数

#endif
