/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 */

/*
 * lex.c — C 词法分析器
 *
 * 机制：将 C 源文件字符流转换为 Token 流。支持标准 C 全部关键字和运算符。
 * 系统调用：read（由 main.c 负责读取源文件）
 *
 * 索引：
 *   lexer_init         初始化词法分析器
 *   lexer_next         获取下一个 Token（跳过空白/注释）
 *     skip_comment     跳过行注释和块注释
 *     read_number      整数字面量
 *     read_ident       标识符/关键字
 *   keyword_lookup     标识符关键字映射
 */

/* 文件结束标志（非 EOF 宏，避免与 TOK_EOF 冲突）。
 * 直接使用字面值 (-1)，不依赖宏展开
 * （toyc 自编译时预处理器有展开 bug）。 */

#include "toyc.h"

/* ─── 关键字表 ─── */

typedef struct {
    const char *word;
    TokenKind kind;
} Keyword;

static Keyword keywords[] = {
    {"__asm__",                   TOK__ASM__},
    {"__attribute__",             TOK__ATTRIBUTE__},
    {"__builtin_huge_val",        TOK__BUILTIN_HUGE_VAL},
    {"__builtin_huge_valf",       TOK__BUILTIN_HUGE_VALF},
    {"__builtin_va_arg",          TOK__BUILTIN_VA_ARG},
    {"__builtin_va_end",          TOK__BUILTIN_VA_END},
    {"__builtin_va_list",         TOK__BUILTIN_VA_LIST},
    {"__builtin_va_start",        TOK__BUILTIN_VA_START},
    {"__const__",                 TOK_CONST},
    {"__inline",                  TOK_INLINE},
    {"__inline__",                TOK_INLINE},
    {"__restrict__",              TOK_RESTRICT},
    {"__signed__",                TOK_SIGNED},
    {"__volatile__",              TOK_VOLATILE},
    {"auto",                      TOK_IDENT},
    {"break",                     TOK_BREAK},
    {"case",                      TOK_CASE},
    {"char",                      TOK_CHAR},
    {"const",                     TOK_CONST},
    {"continue",                  TOK_CONTINUE},
    {"default",                   TOK_DEFAULT},
    {"do",                        TOK_DO},
    {"double",                    TOK_DOUBLE},
    {"else",                      TOK_ELSE},
    {"enum",                      TOK_ENUM},
    {"extern",                    TOK_EXTERN},
    {"float",                     TOK_FLOAT},
    {"for",                       TOK_FOR},
    {"goto",                      TOK_GOTO},
    {"if",                        TOK_IF},
    {"inline",                    TOK_INLINE},
    {"int",                       TOK_INT},
    {"long",                      TOK_LONG},
    {"register",                  TOK_REGISTER},
    {"restrict",                  TOK_RESTRICT},
    {"return",                    TOK_RETURN},
    {"short",                     TOK_SHORT},
    {"signed",                    TOK_SIGNED},
    {"sizeof",                    TOK_SIZEOF},
    {"static",                    TOK_STATIC},
    {"struct",                    TOK_STRUCT},
    {"switch",                    TOK_SWITCH},
    {"typedef",                   TOK_TYPEDEF},
    {"union",                     TOK_UNION},
    {"unsigned",                  TOK_UNSIGNED},
    {"void",                      TOK_VOID},
    {"volatile",                  TOK_VOLATILE},
    {"while",                     TOK_WHILE},
};

#define KEYWORD_COUNT (sizeof(keywords) / sizeof(keywords[0]))

static TokenKind keyword_lookup(const char *start, int len) {
    int i;
    for (i = 0; i < KEYWORD_COUNT; i++) {
        const char *k = keywords[i].word;
        int j;
        for (j = 0; j < len; j++) {
            if (k[j] == '\0' || k[j] != start[j])
                goto next;
        }
        if (k[j] == '\0')
            return keywords[i].kind;
next:;
    }
    return TOK_IDENT;
}

/* ─── 初始化和字符操作 ─── */

