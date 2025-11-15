
char *strcpy(char *dest, const char *src)
{
    while((*dest++ = *src++) != '\0')
        ;
    return (char *)src;
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

int strcmp(const char *s1, const char *s2)
{
    while(*s1 && *s1==*s2)
    {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
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