#include "lexer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void lexer_init(Lexer *lx, const char *src) {
    lx->src = src;
    lx->cur = src;
    lx->line = 1;
    lx->col = 1;

    /* 兼容跳过 UTF-8 BOM (0xEF, 0xBB, 0xBF) */
    if (src && (unsigned char)src[0] == 0xEF &&
               (unsigned char)src[1] == 0xBB &&
               (unsigned char)src[2] == 0xBF) {
        lx->cur += 3;
    }
}

/* ---------- 字符辅助 ---------- */

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int is_digit(char c) { return c >= '0' && c <= '9'; }

static char peek(const Lexer *lx) { return *lx->cur; }
static char peek_next(const Lexer *lx) {
    return *lx->cur ? lx->cur[1] : '\0';
}

/* 前进一个字符并维护行列号 */
static char advance(Lexer *lx) {
    char c = *lx->cur++;
    if (c == '\n') { lx->line++; lx->col = 1; }
    else           { lx->col++; }
    return c;
}

static int match(Lexer *lx, char expected) {
    if (*lx->cur != expected) return 0;
    advance(lx);
    return 1;
}

/* ---------- Token 构造 ---------- */

static Token make_token(TokenType type, const char *start, int line, int col,
                        const Lexer *lx) {
    Token t;
    t.type = type;
    t.start = start;
    t.length = (size_t)(lx->cur - start);
    t.line = line;
    t.col = col;
    t.number = 0;
    t.is_int = 0;
    t.ival = 0;
    t.error = NULL;
    return t;
}

static Token error_token(const char *msg, const char *start, int line, int col,
                         const Lexer *lx) {
    Token t = make_token(T_ERROR, start, line, col, lx);
    t.error = msg;
    return t;
}

/* ---------- 跳过空白与注释 ---------- */

/* 返回 NULL 表示正常;否则返回错误信息(块注释未闭合) */
static const char *skip_trivia(Lexer *lx) {
    for (;;) {
        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lx);
        } else if (c == '/' && peek_next(lx) == '/') {
            while (peek(lx) != '\n' && peek(lx) != '\0') advance(lx);
        } else if (c == '/' && peek_next(lx) == '*') {
            advance(lx); advance(lx);
            while (!(peek(lx) == '*' && peek_next(lx) == '/')) {
                if (peek(lx) == '\0') return "块注释未闭合";
                advance(lx);
            }
            advance(lx); advance(lx);
        } else {
            return NULL;
        }
    }
}

/* ---------- 关键字识别 ---------- */

static const struct { const char *name; TokenType type; } keywords[] = {
    {"let", T_LET},     {"const", T_CONST},   {"fn", T_FN},         {"return", T_RETURN},
    {"if", T_IF},       {"else", T_ELSE},     {"while", T_WHILE},
    {"for", T_FOR},     {"break", T_BREAK},   {"continue", T_CONTINUE},
    {"true", T_TRUE},   {"false", T_FALSE},   {"nil", T_NIL},
    {"and", T_AND},     {"or", T_OR},         {"not", T_NOT},
    {"import", T_IMPORT},
    {"try", T_TRY},     {"catch", T_CATCH},   {"throw", T_THROW},
};

static TokenType ident_type(const char *s, size_t len) {
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (strlen(keywords[i].name) == len &&
            memcmp(keywords[i].name, s, len) == 0)
            return keywords[i].type;
    }
    return T_IDENT;
}

/* ---------- 主函数 ---------- */

