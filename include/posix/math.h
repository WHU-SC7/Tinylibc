/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * math.h — 数学函数（POSIX 兼容层）
 *
 * 用法：
 *   #include <math.h>
 *   链接时自动包含 lib/math/math.o（tmake 自动扫描 lib/）
 *
 * 实现说明：
 *   所有超越函数用泰勒 / 牛顿迭代实现，精度约 1e-10～1e-12。
 *   不保证 IEEE 754 边界行为（NaN、inf 处理）。
 *
 * 当前实现的函数（Phase 1+2）：
 *   sqrt, fabs, ceil, floor, round, trunc, fmod  —— 基础运算
 *   isqrt(扩展)                                     —— 整数平方根
 *   sin, cos, tan, atan, atan2                     —— 三角
 *   exp, log, log10, pow                           —— 指数/对数
 */

#ifndef _MATH_H
#define _MATH_H

#define HUGE_VAL __builtin_huge_val()

#define M_E        2.71828182845904523536
#define M_LOG2E    1.44269504088896340736
#define M_LOG10E   0.43429448190325182765
#define M_LN2      0.69314718055994530942
#define M_LN10     2.30258509299404568402
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.78539816339744830962
#define M_1_PI     0.31830988618379067154
#define M_2_PI     0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2    1.41421356237309504880
#define M_SQRT1_2  0.70710678118654752440

double sqrt(double x);
double fabs(double x);
double ceil(double x);
double floor(double x);
double round(double x);
double trunc(double x);
double fmod(double x, double y);

double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);

double exp(double x);
double log(double x);
double log10(double x);
double pow(double x, double y);

/* 整数平方根（非 POSIX，tinylibc 扩展） */
long long isqrt(long long n);

#endif /* _MATH_H */
