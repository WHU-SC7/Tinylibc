
static __inline long __syscall0(long n)
{
	unsigned long ret;
	__asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
	return ret;
}

static __inline long __syscall1(long n, long a1)
{
	unsigned long ret;
	__asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
	return ret;
}

static __inline long __syscall2(long n, long a1, long a2)
{
	unsigned long ret;
	__asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2)
						  : "rcx", "r11", "memory");
	return ret;
}

static __inline long __syscall3(long n, long a1, long a2, long a3)
{
	unsigned long ret;
	__asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
						  "d"(a3) : "rcx", "r11", "memory");
	return ret;
}

static __inline long __syscall4(long n, long a1, long a2, long a3, long a4)
{
	unsigned long ret;
	__asm__ __volatile__ (
		"mov %5, %%r10\n\tsyscall"
		: "=a"(ret)
		: "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(a4)
		: "rcx", "r11", "r10", "memory");
	return ret;
}

static __inline long __syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
	unsigned long ret;
	__asm__ __volatile__ (
		"mov %5, %%r10\n\tmov %6, %%r8\n\tsyscall"
		: "=a"(ret)
		: "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(a4), "r"(a5)
		: "rcx", "r11", "r10", "r8", "memory");
	return ret;
}

static __inline long __syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
	unsigned long ret;
	__asm__ __volatile__ (
		"mov %5, %%r10\n\tmov %6, %%r8\n\tmov %7, %%r9\n\tsyscall"
		: "=a"(ret)
		: "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(a4), "r"(a5), "r"(a6)
		: "rcx", "r11", "r10", "r8", "r9", "memory");
	return ret;
}
