/* SPDX-License-Identifier: MIT
 *
 * ctype.c — 字符分类与转换函数
 */

int isalpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

int isprint(int c)
{
    return c >= 0x20 && c <= 0x7e;
}

int isgraph(int c)
{
    return c > 0x20 && c <= 0x7e;
}

int isspace(int c)
{
    return c == ' '  || c == '\t' || c == '\n'
        || c == '\r' || c == '\f' || c == '\v';
}

int toupper(int c)
{
    if (c >= 'a' && c <= 'z')
        return c - 32;
    return c;
}

int tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    return c;
}

int isxdigit(int c)
{
    return (c >= '0' && c <= '9')
        || (c >= 'a' && c <= 'f')
        || (c >= 'A' && c <= 'F');
}
