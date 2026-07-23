#include "syscall.h"
#include "syscall_num.h"
#include "core.h"
#include "tlibc_everything.h"

typedef __builtin_va_list my_va_list;
#define my_va_start(v, l)   __builtin_va_start(v, l)
#define my_va_arg(v, t)     __builtin_va_arg(v, t)
#define my_va_end(v)        __builtin_va_end(v)

/* ================================================================== */
/*  Format specifier parsing — width, flags (-, 0)                    */
/* ================================================================== */

#define FMT_FLAG_MINUS  1
#define FMT_FLAG_ZERO   2

struct fmt_spec {
    int flags;
    int width;
    int precision;   /* -1 = 未指定 */
};

static const char *
parse_fmt(const char *p, struct fmt_spec *spec)
{
    spec->flags = 0;
    spec->width = 0;
    spec->precision = -1;
    for (;;) {
        if      (*p == '-') { spec->flags |= FMT_FLAG_MINUS; p++; }
        else if (*p == '0') { spec->flags |= FMT_FLAG_ZERO;  p++; }
        else break;
    }
    while (*p >= '0' && *p <= '9') {
        spec->width = spec->width * 10 + (*p - '0');
        p++;
    }
    if (*p == '.') {
        p++;
        if (*p == '*') {
            spec->precision = -2;   /* 标记：精度从参数列表取 */
            p++;
        } else {
            spec->precision = 0;
            while (*p >= '0' && *p <= '9') {
                spec->precision = spec->precision * 10 + (*p - '0');
                p++;
            }
        }
    }
    return p;
}

/* ================================================================== */
/*  字符串缓冲区结构                                                   */
/* ================================================================== */

typedef struct {
    char *buffer;      // 目标缓冲区
    size_t size;       // 缓冲区总大小
    size_t pos;        // 当前写入位置
    int truncated;     // 是否发生截断
} strbuf_t;

// 向字符串缓冲区写入指定长度的数据，返回应写入的总长度（C99 约定）
static void strbuf_write(strbuf_t *sb, const char *data, size_t len) {
    if (!sb->buffer || sb->size == 0) {
        // 没有缓冲区或大小为0，只计数
        sb->pos += len;
        sb->truncated = 1;
        return;
    }

    if (sb->pos >= sb->size) {
        // 缓冲区已满，只计数不写入
        sb->pos += len;
        sb->truncated = 1;
        return;
    }

    size_t remaining = sb->size - sb->pos;

    if (len < remaining) {
        // 空间足够
        memcpy(sb->buffer + sb->pos, data, len);
        sb->pos += len;
    } else {
        // 空间不足，写入能放下的部分（留一个位置给 null 终止符）
        if (remaining > 1) {
            memcpy(sb->buffer + sb->pos, data, remaining - 1);
        }
        sb->pos += len;  // 累加完整长度，保证返回值为"应写长度"
        sb->truncated = 1;
    }
}

// 向字符串缓冲区写入单个字符
static void strbuf_write_char(strbuf_t *sb, char c) {
    strbuf_write(sb, &c, 1);
}

// 字符串缓冲区填充（重复字符）
static void
strbuf_pad(strbuf_t *sb, int count, char c)
{
    while (count > 0) {
        int n = count > 64 ? 64 : count;
        char buf[64];
        __memset(buf, c, n);
        strbuf_write(sb, buf, n);
        count -= n;
    }
}

// 将数字转换为字符串并写入缓冲区（有符号）
static void strbuf_write_long(strbuf_t *sb, long num) {
    char buf[32];
    char c;
    int count = 0;
    __memset(buf, 0, 32);

    // 处理负数
    if (num < 0) {
        strbuf_write_char(sb, '-');
        num = -num;
    }

    if (num == 0) {
        strbuf_write_char(sb, '0');
        return;
    }

    while (num != 0) {
        c = num % 10;
        buf[count++] = c + '0';
        num /= 10;
    }

    // 反转字符串
    char tmp;
    for (int i = 0; i < count / 2; i++) {
        tmp = buf[i];
        buf[i] = buf[(count - 1) - i];
        buf[(count - 1) - i] = tmp;
    }

    strbuf_write(sb, buf, count);
}

// 向字符串缓冲区写入整数
void strbuf_write_int(strbuf_t *sb, int num) {
    strbuf_write_long(sb, (long)num);
}

// 计算10的n次方
static long long power_of_10(int n) {
    long long r = 1;
    for (int i = 0; i < n; i++) r *= 10;
    return r;
}

// 无符号整数转字符串
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

// 向字符串缓冲区写入浮点数
static void strbuf_write_double(strbuf_t *sb, double d, int dec) {
    char buf[128];
    int len = 0;

    // 处理负数
    if (d < 0) {
        buf[len++] = '-';
        d = -d;
    }

    // 取整数部分和小数部分
    unsigned long integer = (unsigned long)d;
    double frac = d - integer;

    // 四舍五入
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

    strbuf_write(sb, buf, len);
}

