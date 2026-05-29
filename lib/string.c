
char *strcpy(char *dest, const char *src)
{
    while((*dest++ = *src++) != '\0')
        ;
    return (char *)src;
}

char *strncpy(char *dest, const char *src, unsigned long n)
{
    unsigned long i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

int strlen(const char *s)
{
    int len = 0;
    while(*s++)
        len++;
    return len;
}

char *strcat(char *restrict dst, const char *restrict src)
{
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strncat(char *restrict dst, const char *restrict src, unsigned long n)
{
    unsigned long dst_len = strlen(dst);
    unsigned long i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dst[dst_len + i] = src[i];
    }
    dst[dst_len + i] = '\0';
    return dst;
}

int strcmp(const char *s1, const char *s2)
{
    while(*s1 && *s1==*s2)
    {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, unsigned long n)
{
    for (unsigned long i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
        if (s1[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

char *strchr(const char *s, int c) {
    // 转换为 char，标准要求比较字符
    char target = (char)c;
    
    while (*s) {
        if (*s == target)
            return (char *)s;
        s++;
    }
    
    // 检查结束符 '\0'
    if (target == '\0')
        return (char *)s;  // 指向末尾的 '\0'
    
    return (void *)0;
}

char *strrchr(const char *s, int c) {
    char target = (char)c;
    const char *last_occurrence = (void *)0;  // 使用 NULL 而不是 (void*)0
    
    // 先处理查找 '\0' 的情况
    if (target == '\0') {
        while (*s)  // 找到字符串末尾
            s++;
        return (char *)s;  // 此时 s 指向 '\0'
    }
    
    // 查找普通字符
    while (*s) {
        if (*s == target)
            last_occurrence = s;
        s++;
    }
    
    return (char *)last_occurrence;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle)  // 空字符串直接返回原字符串
        return (char *)haystack;
    
    while (*haystack) {
        if (*haystack == *needle) {
            const char *h = haystack + 1;
            const char *n = needle + 1;
            while (*n && *h && *h == *n) {
                h++;
                n++;
            }
            if (!*n)  // 完全匹配
                return (char *)haystack;
        }
        haystack++;
    }
    return (void *)0;  // 未找到
}

//私有实现，把字符串转换成10进制unsigned long
unsigned long tlibc_strtoul(char *str)
{
    unsigned long ret = 0;
    int i=0;
    while(*str)
    {
        if(i>12)
            return (long)-1;
        unsigned long num = *str-48;
        ret *= 10;
        ret += num;
        str++;
        i++;
    }
    return ret;
}

void* memcpy(void* dest, const void* src, unsigned long n)
{
    char* d = (char*)dest;
    const char* s = (const char*)src;
    
    for (unsigned long i = 0; i < n; i++) {
        d[i] = s[i];
    }
    
    return dest;
}

/**
 * 将整数转换为字符串
 * @param num 要转换的整数
 * @param str 存储结果的字符串缓冲区
 * @param radix 进制（2-36）
 * @return 返回 str 指针
 */
char* itoa(int num, char* str, int radix) {
    char* ptr = str;
    int is_negative = 0;
    char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char temp[33];  // 32位整数最多32位+符号+结束符
    int i = 0;
    
    // 处理0的特殊情况
    if (num == 0) {
        *ptr++ = '0';
        *ptr = '\0';
        return str;
    }
    
    // 处理负数（仅十进制）
    if (num < 0 && radix == 10) {
        is_negative = 1;
        num = -num;
    }
    
    // 转换数字
    while (num != 0) {
        temp[i++] = digits[num % radix];
        num /= radix;
    }
    
    // 添加负号
    if (is_negative) {
        temp[i++] = '-';
    }
    
    // 反转字符串
    while (i > 0) {
        *ptr++ = temp[--i];
    }
    *ptr = '\0';
    
    return str;
}

char *strerror(int errnum) {
    // 静态字符串数组，存储错误信息
    static const char *error_strings[] = {
        "Success",                    // 0
        "Operation not permitted",    // 1
        "No such file or directory",  // 2
        "No such process",            // 3
        "Interrupted system call",    // 4
        "Input/output error",         // 5
        "No such device or address",  // 6
        "Argument list too long",     // 7
        "Exec format error",          // 8
        "Bad file descriptor",        // 9
        "No child processes",         // 10
        "Resource temporarily unavailable", // 11
        "Cannot allocate memory",     // 12
        "Permission denied",          // 13
        "Bad address",                // 14
        "Block device required",      // 15
        "Device or resource busy",    // 16
        "File exists",                // 17
        "Invalid cross-device link",  // 18
        "No such device",             // 19
        "Not a directory",            // 20
        "Is a directory",             // 21
        "Invalid argument",           // 22
        "Too many open files in system", // 23
        "Too many open files",        // 24
        "Inappropriate ioctl for device", // 25
        "Text file busy",             // 26
        "File too large",             // 27
        "No space left on device",    // 28
        "Illegal seek",               // 29
        "Read-only file system",      // 30
        "Too many links",             // 31
        "Broken pipe",                // 32
        "Numerical argument out of domain", // 33
        "Numerical result out of range",    // 34
        "Resource deadlock avoided",  // 35
        "File name too long",         // 36
        "No locks available",         // 37
        "Function not implemented",   // 38
        "Directory not empty",        // 39
        "Too many levels of symbolic links", // 40
        "Unknown error"               // 默认（未知错误）
    };
    
    int num_errors = sizeof(error_strings) / sizeof(error_strings[0]) - 1;
    
    // 根据错误码返回对应的错误信息
    if (errnum >= 0 && errnum < num_errors) {
        return (char *)error_strings[errnum];
    } else {
        // 返回未知错误，静态缓冲区可以存储更长的信息
        static char unknown_error[64];
int snprintf(char *str, unsigned long size, const char *format, ...);
        snprintf(unknown_error, sizeof(unknown_error), "Unknown error %d", errnum);
        return unknown_error;
    }
}