Token lexer_next(Lexer *lx) {
    const char *err = skip_trivia(lx);
    const char *start = lx->cur;
    int line = lx->line, col = lx->col;

    if (err) return error_token(err, start, line, col, lx);
    if (peek(lx) == '\0') return make_token(T_EOF, start, line, col, lx);

    char c = advance(lx);

    /* 标识符 / 关键字 */
    if (is_alpha(c)) {
        while (is_alpha(peek(lx)) || is_digit(peek(lx))) advance(lx);
        return make_token(ident_type(start, (size_t)(lx->cur - start)),
                          start, line, col, lx);
    }

    /* 数字:整数部分 + 可选的 .小数部分(小数点后必须有数字) */
    if (is_digit(c)) {
        /* 0x / 0b 整数字面量 */
        if (c == '0' && (peek(lx) == 'x' || peek(lx) == 'X' ||
                         peek(lx) == 'b' || peek(lx) == 'B')) {
            int base = (peek(lx) == 'x' || peek(lx) == 'X') ? 16 : 2;
            advance(lx);
            const char *digits = lx->cur;
            while (is_alpha(peek(lx)) || is_digit(peek(lx))) advance(lx);
            if (lx->cur == digits) return error_token("进制字面量缺少数字", start, line, col, lx);
            char *end;
            unsigned long v = strtoul(digits, &end, base);
            if (end != lx->cur) return error_token("非法的进制数字", start, line, col, lx);
            if (v > 0xFFFFFFFFul) return error_token("整数字面量超出 32 位", start, line, col, lx);
            Token t = make_token(T_NUMBER, start, line, col, lx);
            t.is_int = 1;
            t.ival = (long)(int32_t)(uint32_t)v;    /* 0xFFFFFFFF 即 -1, 与 C 的位模式一致 */
            return t;
        }
        int is_float = 0;
        while (is_digit(peek(lx))) advance(lx);
        if (peek(lx) == '.' && is_digit(peek_next(lx))) {
            is_float = 1;
            advance(lx);
            while (is_digit(peek(lx))) advance(lx);
        }
        Token t = make_token(T_NUMBER, start, line, col, lx);
        t.number = strtod(start, NULL);
        /* 没有小数点且在 int32 范围内 → 整数, 否则按浮点 */
        if (!is_float && t.number <= 2147483647.0) {
            t.is_int = 1;
            t.ival = (long)t.number;
        }
        return t;
    }

    switch (c) {
    case '(': return make_token(T_LPAREN, start, line, col, lx);
    case ')': return make_token(T_RPAREN, start, line, col, lx);
    case '{': return make_token(T_LBRACE, start, line, col, lx);
    case '}': return make_token(T_RBRACE, start, line, col, lx);
    case '[': return make_token(T_LBRACKET, start, line, col, lx);
    case ']': return make_token(T_RBRACKET, start, line, col, lx);
    case ',': return make_token(T_COMMA, start, line, col, lx);
    case ';': return make_token(T_SEMICOLON, start, line, col, lx);
    case ':': return make_token(T_COLON, start, line, col, lx);
    case '?': return make_token(T_QUESTION, start, line, col, lx);
    case '.': return make_token(T_DOT, start, line, col, lx);
    case '+': return make_token(T_PLUS, start, line, col, lx);
    case '-': return make_token(T_MINUS, start, line, col, lx);
    case '*': return make_token(T_STAR, start, line, col, lx);
    case '/': return make_token(T_SLASH, start, line, col, lx);
    case '%': return make_token(T_PERCENT, start, line, col, lx);
    case '&': return make_token(T_AMP, start, line, col, lx);
    case '|': return make_token(T_PIPE, start, line, col, lx);
    case '^': return make_token(T_CARET, start, line, col, lx);
    case '~': return make_token(T_TILDE, start, line, col, lx);
    case '!':
        return make_token(match(lx, '=') ? T_BANG_EQ : T_BANG, start, line, col, lx);
    case '=':
        return make_token(match(lx, '=') ? T_EQ_EQ : T_ASSIGN, start, line, col, lx);
    case '<':
        if (match(lx, '<')) return make_token(T_SHL, start, line, col, lx);
        return make_token(match(lx, '=') ? T_LE : T_LT, start, line, col, lx);
    case '>':
        if (match(lx, '>')) return make_token(T_SHR, start, line, col, lx);
        return make_token(match(lx, '=') ? T_GE : T_GT, start, line, col, lx);

    case '"':
        /* 字符串:不允许直接换行,转义仅支持 \n \t \r \" \\ */
        while (peek(lx) != '"') {
            char p = peek(lx);
            if (p == '\0' || p == '\n')
                return error_token("字符串未闭合", start, line, col, lx);
            if (p == '\\') {
                advance(lx);
                char e = peek(lx);
                if (e != 'n' && e != 't' && e != 'r' && e != '"' && e != '\\')
                    return error_token("非法转义序列", start, line, col, lx);
            }
            advance(lx);
        }
        advance(lx); /* 结尾的引号 */
        return make_token(T_STRING, start, line, col, lx);
    }

    return error_token("非法字符", start, line, col, lx);
}

/* ---------- 工具函数 ---------- */

char *token_string_value(const Token *t) {
    /* 去掉两侧引号后,结果长度不会超过 length - 2 */
    char *out = malloc(t->length);
    if (!out) return NULL;
    size_t n = 0;
    for (size_t i = 1; i + 1 < t->length; i++) {
        char c = t->start[i];
        if (c == '\\') {
            switch (t->start[++i]) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            default:  c = t->start[i]; break; /* \" 和 \\ */
            }
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return out;
}

const char *token_type_name(TokenType t) {
    static const char *names[] = {
        "LPAREN", "RPAREN", "LBRACE", "RBRACE", "LBRACKET", "RBRACKET",
        "COMMA", "SEMICOLON", "COLON", "QUESTION", "DOT",
        "PLUS", "MINUS", "STAR", "SLASH", "PERCENT",
        "AMP", "PIPE", "CARET", "TILDE", "SHL", "SHR",
        "BANG", "BANG_EQ", "ASSIGN", "EQ_EQ",
        "LT", "LE", "GT", "GE",
        "IDENT", "NUMBER", "STRING",
        "LET", "CONST", "FN", "RETURN", "IF", "ELSE", "WHILE", "FOR",
        "BREAK", "CONTINUE", "TRUE", "FALSE", "NIL",
        "AND", "OR", "NOT", "IMPORT",
        "TRY", "CATCH", "THROW",
        "ERROR", "EOF",
    };
    return names[t];
}