/* ================================================================== */
/*  带宽度/对齐的 strbuf 输出函数                                     */
/* ================================================================== */

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

static void
strbuf_write_string_padded(strbuf_t *sb, const char *s, const struct fmt_spec *spec)
{
    if (!s) s = "(null)";
    int len = 0;
    while (s[len]) len++;
    if (spec->precision >= 0 && spec->precision < len)
        len = spec->precision;
    int pad = spec->width > len ? spec->width - len : 0;
    if (pad > 0 && !(spec->flags & FMT_FLAG_MINUS))
        strbuf_pad(sb, pad, ' ');
    strbuf_write(sb, s, len);
    if (pad > 0 && (spec->flags & FMT_FLAG_MINUS))
        strbuf_pad(sb, pad, ' ');
}

static void
strbuf_write_int_padded(strbuf_t *sb, long num, const struct fmt_spec *spec)
{
    char buf[32];
    int len = long_to_buf(num, buf);
    int sign = (len > 0 && buf[0] == '-') ? 1 : 0;
    int digits = len - sign;

    if (spec->precision == 0 && num == 0) {
        digits = 0; sign = 0;
    }
    int prec_zeros = 0;
    if (spec->precision > digits)
        prec_zeros = spec->precision - digits;

    int content = sign + prec_zeros + digits;
    int pad = spec->width > content ? spec->width - content : 0;
    int zero_flag = (spec->flags & FMT_FLAG_ZERO) && (spec->precision < 0);

    if (pad > 0 && (spec->flags & FMT_FLAG_MINUS)) {
        if (sign) strbuf_write_char(sb, '-');
        if (prec_zeros) strbuf_pad(sb, prec_zeros, '0');
        if (digits) strbuf_write(sb, buf + sign, digits);
        if (pad) strbuf_pad(sb, pad, ' ');
    } else if (pad > 0 && zero_flag) {
        if (sign) strbuf_write_char(sb, '-');
        if (prec_zeros) strbuf_pad(sb, prec_zeros, '0');
        if (pad) strbuf_pad(sb, pad, '0');
        if (digits) strbuf_write(sb, buf + sign, digits);
    } else {
        if (pad) strbuf_pad(sb, pad, ' ');
        if (sign) strbuf_write_char(sb, '-');
        if (prec_zeros) strbuf_pad(sb, prec_zeros, '0');
        if (digits) strbuf_write(sb, buf + sign, digits);
    }
}

static void
strbuf_write_hex_padded(strbuf_t *sb, unsigned long val, int prefix,
                        const struct fmt_spec *spec, int upper)
{
    const char *hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
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

    int hex_start = prefix ? 2 : 0;
    int hex_digits = pos - hex_start;

    if (spec->precision == 0 && val == 0) {
        pos = prefix ? 2 : 0;
        hex_digits = 0;
    }
    if (spec->precision > hex_digits) {
        int extra = spec->precision - hex_digits;
        for (int i = pos - 1; i >= hex_start; i--)
            buf[i + extra] = buf[i];
        for (int i = 0; i < extra; i++)
            buf[hex_start + i] = '0';
        pos += extra;
    }

    int pad = spec->width > pos ? spec->width - pos : 0;
    int zero_flag = (spec->flags & FMT_FLAG_ZERO) && (spec->precision < 0);

    if (pad > 0 && (spec->flags & FMT_FLAG_MINUS)) {
        strbuf_write(sb, buf, pos);
        strbuf_pad(sb, pad, ' ');
    } else if (pad > 0 && zero_flag && prefix) {
        strbuf_write(sb, buf, 2);
        strbuf_pad(sb, pad, '0');
        strbuf_write(sb, buf + 2, pos - 2);
    } else if (pad > 0) {
        char pc = zero_flag ? '0' : ' ';
        strbuf_pad(sb, pad, pc);
        strbuf_write(sb, buf, pos);
    } else {
        strbuf_write(sb, buf, pos);
    }
}

