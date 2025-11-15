
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