void lexer_init(Lexer *lx, const char *src, int len, const char *fname) {
    lx->start = src;
    lx->pos = src;
    lx->end = src + len;
    lx->filename = fname;
    lx->line = 1;
    lx->col = 1;
    lx->lex_err = 0;
    lx->cur.kind = TOK_EOF;
}

static int is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

static void advance(Lexer *lx) {
    if (*lx->pos == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    lx->pos++;
}

static int input_peek(Lexer *lx) {
    return (lx->pos < lx->end) ? (unsigned char)*lx->pos : (-1);
}

/* ─── 跳过注释 ─── */

static void skip_comment(Lexer *lx, int style) {
    if (style == '/') {
        /* // 行注释 */
        while (input_peek(lx) != '\n' && input_peek(lx) != (-1))
            advance(lx);
    } else {
        /* 块注释：跳过直到遇见星号斜线 */
        advance(lx); /* 跳过 *，当前在星号后 */
        while (1) {
            int c = input_peek(lx);
            if (c == (-1))
                return;
            if (c == '*' && lx->pos + 1 < lx->end && *(lx->pos + 1) == '/') {
                advance(lx);
                advance(lx);
                return;
            }
            advance(lx);
        }
    }
}

/* ─── 64 位整数算术辅助（2 × u32，仅 32 位运算） ─── */
/* 非 static：被 cgen_expr.c 在代码生成时调用以解析 float 字面量 */

void u64_mul10(unsigned int *hi, unsigned int *lo) {
    unsigned int lo2 = *lo << 1;
    unsigned int hi2 = (*hi << 1) | (*lo >> 31);
    unsigned int lo8 = *lo << 3;
    unsigned int hi8 = (*hi << 3) | (*lo >> 29);
    unsigned int r_lo = lo2 + lo8;
    unsigned int r_hi = hi2 + hi8 + (r_lo < lo2 ? 1 : 0);
    *lo = r_lo; *hi = r_hi;
}

unsigned int u64_div10(unsigned int *hi, unsigned int *lo) {
    /* (hi * 2^32 + lo) / 10 = (hi/10) * 2^32 + (hi%10) * 429496729 + (hi%10 * 6 + lo) / 10
     * 其中 2^32 = 10 * 429496729 + 6 */
    unsigned int old_lo = *lo;
    unsigned int q_hi = *hi / 10;
    unsigned int r_hi = *hi % 10;
    unsigned int extra = old_lo / 10 + (r_hi * 6 + old_lo % 10) / 10;
    unsigned int q_lo = r_hi * 429496729U + extra;
    unsigned int carry = (q_lo < r_hi * 429496729U) ? 1 : 0;
    *hi = q_hi + carry;
    *lo = q_lo;
    return (r_hi * 6 + old_lo % 10) % 10;
}

void u64_add_digit(unsigned int *hi, unsigned int *lo, unsigned int d) {
    unsigned int new_lo = *lo + d;
    *hi += (new_lo < *lo ? 1 : 0);
    *lo = new_lo;
}

/* ─── 十进制字符串 → IEEE 754 双精度位模式（纯 32 位整数运算） ─── */

void parse_float_literal(const char *s, int len,
                                 unsigned int *out_lo, unsigned int *out_hi) {
    int i = 0;
    int sign_flag = 0;

    if (i < len && s[i] == '-') { sign_flag = 1; i++; }
    else if (i < len && s[i] == '+') { i++; }
    while (i < len && s[i] == '0') i++;

    unsigned int m_lo = 0, m_hi = 0;
    int total_digits = 0;
    int seen_dot = 0;
    int int_digits = 0;            /* 整数部分位数（小数点前的位数） */

    while (i < len) {
        if (s[i] >= '0' && s[i] <= '9') {
            if (total_digits < 18) {
                u64_mul10(&m_hi, &m_lo);
                u64_add_digit(&m_hi, &m_lo, (unsigned)(s[i] - '0'));
                total_digits++;
            }
            if (!seen_dot) int_digits++;
            i++;
        } else if (s[i] == '.') { seen_dot = 1; i++; }
        else { break; }
    }

    int exp10 = 0, exp_sign = 1;
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < len && s[i] == '-') { exp_sign = -1; i++; }
        else if (i < len && s[i] == '+') { i++; }
        while (i < len && s[i] >= '0' && s[i] <= '9') {
            exp10 = exp10 * 10 + (s[i] - '0'); i++;
        }
    }

    int frac_in_m = total_digits - int_digits;
    if (frac_in_m < 0) { frac_in_m = 0; }
    /* 当整数部分超过 18 位限制时，补偿被截断的高位 */
    int int_excess = (int_digits > total_digits) ? (int_digits - total_digits) : 0;
    int exp_total = exp10 * exp_sign - frac_in_m + int_excess;
    if (total_digits == 0 || (m_hi == 0 && m_lo == 0)) {
        *out_lo = 0; *out_hi = sign_flag ? 0x80000000U : 0; return;
    }

    /* 应用正指数 */
    if (exp_total > 0) {
        int k;
        for (k = 0; k < exp_total; k++) {
            if (m_hi > 0xFFFFFFFFU / 10) break;
            u64_mul10(&m_hi, &m_lo);
        }
    }

    /* 处理十进制除法：M / 10^|exp_total| */
    unsigned int R_hi = 0, R_lo = 0;  /* 余数累加器（R × 10 + digit） */
    unsigned int D_hi = 0, D_lo = 0;  /* 分母累加器 */
    int divs = (exp_total < 0) ? -exp_total : 0;
    if (divs > 0) {
        D_lo = 1;
        /* 做 divs 次 ÷10，先收集所有余数再逆序重建 R。
         * 余数从 LSB 开始出（314159 → 9,5,1,4,1），
         * 逆序处理得 R=14159。 */
        unsigned int rems[20];
        int di;
        for (di = 0; di < divs && di < 20; di++)
            rems[di] = u64_div10(&m_hi, &m_lo);
        for (di = divs - 1; di >= 0; di--) {
            if (di < divs - 1) u64_mul10(&R_hi, &R_lo);
            u64_add_digit(&R_hi, &R_lo, rems[di]);
            u64_mul10(&D_hi, &D_lo);
        }
    }

    /* 找 m（= Q，商）的最高位 */
    unsigned int tmp;
    int bit_pos = 0;
    if (m_hi > 0) { tmp = m_hi; bit_pos = 32; }
    else { tmp = m_lo; }
    { int _b = 0; unsigned int _t = tmp;
      while (_t > 1) { _t >>= 1; _b++; } bit_pos += _b; }

    /* 若 Q == 0 但有余数，则数字为纯分数 < 1.0（如 0.9）。
     * 需在 R/D 的二进制展开中找前导 1 的位置来设定负的 bit_pos，
     * 使后续尾数展开提供足够的位数。 */
    if (m_hi == 0 && m_lo == 0 && divs > 0 && (R_hi > 0 || R_lo > 0)) {
        unsigned int r_hi = R_hi, r_lo = R_lo;
        unsigned int d_hi = D_hi, d_lo = D_lo;
        int lz = 0;
        while (lz < 2048) {
            r_hi = (r_hi << 1) | (r_lo >> 31);
            r_lo <<= 1;
            if (r_hi > d_hi || (r_hi == d_hi && r_lo >= d_lo)) {
                /* 找到前导 1：位置 -lz-1（如 0.9→-1, 0.1→-4） */
                unsigned int sub_lo = r_lo - d_lo;
                unsigned int sub_hi = r_hi - d_hi - (sub_lo > r_lo ? 1 : 0);
                R_hi = sub_hi; R_lo = sub_lo;
                break;
            }
            lz++;
        }
        bit_pos = -(lz + 1);
    }

    /* Q == 0（纯分数）时前导 1 已在上述过程中从 R/D 中提取，
     * R 已更新为剩余小数。mbits 固定为 52（只占 IEEE 52 位尾数），
     * 避免负 bit_pos 导致展开产生多余的溢出位。 */

    /* 构建 52 位 IEEE 尾数
     * 公式: mantissa = (Q - 2^P) × 2^(52-P) + R/D × 2^(52-P)
     * 其中 P = bit_pos, Q = m_hi/m_lo, R = R_hi/R_lo, D = D_hi/D_lo */
    unsigned int mant_lo = 0, mant_hi = 0;
    int mbits = 52 - bit_pos;  /* 尾数中需要的位数（减去前导 1 后的剩余） */
    if (m_hi == 0 && m_lo == 0 && bit_pos < 0) mbits = 52;

    if (mbits > 0) {
        /* 计算整数部分: (Q - 2^P) × 2^(mbits) 作为 64 位值 */
        /* 去掉 Q 的前导 1，用 mbits 位表示 */
        unsigned int q_hi = m_hi, q_lo = m_lo;
        /* 从 Q 的 bit_pos 位去掉前导 1 */
        if (bit_pos < 32) {
            /* 前导 1 在 lo 中 */
            q_lo &= ~(1U << bit_pos);
        } else {
            /* 前导 1 在 hi 中 */
            q_hi &= ~(1U << (bit_pos - 32));
        }
        /* 左移 mbits 位 */
        { int _si; for (_si = 0; _si < mbits; _si++) {
            q_hi = (q_hi << 1) | (q_lo >> 31); q_lo <<= 1;
        }}
        mant_hi = q_hi & 0xFFFFFU;
        mant_lo = q_lo;

        /* 加上分数部分: R/D × 2^(mbits) */
        if (divs > 0 && (R_hi > 0 || R_lo > 0)) {
            /* R/D 是分数，做 mbits+1 位二进制展开后加到尾数上 */
            unsigned int r_hi = R_hi, r_lo = R_lo;
            unsigned int d_hi = D_hi, d_lo = D_lo;
            int bi;
            /* 生成最多 mbits 个二进制位 */
            unsigned int carry = 0;
            unsigned int extra = 0;  /* 余数是否还有非零部分 */
            for (bi = 0; bi < mbits + 1; bi++) {
                /* r = r * 2 */
                r_hi = (r_hi << 1) | (r_lo >> 31);
                r_lo <<= 1;
                /* 判断 r >= D */
                int ge = (r_hi > d_hi) || (r_hi == d_hi && r_lo >= d_lo);
                if (ge) {
                    /* r = r - D */
                    unsigned int sub_lo = r_lo - d_lo;
                    unsigned int sub_hi = r_hi - d_hi - (sub_lo > r_lo ? 1 : 0);
                    r_lo = sub_lo; r_hi = sub_hi;
                    if (bi < mbits) {
                        /* 这个位是 1，加到 mant 的 (mbits-1-bi) 位置 */
                        if ((mbits - 1 - bi) < 32) {
                            mant_lo |= (1U << (mbits - 1 - bi));
                        } else {
                            mant_hi |= (1U << (mbits - 1 - bi - 32));
                        }
                    } else {
                        /* 第 mbits 个位用于舍入 */
                        carry = 1;
                    }
                } else if (bi == mbits && r_lo == 0 && r_hi == 0) {
                }
                if (r_hi > 0 || r_lo > 0) extra = 1;
            }
            /* 舍入 */
            if (carry || extra) {
                /* 简单舍入：如果下一个位是 1 或有余数，尾数 += 1 */
                unsigned int old_lo = mant_lo;
                mant_lo += 1;
                if (mant_lo < old_lo) {
                    mant_hi++;
                    if (mant_hi & (1U << 20)) {  /* mantissa overflow */
                        mant_hi = 0;
                        bit_pos++;
                    }
                }
                mant_hi &= 0xFFFFFU;
            }
        }
    }

    int ieee_exp = 1023 + bit_pos;
    unsigned int hi = (sign_flag ? 0x80000000U : 0U)
                    | ((unsigned int)(ieee_exp & 0x7FF) << 20)
                    | mant_hi;
    *out_lo = mant_lo; *out_hi = hi;
}

