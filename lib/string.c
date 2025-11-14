
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