
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

char *strchr(const char *s, int c){
    while(*s)
    {
        if(*s == c)
            return (char *)s;
        s++;
    }
    return (void *)-1;
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