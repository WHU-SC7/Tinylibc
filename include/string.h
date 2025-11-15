#ifndef __STRING_H
#define __STRING_H

char *strcpy(char *dest, const char *src);
int strlen(const char *s);
char *strcat(char *restrict dst, const char *restrict src);

int strcmp(const char *s1, const char *s2);

#endif