/* ─── 读数字 ─── */

static Token read_number(Lexer *lx) {
    Token t;
    t.start = lx->pos - 1;  /* pos 已在 lexer_next 中前移过首位 */
    t.kind = TOK_NUMBER;
    t.ival = 0;
    t.dval = 0.0;
    t.is_float = 0;
    t.is_unsigned = 0;
    t.sval = 0;

    int base = 10;
    int maybe_float = 0;

    /* 使用 unsigned long 累加，避免 >= 2^63 的值在有符号 long 中溢出 */
    unsigned long uval = 0;

    if (t.start[0] == '0' && (input_peek(lx) == 'x' || input_peek(lx) == 'X')) {
        base = 16;
        advance(lx);
    } else if (t.start[0] == '0' && is_digit(input_peek(lx))) {
        /* 以 0 开头且后跟数字：八进制字面量（如 0777 = 511） */
        base = 8;
        uval = 0;
    } else if (t.start[0] == '.') {
        /* 以 . 开头的浮点数：.5 .25 等 */
        maybe_float = 1;
    } else if (is_digit(t.start[0])) {
        uval = t.start[0] - '0';  /* 把已消费的首位算回来 */
    }

    /* 读取整数位（十进制/十六进制/八进制） */
    while (1) {
        int c = input_peek(lx);
        if (base == 8) {
            /* 八进制：只接受 0-7 */
            if (c >= '0' && c <= '7') {
                uval = uval * 8 + (c - '0');
                advance(lx);
            } else {
                break;
            }
        } else if (is_digit(c)) {
            uval = uval * base + (c - '0');
            advance(lx);
        } else if (base == 16 && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            uval = uval * 16 + ((c >= 'a') ? (c - 'a' + 10) : (c - 'A' + 10));
            advance(lx);
        } else {
            break;
        }
    }

    /* 小数部分（仅十进制，且下一位必须是数字：3.f → 3 . f，不是 float） */
    if (base == 10 && input_peek(lx) == '.') {
        int next = (lx->pos + 1 < lx->end) ? (unsigned char)*(lx->pos + 1) : -1;
        if (is_digit(next)) {
            maybe_float = 1;
            advance(lx);  /* 跳过 . */
            while (is_digit(input_peek(lx)))
                advance(lx);
        }
    }

    /* 指数部分（仅十进制，下一个必须是数字或符号） */
    if (base == 10 && (input_peek(lx) == 'e' || input_peek(lx) == 'E')) {
        int next = (lx->pos + 1 < lx->end) ? (unsigned char)*(lx->pos + 1) : -1;
        if (is_digit(next) || next == '+' || next == '-') {
            maybe_float = 1;
            advance(lx);  /* 跳过 e/E */
            if (input_peek(lx) == '+' || input_peek(lx) == '-')
                advance(lx);
            while (is_digit(input_peek(lx)))
                advance(lx);
        }
    }

    /* 计算浮点值 */
    if (maybe_float) {
        t.is_float = 8;  /* 默认 double (64-bit) */
    }

    /* 跳过后缀：u/l/ll（整数）和 f/F（浮点） */
    while (1) {
        int c = input_peek(lx);
        if (c == 'u' || c == 'U') {
            t.is_unsigned = 1;
            advance(lx);
        } else if (c == 'l' || c == 'L') {
            advance(lx);
        } else if (c == 'f' || c == 'F') {
            if (t.is_float) t.is_float = 4;  /* f 后缀 → float (32-bit) */
            advance(lx);
        } else
            break;
    }

    t.ival = (long)uval;  /* 位模式在 64 位二补码下保存不变 */

    t.len = lx->pos - t.start;
    return t;
}

