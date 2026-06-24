#include "syscall.h"
#include "syscall_num.h"
#include "tlibc.h"
#include "core.h"

typedef __builtin_va_list my_va_list;
#define my_va_start(v, l)   __builtin_va_start(v, l)
#define my_va_arg(v, t)     __builtin_va_arg(v, t)
#define my_va_end(v)        __builtin_va_end(v)

/* ================================================================== */
/*  Format specifier parsing — width, flags (-, 0)                    */
/* ================================================================== */

#define FMT_FLAG_MINUS  1   /* 左对齐 */
#define FMT_FLAG_ZERO   2   /* 零填充 */

struct fmt_spec {
    int flags;
    int width;
};

/* 解析 flags（-, 0）和 width，返回指向格式字符的指针 */
static const char *
parse_fmt(const char *p, struct fmt_spec *spec)
{
    spec->flags = 0;
    spec->width = 0;

    for (;;) {
        if      (*p == '-') { spec->flags |= FMT_FLAG_MINUS; p++; }
        else if (*p == '0') { spec->flags |= FMT_FLAG_ZERO;  p++; }
        else break;
    }

    while (*p >= '0' && *p <= '9') {
        spec->width = spec->width * 10 + (*p - '0');
        p++;
    }

    return p;
}

/* 重复输出字符 count 次到 fd */
static void
pad_output(int fd, int count, char c)
{
    char buf[64];
    while (count > 0) {
        int n = count > 64 ? 64 : count;
        __memset(buf, c, n);
        __write(fd, buf, n);
        count -= n;
    }
}

