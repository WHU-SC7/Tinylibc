#ifndef __TLIBC_COMPAT_H
#define __TLIBC_COMPAT_H

// 文件 I/O 操作
#define write(fd, buf, len) __write(fd, buf, len)
#define read(fd, buf, len) __read(fd, buf, len)
#define openat(fd, pathname, flags, mode) __openat(fd, pathname, flags, mode)
#define creat(pathname, mode) __creat(pathname, mode)
#define close(fd) __close(fd)

// 目录操作
#define getdents64(fd, dirp, count) __getdents64(fd, dirp, count)
#define fstat(fd, statbuf) __fstat(fd, statbuf)
#define unlinkat(dirfd, pathname, flags) __unlinkat(dirfd, pathname, flags)
#define getcwd(buf, size) __getcwd(buf, size)
#define chdir(path) __chdir(path)
#define mkdirat(dirfd, pathname, mode) __mkdirat(dirfd, pathname, mode)
#define rmdir(pathname) __rmdir(pathname)
#define renameat(olddirfd, oldpath, newdirfd, newpath) __renameat(olddirfd, oldpath, newdirfd, newpath)
#define rename(oldpath, newpath) __rename(oldpath, newpath)

// 进程操作
#define fork() __fork()
#define exit(status) __exit(status)
#define waitpid(pid, wstatus, options) __waitpid(pid, wstatus, options)
#define wait(wstatus) __wait(wstatus)
#define execve(pathname, argv, envp) __execve(pathname, argv, envp)

// 设备控制
#define ioctl(fd, request, argp) __ioctl(fd, request, argp)

// 内存管理
#define brk(addr) __brk(addr)
#define mmap(addr, length, prot, flags, fd, offset) __mmap(addr, length, prot, flags, fd, offset)
#define munmap(addr, length) __munmap(addr, length)
#define madvise(addr, length, advice) __madvise(addr, length, advice)

// 时间相关
#define nanosleep(req, rem) __nanosleep(req, rem)
#define msleep(msecond) tlibc_msleep(msecond)
#define usleep(usecond) tlibc_usleep(usecond)
#define time(tloc) __time(tloc)
#define clock_gettime(clockid, tp) __clock_gettime(clockid, tp)
#define timespec_get(ts, base) tlibc_timespec_get(ts, base)

// 信号处理
#define sigaction(signum, act, oldact) __sigaction(signum, act, oldact)
#define sigaction_simple(signum, handler) tlibc_sigaction(signum, handler)
#define rt_sigprocmask(how, set, oldset, sigsetsize) __rt_sigprocmask(how, set, oldset, sigsetsize)
#define kill(pid, sig) __kill(pid, sig)

// 进程间通信
#define pipe2(pipefd, flags) __pipe2(pipefd, flags)

// 文件控制
#define fcntl(fd, cmd, arg) __fcntl(fd, cmd, (unsigned long)(arg))
#define dup(oldfd) __dup(oldfd)
#define dup2(oldfd, newfd) __dup2(oldfd, newfd)
#define dup3(oldfd, newfd, flags) __dup3(oldfd, newfd, flags)

// 系统调用
#define yield() __yield()
#define setsid() __setsid()
#define getpid() __getpid()
#define getrandom(buf, buflen, flags) __getrandom(buf, buflen, flags)
#define lseek(fd, offset, whence) __lseek(fd, offset, whence)
#define ftruncate(fd, length) __ftruncate(fd, length)
#define readlinkat(dirfd, pathname, buf, bufsiz) __readlinkat(dirfd, pathname, buf, bufsiz)

// 线程相关
#define clone(fn, stack, flags, arg, parent_tid, tls, child_tid) __clone(fn, stack, flags, arg, parent_tid, tls, child_tid)
#define gettid() __gettid()
#define futex(uaddr, futex_op, val, timeout, uaddr2, val3) __futex(uaddr, futex_op, val, timeout, uaddr2, val3)
#define clone_thread(stack) tlibc_clone_thread(stack)
#define exit_group(status) __exit_group(status)

// 字符串操作
#define memset(dst, value, n) __memset(dst, value, n)
#define memmove(dest, src, n) __memmove(dest, src, n)

// 系统接口
#define getuid() tlibc_getuid()
#define chmod(pathname, mode) tlibc_chmod(pathname, mode)
#define stat(pathname, statbuf) tlibc_stat(pathname, statbuf)

#endif