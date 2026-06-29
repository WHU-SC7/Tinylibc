#ifndef __STRING_H
#define __STRING_H

#include "tlibc_types.h"  /* size_t */

char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
size_t strlen(const char *s);
char *strcat(char *restrict dst, const char *restrict src);
char *strncat(char *restrict dst, const char *restrict src, size_t n);

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);

size_t tlibc_strtoul(char *str);
void *memcpy(void *dest, const void *src, size_t n);
char *itoa(int num, char *str, int radix);

char *strtok_r(char *str, const char *delim, char **save_ptr);
char *strtok(char *str, const char *delim);

size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strpbrk(const char *s, const char *accept);

int memcmp(const void *s1, const void *s2, size_t n);

char *strerror(int errnum);

void *__memset(void *dst, int value, size_t n);
void *__memmove(void *dest, const void *src, size_t n);

#endif
