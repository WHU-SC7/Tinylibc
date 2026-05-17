#ifndef __STRING_H
#define __STRING_H

char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, unsigned long n);
int strlen(const char *s);
char *strcat(char *restrict dst, const char *restrict src);
char *strncat(char *restrict dst, const char *restrict src, unsigned long n);

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, unsigned long n);

char *strchr(const char *s, int c);

unsigned long tlibc_strtoul(char *str);
void* memcpy(void* dest, const void* src, unsigned long n);

#endif