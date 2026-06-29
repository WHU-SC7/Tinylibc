#ifndef __STDLIB_H
#define __STDLIB_H

#include "tlibc_types.h"  /* size_t */

int atoi(const char *nptr);
long atol(const char *nptr);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);

int abs(int j);
long labs(long j);

#endif
