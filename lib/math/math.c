/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * math.c — 数学函数实现
 *
 * 设计原则：
 *   - 超越函数用泰勒级数 + 范围归约，精度 ~1e-10～1e-12
 *   - 用 union 做 double 位操作，避免依赖 stdint.h 的 uint64_t
 *   - 每类函数独立，无全局状态
 *   - 仅使用 C89 兼容语法（无混合声明、无指派初始化器）
 *
 * 系统调用：无（纯计算）
 *
 * 索引：
 *   sqrt           牛顿迭代
 *   fabs/ceil/floor/round/trunc/fmod  直接用运算或类型转换
 *   sin/cos        范围归约到 [-π/4, π/4] → 泰勒多项式
 *   atan           二倍角归约 → 泰勒，atan2 用 atan + 象限判断
 *   exp            x = k·ln2 + r 归约 → 泰勒 → 2ᵏ 缩放
 *   log            提取指数 + 尾数 → atanh((m-1)/(m+1)) 级数
 *   pow            exp(y·log(x))，整数幂快速路径
 *   isqrt          整数牛顿迭代
 */

#include "core.h"
#include "math.h"

/* ================================================================== */
/*  位操作工具（double 的 IEEE 754 位布局）                           */
/* ================================================================== */

typedef union { double d; unsigned long long u; } dbl_t;

/* 提取符号、指数、尾数 */
#define DBL_SIGN(u)  ((int)((u) >> 63))
#define DBL_EXP(u)   ((int)(((u) >> 52) & 0x7ff) - 1023)
#define DBL_MANT(u)  ((u) & 0x000fffffffffffffULL)

/* 辅助：用 union 把 double 装入位域（C89 无指派初始化器） */
static dbl_t dbl_from_double(double x) {
    dbl_t v;
    v.d = x;
    return v;
}

/* ================================================================== */
/*  sqrt — 牛顿迭代                                                   */
/* ================================================================== */

double sqrt(double x)
{
    dbl_t v;
    double r;

    if (x <= 0.0) return 0.0;
    if (x == 1.0) return 1.0;

    /* 经典初始猜测：将 double 按整数右移 1 位 + 魔术常数 */
    /* 效果 ≈ 将指数减半，尾数近似 sqrt(m) */
    v = dbl_from_double(x);
    v.u = (v.u >> 1) + 0x1FF8000000000000ULL;
    r = v.d;

    /* 牛顿迭代（5 次已够双精度） */
    r = 0.5 * (r + x / r);
    r = 0.5 * (r + x / r);
    r = 0.5 * (r + x / r);
    r = 0.5 * (r + x / r);
    r = 0.5 * (r + x / r);
    return r;
}

/* ================================================================== */
/*  fabs — 清除符号位                                                 */
/* ================================================================== */

double fabs(double x)
{
    dbl_t v = dbl_from_double(x);
    v.u &= 0x7fffffffffffffffULL;
    return v.d;
}

/* ================================================================== */
/*  trunc / ceil / floor / round                                       */
/* ================================================================== */

double trunc(double x)
{
    dbl_t v = dbl_from_double(x);
    int e = DBL_EXP(v.u);
    unsigned long long mask;
    /* |x| ≥ 2^53 — 已无小数部分 */
    if (e >= 52) return x;
    /* |x| < 1 — 截断到 0（保留符号位） */
    if (e < 0) { v.u &= 0x8000000000000000ULL; return v.d; }

    /* 清除低 (52 - e) 位（小数部分） */
    mask = 0xffffffffffffffffULL << (52 - e);
    v.u &= mask;
    return v.d;
}

double ceil(double x)
{
    double t = trunc(x);
    /* trunc 向零舍入：x>0 时 t ≤ x，有小数则进 1 */
    if (t != x && x > 0.0) return t + 1.0;
    return t;
}

double floor(double x)
{
    double t = trunc(x);
    /* trunc 向零舍入：x<0 时 t ≥ x，有小数则退 1 */
    if (t != x && x < 0.0) return t - 1.0;
    return t;
}

double round(double x)
{
    dbl_t v = dbl_from_double(x);
    int e = DBL_EXP(v.u);
    double t, r;

    if (e >= 52) return x;
    if (e < 0) {
        /* |x| ∈ (0, 0.5) → 0; [0.5, 1) → ±1 */
        return fabs(x) >= 0.5 ? (x > 0.0 ? 1.0 : -1.0) : 0.0;
    }

    t = trunc(x);
    r = x - t;              /* 小数部分 */
    if (r >= 0.5) return t + 1.0;
    if (r <= -0.5) return t - 1.0;
    return t;
}

