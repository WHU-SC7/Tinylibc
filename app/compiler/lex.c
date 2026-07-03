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
 * （tcc 自编译时预处理器有展开 bug）。 */

#include "tcc.h"

/* ─── 关键字表 ─── */

typedef struct {
    const char *word;
    TokenKind kind;
} Keyword;

static Keyword keywords[] = {
    {"__asm__",                   TOK__ASM__},
    {"__attribute__",             TOK__ATTRIBUTE__},
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
    {"float",                     TOK_IDENT},
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

void lexer_init(Lexer *lx, const char *src, int len) {
    lx->start = src;
    lx->pos = src;
    lx->end = src + len;
    lx->line = 1;
    lx->col = 1;
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

/* ─── 简易字符串转 double ─── */
/* 由 read_number 调用，解析已确认格式正确的浮点字符串 */

static double simple_atof(const char *s, int len) {
    double result = 0.0;
    int i = 0;
    int exp_sign = 1;
    int exponent = 0;
    int has_exponent = 0;

    /* 整数部分 */
    while (i < len && is_digit(s[i])) {
        result = result * 10.0 + (double)(s[i] - '0');
        i++;
    }

    /* 小数部分 */
    if (i < len && s[i] == '.') {
        i++;
        double frac = 0.0;
        double divisor = 1.0;
        while (i < len && is_digit(s[i])) {
            frac = frac * 10.0 + (double)(s[i] - '0');
            divisor *= 10.0;
            i++;
        }
        result += frac / divisor;
    }

    /* 指数部分 */
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < len && s[i] == '-') {
            exp_sign = -1; i++;
        } else if (i < len && s[i] == '+') {
            i++;
        }
        while (i < len && is_digit(s[i])) {
            exponent = exponent * 10 + (s[i] - '0');
            i++;
        }
        has_exponent = 1;
    }

    if (has_exponent) {
        int exp = exponent * exp_sign;
        double pow10 = 1.0;
        if (exp > 0) {
            while (exp-- > 0) pow10 *= 10.0;
        } else {
            while (exp++ < 0) pow10 /= 10.0;
        }
        result *= pow10;
    }

    return result;
}

/* ─── 读数字 ─── */

static Token read_number(Lexer *lx) {
    Token t;
    t.start = lx->pos - 1;  /* pos 已在 lexer_next 中前移过首位 */
    t.kind = TOK_NUMBER;
    t.ival = 0;
    t.dval = 0.0;
    t.is_float = 0;
    t.sval = 0;

    int base = 10;
    int maybe_float = 0;

    if (t.start[0] == '0' && (input_peek(lx) == 'x' || input_peek(lx) == 'X')) {
        base = 16;
        advance(lx);
    } else if (t.start[0] == '0' && is_digit(input_peek(lx))) {
        /* 以 0 开头且后跟数字：八进制字面量（如 0777 = 511） */
        base = 8;
        t.ival = 0;
    } else if (t.start[0] == '.') {
        /* 以 . 开头的浮点数：.5 .25 等 */
        maybe_float = 1;
    } else if (is_digit(t.start[0])) {
        t.ival = t.start[0] - '0';  /* 把已消费的首位算回来 */
    }

    /* 读取整数位（十进制/十六进制/八进制） */
    while (1) {
        int c = input_peek(lx);
        if (base == 8) {
            /* 八进制：只接受 0-7 */
            if (c >= '0' && c <= '7') {
                t.ival = t.ival * 8 + (c - '0');
                advance(lx);
            } else {
                break;
            }
        } else if (is_digit(c)) {
            t.ival = t.ival * base + (c - '0');
            advance(lx);
        } else if (base == 16 && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            t.ival = t.ival * 16 + ((c >= 'a') ? (c - 'a' + 10) : (c - 'A' + 10));
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
        t.is_float = 1;
        t.dval = simple_atof(t.start, lx->pos - t.start);
    }

    /* 跳过后缀：u/l/ll（整数）和 f/F（浮点） */
    while (1) {
        int c = input_peek(lx);
        if (c == 'u' || c == 'U' || c == 'l' || c == 'L' ||
            c == 'f' || c == 'F')
            advance(lx);
        else
            break;
    }

    t.len = lx->pos - t.start;
    return t;
}

/* ─── 读标识符/关键字 ─── */

static Token read_ident(Lexer *lx) {
    Token t;
    t.start = lx->pos - 1;
    t.kind = TOK_IDENT;
    t.ival = 0;
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
    t.sval = 0;

    while (1) {
        int c = input_peek(lx);
        if (c == '"') {
            advance(lx);
            break;
        }
        if (c == '\n' || c == (-1)) {
            t.kind = TOK_ERROR;
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

    lx->start = lx->pos;
    int c = input_peek(lx);
    advance(lx);

    Token t;
    t.start = lx->start;
    t.ival = 0;
    t.sval = 0;
    t.is_float = 0;

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
            t.kind = TOK_ERROR;
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
    case '^': t.kind = TOK_CARET; break;

    case '.':
        if (is_digit(input_peek(lx))) {
            t = read_number(lx);
        } else if (input_peek(lx) == '.') { advance(lx);
            if (input_peek(lx) == '.') { advance(lx); t.kind = TOK_ELLIPSIS; }
            else t.kind = TOK_ERROR;
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
        break;
    }

    t.len = lx->pos - t.start;
    lx->cur = t;
    return t;
}

Token lexer_peek(Lexer *lx) {
    return lx->cur;
}