/* ─── 读标识符/关键字 ─── */

static Token read_ident(Lexer *lx) {
    Token t;
    t.start = lx->pos - 1;
    t.kind = TOK_IDENT;
    t.ival = 0;
    t.is_unsigned = 0;
    t.sval = 0;

    while (is_alnum(input_peek(lx)))
        advance(lx);

    t.len = lx->pos - t.start;
    t.kind = keyword_lookup(t.start, t.len);
    return t;
}

/* ─── 读字符串 ─── */

static Token read_string(Lexer *lx) {
    Token t;
    t.start = lx->pos - 1;
    t.kind = TOK_STRING;
    t.ival = 0;
    t.is_unsigned = 0;
    t.sval = 0;

    while (1) {
        int c = input_peek(lx);
        if (c == '"') {
            advance(lx);
            break;
        }
        if (c == '\n' || c == (-1)) {
            t.kind = TOK_ERROR;
            lx->lex_err = "unterminated string literal";
            break;
        }
        if (c == '\\') {
            advance(lx);
            if (input_peek(lx) != (-1)) advance(lx);
        } else {
            advance(lx);
        }
    }
    t.len = lx->pos - t.start;
    return t;
}

/* ─── 主力函数：取下一个 Token ─── */

Token lexer_next(Lexer *lx) {
    /* 跳过空白和注释 */
    while (1) {
        int c = input_peek(lx);
        if (c == (-1)) {
            Token t;
            t.kind = TOK_EOF;
            t.start = lx->pos;
            t.len = 0;
            t.ival = 0;
            t.is_unsigned = 0;
            t.sval = 0;
            lx->cur = t;
            return t;
        }
        if (c == '/' && lx->pos + 1 < lx->end) {
            int n = *(lx->pos + 1);
            if (n == '/' || n == '*') {
                advance(lx);
                skip_comment(lx, n);
                continue;
            }
        }
        /* 跳过预处理器行（# 行号 "file"） */
        if (c == '#' && lx->col <= 2) {
            while (input_peek(lx) != '\n' && input_peek(lx) != (-1))
                advance(lx);
            continue;
        }
        if (is_ws(c)) {
            advance(lx);
            continue;
        }
        break;
    }

    lx->lex_err = 0;
    lx->start = lx->pos;
    int c = input_peek(lx);
    advance(lx);

    Token t;
    t.start = lx->start;
    t.ival = 0;
    t.is_unsigned = 0;
    t.is_float = 0;
    t.sval = 0;

    switch (c) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        t = read_number(lx);
        break;

    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
    case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
    case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
    case 'v': case 'w': case 'x': case 'y': case 'z':
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
    case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
    case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
    case 'V': case 'W': case 'X': case 'Y': case 'Z':
    case '_':
        t = read_ident(lx);
        break;

    case '"':
        t = read_string(lx);
        break;

    case '\'': {
        t.kind = TOK_NUMBER;
        if (input_peek(lx) == '\\') {
            advance(lx);
            if (input_peek(lx) == 'x') {
                /* \xAB 十六进制转义 */
                advance(lx);
                int hex_val = 0;
                int hex_digits = 0;
                while (hex_digits < 2) {
                    int c = input_peek(lx);
                    if (c >= '0' && c <= '9') { hex_val = hex_val * 16 + (c - '0'); advance(lx); hex_digits++; }
                    else if (c >= 'a' && c <= 'f') { hex_val = hex_val * 16 + (c - 'a' + 10); advance(lx); hex_digits++; }
                    else if (c >= 'A' && c <= 'F') { hex_val = hex_val * 16 + (c - 'A' + 10); advance(lx); hex_digits++; }
                    else break;
                }
                t.ival = hex_val;
            } else if (input_peek(lx) >= '0' && input_peek(lx) <= '7') {
                /* \NNN 八进制转义：读取最多 3 位 */
                int oct_val = 0;
                int oct_digits = 0;
                while (oct_digits < 3) {
                    int c = input_peek(lx);
                    if (c >= '0' && c <= '7') { oct_val = oct_val * 8 + (c - '0'); advance(lx); oct_digits++; }
                    else break;
                }
                t.ival = oct_val;
            } else {
                switch (input_peek(lx)) {
                case 'n': t.ival = '\n'; break;
                case 't': t.ival = '\t'; break;
                case 'r': t.ival = '\r'; break;
                case '0': t.ival = '\0'; break;
                case '\\': t.ival = '\\'; break;
                case '\'': t.ival = '\''; break;
                case '"': t.ival = '"'; break;
                default:  t.ival = input_peek(lx); break;
                }
                advance(lx);
            }
        } else {
            t.ival = input_peek(lx);
            advance(lx);
        }
        if (input_peek(lx) == '\'')
            advance(lx);
        else
            { t.kind = TOK_ERROR; lx->lex_err = "unterminated character constant"; }
        break;
    }

    case ';': t.kind = TOK_SEMI; break;
    case '{': t.kind = TOK_LBRACE; break;
    case '}': t.kind = TOK_RBRACE; break;
    case '(': t.kind = TOK_LPAREN; break;
    case ')': t.kind = TOK_RPAREN; break;
    case '[': t.kind = TOK_LBRACKET; break;
    case ']': t.kind = TOK_RBRACKET; break;
    case ',': t.kind = TOK_COMMA; break;
    case '?': t.kind = TOK_QUESTION; break;
    case '~': t.kind = TOK_TILDE; break;
    case '^':
        if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_CARET_EQ; }
        else t.kind = TOK_CARET;
        break;

    case '.':
        if (is_digit(input_peek(lx))) {
            t = read_number(lx);
        } else if (input_peek(lx) == '.') { advance(lx);
            if (input_peek(lx) == '.') { advance(lx); t.kind = TOK_ELLIPSIS; }
            else { t.kind = TOK_ERROR; lx->lex_err = "invalid token '..'"; }
        } else t.kind = TOK_DOT;
        break;

    case '+':
        if (input_peek(lx) == '+') { advance(lx); t.kind = TOK_PLUS_PLUS; }
        else if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_PLUS_EQ; }
        else t.kind = TOK_PLUS;
        break;

    case '-':
        if (input_peek(lx) == '-') { advance(lx); t.kind = TOK_MINUS_MINUS; }
        else if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_MINUS_EQ; }
        else if (input_peek(lx) == '>') { advance(lx); t.kind = TOK_ARROW; }
        else t.kind = TOK_MINUS;
        break;

    case '*':
        if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_STAR_EQ; }
        else t.kind = TOK_STAR;
        break;

    case '/':
        if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_SLASH_EQ; }
        else t.kind = TOK_SLASH;
        break;

    case '%':
        if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_PERCENT_EQ; }
        else t.kind = TOK_PERCENT;
        break;

    case '&':
        if (input_peek(lx) == '&') { advance(lx); t.kind = TOK_AND_AND; }
        else if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_AND_EQ; }
        else t.kind = TOK_AMPERSAND;
        break;

    case '|':
        if (input_peek(lx) == '|') { advance(lx); t.kind = TOK_OR_OR; }
        else if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_OR_EQ; }
        else t.kind = TOK_PIPE;
        break;

    case '<':
        if (input_peek(lx) == '<') { advance(lx);
            if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_LESS_LESS_EQ; }
            else t.kind = TOK_LESS_LESS;
        } else if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_LESS_EQ; }
        else t.kind = TOK_LESS;
        break;

    case '>':
        if (input_peek(lx) == '>') { advance(lx);
            if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_GREATER_GREATER_EQ; }
            else t.kind = TOK_GREATER_GREATER;
        } else if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_GREATER_EQ; }
        else t.kind = TOK_GREATER;
        break;

    case '=':
        if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_EQ_EQ; }
        else t.kind = TOK_EQ;
        break;

    case '!':
        if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_NOT_EQ; }
        else t.kind = TOK_EXCLAM;
        break;

    case ':': t.kind = TOK_COLON; break;

    default:
        t.kind = TOK_ERROR;
        lx->lex_err = "unrecognized character";
        t.ival = c;
        break;
    }

    t.len = lx->pos - t.start;
    lx->cur = t;
    return t;
}

Token lexer_peek(Lexer *lx) {
    return lx->cur;
}