// 核心函数：格式化输出到字符串缓冲区
static void vsnprintf_core(strbuf_t *sb, const char *fmt, my_va_list args) {
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            const char *start = p;
            while (*p && *p != '%') p++;
            int count = p - start;
            strbuf_write(sb, start, count);
            if (!*p) break;
            p = start + count - 1;
            continue;
        }

        struct fmt_spec spec;
        p = parse_fmt(p + 1, &spec);

        /* `.*` 精度：从参数列表取 int */
        if (spec.precision == -2) {
            int prec = my_va_arg(args, int);
            spec.precision = prec >= 0 ? prec : -1;
        }

        /* 长度修饰符：l / ll */
        int l_cnt = 0;
        while (*p == 'l') { if (l_cnt < 2) l_cnt++; p++; }

        switch (*p) {
            case 'd':
            case 'i': {
                if (l_cnt >= 2) {
                    long long n = my_va_arg(args, long long);
                    strbuf_write_int_padded(sb, (long)n, &spec);
                } else if (l_cnt == 1) {
                    long n = my_va_arg(args, long);
                    strbuf_write_int_padded(sb, n, &spec);
                } else {
                    int n = my_va_arg(args, int);
                    long ln = (long)n;
                    strbuf_write_int_padded(sb, ln, &spec);
                }
                break;
            }
            case 's': {
                char *s = my_va_arg(args, char*);
                strbuf_write_string_padded(sb, s, &spec);
                break;
            }
            case 'c': {
                char c = (char)my_va_arg(args, int);
                if (spec.width > 1 && !(spec.flags & FMT_FLAG_MINUS))
                    strbuf_pad(sb, spec.width - 1, ' ');
                strbuf_write_char(sb, c);
                if (spec.width > 1 && (spec.flags & FMT_FLAG_MINUS))
                    strbuf_pad(sb, spec.width - 1, ' ');
                break;
            }
            case 'f': {
                double d = my_va_arg(args, double);
                int dec = spec.precision >= 0 ? spec.precision : 6;
                /* 先用 null 缓冲区预计算长度 */
                strbuf_t tmp = {NULL, 0, 0, 0};
                strbuf_write_double(&tmp, d, dec);
                int len = (int)tmp.pos;
                int pad = spec.width > len ? spec.width - len : 0;
                if (pad > 0 && !(spec.flags & FMT_FLAG_MINUS))
                    strbuf_pad(sb, pad, ' ');
                strbuf_write_double(sb, d, dec);
                if (pad > 0 && (spec.flags & FMT_FLAG_MINUS))
                    strbuf_pad(sb, pad, ' ');
                break;
            }
            case 'x':
            case 'X': {
                unsigned long n;
                if (l_cnt >= 2) {
                    unsigned long long nn = my_va_arg(args, unsigned long long);
                    n = (unsigned long)nn;
                } else {
                    n = my_va_arg(args, unsigned long);
                }
                strbuf_write_hex_padded(sb, n, 0, &spec, *p == 'X');
                break;
            }
            case 'p': {
                void *ptr = my_va_arg(args, void *);
                strbuf_write_hex_padded(sb, (unsigned long)ptr, 1, &spec, 0);
                break;
            }
            case 'u': {
                unsigned long n;
                if (l_cnt >= 2) {
                    unsigned long long nn = my_va_arg(args, unsigned long long);
                    n = (unsigned long)nn;
                } else {
                    unsigned int un = my_va_arg(args, unsigned int);
                    n = (unsigned long)un;
                }
                char buf[32];
                int len = ulong_to_str(n, buf);
                if (spec.precision == 0 && n == 0) len = 0;
                int prec_zeros = spec.precision > len ? spec.precision - len : 0;
                int content = prec_zeros + len;
                int pad = spec.width > content ? spec.width - content : 0;
                int zero_flag = (spec.flags & FMT_FLAG_ZERO) && (spec.precision < 0);
                char pc = zero_flag ? '0' : ' ';
                if (pad > 0 && (spec.flags & FMT_FLAG_MINUS)) {
                    if (prec_zeros) strbuf_pad(sb, prec_zeros, '0');
                    if (len) strbuf_write(sb, buf, len);
                    if (pad) strbuf_pad(sb, pad, ' ');
                } else if (pad > 0 && zero_flag) {
                    if (pad) strbuf_pad(sb, pad, '0');
                    if (prec_zeros) strbuf_pad(sb, prec_zeros, '0');
                    if (len) strbuf_write(sb, buf, len);
                } else {
                    if (pad) strbuf_pad(sb, pad, pc);
                    if (prec_zeros) strbuf_pad(sb, prec_zeros, '0');
                    if (len) strbuf_write(sb, buf, len);
                }
                break;
            }
            case '%':
                strbuf_write_char(sb, '%');
                break;
            default:
                strbuf_write_char(sb, '%');
                strbuf_write_char(sb, *p);
                break;
        }
    }
}

// snprintf 主函数
int snprintf(char *str, size_t size, const char *format, ...) {
    if (size == 0) {
        // size为0时，只计算所需长度，不写入数据
        strbuf_t sb = {NULL, 0, 0, 0};
        my_va_list args;
        my_va_start(args, format);
        vsnprintf_core(&sb, format, args);
        my_va_end(args);
        return (int)sb.pos;
    }

    strbuf_t sb = {str, size, 0, 0};
    my_va_list args;
    my_va_start(args, format);
    vsnprintf_core(&sb, format, args);
    my_va_end(args);

    // 添加字符串结束符
    if (sb.pos < size) {
        str[sb.pos] = '\0';
    } else {
        str[size - 1] = '\0';
    }

    // 返回实际需要的字符数（不包括结束符）
    return (int)sb.pos;
}