/* ================================================================== */
/*  fmod — x - y·trunc(x/y)                                           */
/* ================================================================== */

double fmod(double x, double y)
{
    double q;
    if (y == 0.0) return 0.0;
    q = trunc(x / y);
    return x - q * y;
}

/* ================================================================== */
/*  isqrt — 整数平方根（非 POSIX）                                    */
/* ================================================================== */

long long isqrt(long long n)
{
    long long x, y;
    if (n <= 0) return 0;
    x = n;
    y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/* ================================================================== */
/*  sin / cos 内部多项式                                              */
/*  范围归约: x → r ∈ [-π/4, π/4], n = 象限(0-3)                     */
/* ================================================================== */

/* 泰勒多项式 sin(r) = r - r³/6 + r⁵/120 - r⁷/5040 + … */
static double sin_poly(double x)
{
    double x2 = x * x;
    return x * (1.0 + x2 * (-1.0/6.0 + x2 * (1.0/120.0 + x2 *
           (-1.0/5040.0 + x2 * (1.0/362880.0 + x2 * (-1.0/39916800.0 + x2 *
           (1.0/6227020800.0 + x2 * (-1.0/1307674368000.0))))))));
}

/* 泰勒多项式 cos(r) = 1 - r²/2 + r⁴/24 - r⁶/720 + … */
static double cos_poly(double x)
{
    double x2 = x * x;
    return 1.0 + x2 * (-1.0/2.0 + x2 * (1.0/24.0 + x2 * (-1.0/720.0 + x2 *
           (1.0/40320.0 + x2 * (-1.0/3628800.0 + x2 * (1.0/479001600.0))))));
}

/* 范围归约：x → r ∈ [-π/4, π/4], n = 象限, 返回 sin(r) 或 cos(r) 带符号 */
static void sin_cos_reduce(double x, double *rv, int *quadrant)
{
    const double two_over_pi = 0.63661977236758134308;  /* 2/π */
    double n = round(x * two_over_pi);
    double r = x - n * M_PI_2;  /* r ∈ [-π/4, π/4] */
    int q = ((int)n & 3);       /* 象限 0-3 */

    if (q < 0) q += 4;

    *quadrant = q;
    *rv = r;
}

double sin(double x)
{
    double r;
    int q;
    sin_cos_reduce(x, &r, &q);

    switch (q) {
    case 0: return  sin_poly(r);
    case 1: return  cos_poly(r);
    case 2: return -sin_poly(r);
    default: return -cos_poly(r);
    }
}

double cos(double x)
{
    double r;
    int q;
    sin_cos_reduce(x, &r, &q);

    switch (q) {
    case 0: return  cos_poly(r);
    case 1: return -sin_poly(r);
    case 2: return -cos_poly(r);
    default: return  sin_poly(r);
    }
}

double tan(double x)
{
    return sin(x) / cos(x);
}

/* ================================================================== */
/*  atan / atan2                                                       */
/*  atan 策略：二倍角归约到 |x| ≤ 0.2 后用泰勒                        */
/* ================================================================== */

static double atan_poly(double x)
{
    /* atan(x) = x - x³/3 + x⁵/5 - …  for |x| ≤ 1 */
    double x2 = x * x;
    return x * (1.0 + x2 * (-1.0/3.0 + x2 * (1.0/5.0 + x2 *
           (-1.0/7.0 + x2 * (1.0/9.0 + x2 * (-1.0/11.0 + x2 *
            (1.0/13.0 + x2 * (-1.0/15.0 + x2 * (1.0/17.0)))))))));
}

double atan(double x)
{
    double r, s, result;

    if (x < 0.0) return -atan(-x);

    if (x > 1.0) return M_PI_2 - atan(1.0 / x);

    /* |x| ≤ 1: 用二倍角归约到小范围后泰勒 */
    /* atan(x) = 2·atan(x / (1 + √(1+x²)))  —— 每次约减半 */
    r = x;
    result = 0.0;  /* 仅用于抑制假告警 */

    {
        int k = 0;
        while (r > 0.2) {
            s = 1.0 + sqrt(1.0 + r * r);
            /* 防止除以 0（不会发生，因为 s ≥ 1） */
            r = r / s;
            k++;
        }

        result = atan_poly(r);
        while (k-- > 0) result *= 2.0;
    }
    return result;
}

double atan2(double y, double x)
{
    double a;

    if (x == 0.0 && y == 0.0) return 0.0;
    if (x == 0.0) return y > 0.0 ? M_PI_2 : -M_PI_2;

    a = atan(y / x);
    if (x < 0.0) {
        return y >= 0.0 ? a + M_PI : a - M_PI;
    }
    return a;
}

/* ================================================================== */
/*  exp — eˣ                                                           */
/*  归约: x = k·ln2 + r  →  eˣ = 2ᵏ·eʳ                                 */
/* ================================================================== */

double exp(double x)
{
    double k, r, r2, r4, exp_r;
    dbl_t v;
    int e;
    long long kk;

    if (x < -745.0) return 0.0;      /* 下溢 */
    if (x > 709.0) return HUGE_VAL;  /* 上溢 */

    /* x = k·ln2 + r, |r| ≤ ln2/2 */
    k = round(x * M_LOG2E);
    r = x - k * M_LN2;

    /* 泰勒: eʳ = Σ rⁿ/n! */
    r2 = r * r;
    r4 = r2 * r2;
    exp_r = 1.0 + r + r2/2.0 + r*r2/6.0 + r4/24.0 +
            r*r4/120.0 + r2*r4/720.0 + r*r2*r4/5040.0 +
            r4*r4/40320.0 + r*r4*r4/362880.0;

    /* 2ᵏ 缩放: 操作指数位 */
    v = dbl_from_double(exp_r);
    e = DBL_EXP(v.u);
    kk = (long long)k;
    if (e + kk > 1023) return HUGE_VAL;   /* 上溢 */
    if (e + kk < -1022) return 0.0;        /* 下溢 */

    v.u += (unsigned long long)kk << 52;
    return v.d;
}

/* ================================================================== */
/*  log / log10                                                        */
/*  提取指数 e 和尾数 m ∈ [1, 2)，归约到 [1/√2, √2]                  */
/*  log(m) = 2·atanh((m-1)/(m+1)) 级数                                */
/* ================================================================== */

double log(double x)
{
    dbl_t v;
    int e;
    double m, t, t2, s;

    if (x <= 0.0) return -HUGE_VAL;

    /* 提取指数 e 和尾数 m ∈ [1, 2) */
    v = dbl_from_double(x);
    e = DBL_EXP(v.u);
    v.u = (v.u & 0x800fffffffffffffULL) | 0x3ff0000000000000ULL;
    m = v.d;

    /* 归约到 [1/√2, √2] */
    if (m > M_SQRT2) { m *= 0.5; e++; }

    /* t = (m-1)/(m+1) ∈ [-0.172, 0.172] */
    t = (m - 1.0) / (m + 1.0);
    t2 = t * t;

    /* log(m) = 2·(t + t³/3 + t⁵/5 + …) */
    s = t * (1.0 + t2 * (1.0/3.0 + t2 * (1.0/5.0 + t2 *
                (1.0/7.0 + t2 * (1.0/9.0 + t2 * (1.0/11.0 + t2 *
                 (1.0/13.0 + t2 * (1.0/15.0))))))));

    return 2.0 * s + (double)e * M_LN2;
}

double log10(double x)
{
    return log(x) * M_LOG10E;
}

/* ================================================================== */
/*  pow                                                               */
/*  一般情况: xʸ = exp(y·log(x))                                      */
/*  y 为整数时用连乘                                                    */
/* ================================================================== */

double pow(double x, double y)
{
    long long n;
    int neg;

    if (x == 0.0) return (y == 0.0) ? 1.0 : 0.0;
    if (x == 1.0) return 1.0;

    /* 整数幂快速路径 */
    n = (long long)round(y);
    if ((double)n == y) {
        if (n == 0) return 1.0;
        neg = (n < 0);
        if (neg) { n = -n; x = 1.0 / x; }
        {
            double r = 1.0;
            while (n) {
                if (n & 1) r *= x;
                x *= x;
                n >>= 1;
            }
            return r;
        }
    }

    /* 一般情况: 需要 x > 0 */
    if (x < 0.0) return 0.0;  /* 负数非整数指数未定义 */
    return exp(y * log(x));
}
