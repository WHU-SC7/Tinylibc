#include "syscall.h"
#include "syscall_num.h"
#include "tlibc.h"
#include "core.h"

typedef __builtin_va_list my_va_list;
#define my_va_start(v, l)   __builtin_va_start(v, l)
#define my_va_arg(v, t)     __builtin_va_arg(v, t)
#define my_va_end(v)        __builtin_va_end(v)

//printf
void print_int(int num)
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
        __write(STDOUT,negative,1);
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
    
    __write(STDOUT,buf,count);
}

void print_long(long num)
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
        __write(STDOUT,negative,1);
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
    
    __write(STDOUT,buf,count);
}

void print_string(const char *str)
{
    int count = 0;
    char *str_calcu = (char *)str; //计算字符个数
    while(*str_calcu++)
        count++;
    __write(STDOUT,str,count);
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

void __printf(const char *fmt, ...) {
    my_va_list args;
    my_va_start(args, fmt);   // args 指向第一个可变参数

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            char *str_end = (char *)p;
            while(*str_end!='%' && *str_end)
            {
                str_end++;
            }
            int count = str_end-p; //计算输出字符的个数
            __write(STDOUT,p,count);
            p = p+(count-1); //跳过已经输出的字符
            continue;
        }

        switch (*++p) {
            case 'd': {
                long n = my_va_arg(args, long);  // %d 默认提升为 int，但为了简单用 long
                print_int(n);
                break;
            }
            case 'l':
                if (*++p == 'd') {
                    long n = my_va_arg(args, long);
                    print_int(n);
                } else {
                    p--;  // 回退
                    long n = my_va_arg(args, long); //原来大量使用了%l
                    print_int(n);
                    // 可扩展其他 %l 格式
                }
                break;
            case 's': {
                char *s = my_va_arg(args, char*);
                if (s) print_string(s);
                break;
            }
            case 'c': {
                char c = (char)my_va_arg(args, int);
                __write(STDOUT, &c, 1);
                break;
            }
            case 'f': {
                double d = my_va_arg(args, double);   // float 会自动提升为 double
                char buf[128];                         // 足够容纳浮点字符串
                int len = double_to_str(d, buf, 6);    // 默认6位小数
                __write(STDOUT, buf, len);
                break;
            }
            default:
                __write(STDOUT, p-1, 2);  // 输出 %x
                break;
        }
    }
    my_va_end(args);
}