/* 字符串长度 */
static int
str_len(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* 将 long 转换为字符串缓冲区，返回长度（buf[0] = '-' 表示负数） */
static int
long_to_buf(long num, char *buf)
{
    if (num == 0) {
        buf[0] = '0';
        return 1;
    }
    int neg = 0;
    if (num < 0) {
        neg = 1;
        num = -num;
    }
    char tmp[32];
    int i = 0;
    while (num > 0) {
        tmp[i++] = '0' + (num % 10);
        num /= 10;
    }
    if (neg) {
        buf[0] = '-';
        for (int j = 0; j < i; j++)
            buf[1 + j] = tmp[i - 1 - j];
        return i + 1;
    } else {
        for (int j = 0; j < i; j++)
            buf[j] = tmp[i - 1 - j];
        return i;
    }
}

/* ================================================================== */
/*  原始辅助函数（保留不变）                                           */
/* ================================================================== */

/* Writes a decimal integer to a file descriptor. Internal helper for printf */
static void fprint_int(int fd, int num)
{

    char buf[32];
    char c;
    int count=0;
    __memset((void *)buf,0,32);

    //处理负数
    if(num < 0)
    {
        num = -num;
        char *negative = "-";
        __write(fd,negative,1);
    }
    if(num == 0)
    {
        count = 1;
        buf[0] = '0';
    }
    while(num!=0)
    {
        c = num % 10; //从i最低位开始，计算每一位的数字
        buf[count++] = c + 48; //向缓冲区写入对应字符，48表示字符0
        num /= 10;
    }

    char tmp;
    for(int i=0; i < count/2; i++) //反转，让字符串顺序正确
    {
        tmp = buf[i];
        buf[i] = buf[(count-1)-i];
        buf[(count-1)-i] = tmp;
    }

    __write(fd,buf,count);
}

//printf
void tlibc_print_int(int num)
{
    fprint_int(STDOUT, num);
}

// 辅助函数：计算 10 的 n 次方（n <= 9 即可，因为小数位数通常不多）
static long long power_of_10(int n) {
    long long r = 1;
    for (int i = 0; i < n; i++) r *= 10;
    return r;
}

// 辅助函数：将 unsigned long 转换为字符串（无符号）
static int ulong_to_str(unsigned long n, char *buf) {
    int i = 0;
    char tmp[32];
    do {
        tmp[i++] = '0' + (n % 10);
        n /= 10;
    } while (n);
    int len = i;
    for (int j = 0; j < len; j++) {
        buf[j] = tmp[len - 1 - j];
    }
    return len;
}

// 将 double 转换为字符串，保留 dec 位小数（默认6），写入 buf，返回长度
static int double_to_str(double d, char *buf, int dec) {
    int len = 0;
    // 处理负数
    if (d < 0) {
        buf[len++] = '-';
        d = -d;
    }

    // 取整数部分和小数部分
    unsigned long integer = (unsigned long)d;
    double frac = d - integer;

    // 四舍五入：先按指定位数放大，加0.5取整，再处理进位
    long long frac_part = (long long)(frac * power_of_10(dec) + 0.5);
    if (frac_part >= power_of_10(dec)) {
        integer++;
        frac_part = 0;
    }

    // 输出整数部分
    len += ulong_to_str(integer, buf + len);

    // 输出小数点
    buf[len++] = '.';

    // 输出小数部分，补零到 dec 位
    char frac_buf[16];
    int frac_len = ulong_to_str(frac_part, frac_buf);
    for (int i = 0; i < dec - frac_len; i++) {
        buf[len++] = '0';
    }
    for (int i = 0; i < frac_len; i++) {
        buf[len++] = frac_buf[i];
    }

    return len;
}

/* ================================================================== */
/*  带宽度/对齐的填充输出函数                                         */
/* ================================================================== */

/* 输出字符串，支持宽度和左对齐 */
static void
fprint_string_padded(int fd, const char *s, const struct fmt_spec *spec)
{
    if (!s) s = "(null)";
    int len = str_len(s);
    int pad = spec->width > len ? spec->width - len : 0;

    if (pad > 0 && !(spec->flags & FMT_FLAG_MINUS))
        pad_output(fd, pad, ' ');
    __write(fd, s, len);
    if (pad > 0 && (spec->flags & FMT_FLAG_MINUS))
        pad_output(fd, pad, ' ');
}

/* 输出整数，支持宽度、左对齐、零填充 */
static void
fprint_int_padded(int fd, long num, const struct fmt_spec *spec)
{
    char buf[32];
    int len = long_to_buf(num, buf);
    int pad = spec->width > len ? spec->width - len : 0;

    if (pad > 0 && (spec->flags & FMT_FLAG_MINUS)) {
        /* 左对齐：内容后补空格 */
        __write(fd, buf, len);
        pad_output(fd, pad, ' ');
    } else if (pad > 0 && (spec->flags & FMT_FLAG_ZERO)) {
        /* 零填充右对齐：负号在前，零在后 */
        int offset = (buf[0] == '-') ? 1 : 0;
        if (offset) __write(fd, "-", 1);
        pad_output(fd, pad, '0');
        __write(fd, buf + offset, len - offset);
    } else {
        /* 空格填充右对齐（或无需填充） */
        if (pad > 0) pad_output(fd, pad, ' ');
        __write(fd, buf, len);
    }
}

/* 输出十六进制，支持宽度、对齐、零填充 */
static void
fprint_hex_padded(int fd, unsigned long val, int prefix,
                  const struct fmt_spec *spec)
{
    const char *hex = "0123456789abcdef";
    char buf[18];
    int pos = 0;

    if (prefix) {
        buf[pos++] = '0';
        buf[pos++] = 'x';
    }

    if (val == 0) {
        buf[pos++] = '0';
    } else {
        int started = 0;
        for (int i = (sizeof(unsigned long) * 2) - 1; i >= 0; i--) {
            unsigned char nibble = (val >> (i * 4)) & 0xf;
            if (nibble || started) {
                buf[pos++] = hex[nibble];
                started = 1;
            }
        }
    }

    int pad = spec->width > pos ? spec->width - pos : 0;
    char pc = (spec->flags & FMT_FLAG_ZERO) ? '0' : ' ';

    if (pad > 0 && (spec->flags & FMT_FLAG_MINUS)) {
        __write(fd, buf, pos);
        pad_output(fd, pad, ' ');
    } else if (pad > 0) {
        pad_output(fd, pad, pc);
        __write(fd, buf, pos);
    } else {
        __write(fd, buf, pos);
    }
}

/* ================================================================== */
/*  核心格式化引擎                                                     */
/* ================================================================== */

static void
vfprintf_core(int fd, const char *fmt, my_va_list args)
{
    for (const char *p = fmt; *p; p++) {
        /* ---- 普通文本：一次写出一段 ---- */
        if (*p != '%') {
            const char *start = p;
            while (*p && *p != '%')
                p++;
            int count = p - start;
            __write(fd, start, count);
            if (!*p) break;         /* 到末尾，结束 */
            p = start + count - 1;  /* 指向 % 前一个字符，for 循环 p++ 后到 % */
            continue;
        }

        /* ---- 格式说明符 ---- */
        struct fmt_spec spec;
        p = parse_fmt(p + 1, &spec);  /* p 指向格式字符 */

        switch (*p) {
            case 'd': {
                /* %d 读取 int（4 字节），符合 x86_64 可变参数传参约定 */
                int n = my_va_arg(args, int);
                fprint_int_padded(fd, (long)n, &spec);
                break;
            }
            case 'l':
                if (*(p + 1) == 'd') {
                    p++;
                    long n = my_va_arg(args, long);
                    fprint_int_padded(fd, n, &spec);
                } else {
                    /* 裸 %l（无 d），容错 */
                    p--;
                    long n = my_va_arg(args, long);
                    fprint_int_padded(fd, n, &spec);
                }
                break;
            case 's': {
                char *s = my_va_arg(args, char*);
                fprint_string_padded(fd, s, &spec);
                break;
            }
            case 'c': {
                char c = (char)my_va_arg(args, int);
                if (spec.width > 1 && !(spec.flags & FMT_FLAG_MINUS))
                    pad_output(fd, spec.width - 1, ' ');
                __write(fd, &c, 1);
                if (spec.width > 1 && (spec.flags & FMT_FLAG_MINUS))
                    pad_output(fd, spec.width - 1, ' ');
                break;
            }
            case 'f': {
                double d = my_va_arg(args, double);
                char buf[128];
                int len = double_to_str(d, buf, 6);
                int pad = spec.width > len ? spec.width - len : 0;
                if (pad > 0 && !(spec.flags & FMT_FLAG_MINUS))
                    pad_output(fd, pad, ' ');
                __write(fd, buf, len);
                if (pad > 0 && (spec.flags & FMT_FLAG_MINUS))
                    pad_output(fd, pad, ' ');
                break;
            }
            case 'x': {
                unsigned long n = my_va_arg(args, unsigned long);
                fprint_hex_padded(fd, n, 0, &spec);
                break;
            }
            case 'p': {
                void *ptr = my_va_arg(args, void *);
                fprint_hex_padded(fd, (unsigned long)ptr, 1, &spec);
                break;
            }
            case 'u': {
                unsigned long n = my_va_arg(args, unsigned long);
                char buf[32];
                int len = ulong_to_str(n, buf);
                int pad = spec.width > len ? spec.width - len : 0;
                char pc = (spec.flags & FMT_FLAG_ZERO) ? '0' : ' ';
                if (pad > 0 && (spec.flags & FMT_FLAG_MINUS)) {
                    __write(fd, buf, len);
                    pad_output(fd, pad, ' ');
                } else if (pad > 0) {
                    pad_output(fd, pad, pc);
                    __write(fd, buf, len);
                } else {
                    __write(fd, buf, len);
                }
                break;
            }
            case '%':
                __write(fd, "%", 1);
                break;
            default:
                __write(fd, p - 1, 2);  /* 输出 %x */
                break;
        }
    }
}

void __fprintf(int fd, const char *fmt, ...) {
    my_va_list args;
    my_va_start(args, fmt);
    vfprintf_core(fd, fmt, args);
    my_va_end(args);
}

void __printf(const char *fmt, ...) {
    my_va_list args;
    my_va_start(args, fmt);
    vfprintf_core(STDOUT, fmt, args);
    my_va_end(args);
}
