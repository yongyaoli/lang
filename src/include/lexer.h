#ifndef L_LEXER_H
#define L_LEXER_H

#include <stddef.h>

/* Token 类型,与 L_EBNF.md 保持一致 */
typedef enum {
    /* 单字符符号 */
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET,
    T_COMMA, T_SEMICOLON, T_COLON, T_QUESTION, T_DOT,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    /* 位运算 */
    T_AMP, T_PIPE, T_CARET, T_TILDE, T_SHL, T_SHR,
    /* 一或两字符符号 */
    T_BANG, T_BANG_EQ, T_ASSIGN, T_EQ_EQ,
    T_LT, T_LE, T_GT, T_GE,
    /* 字面量与标识符 */
    T_IDENT, T_NUMBER, T_STRING,
    /* 关键字 */
    T_LET, T_CONST, T_FN, T_RETURN, T_IF, T_ELSE, T_WHILE, T_FOR,
    T_BREAK, T_CONTINUE, T_TRUE, T_FALSE, T_NIL,
    T_AND, T_OR, T_NOT, T_IMPORT,
    T_TRY, T_CATCH, T_THROW,
    /* 特殊 */
    T_ERROR, T_EOF
} TokenType;

typedef struct {
    TokenType type;
    const char *start;  /* 指向源码中的词素起点(不拥有内存) */
    size_t length;      /* 词素长度;字符串含两侧引号 */
    int line;           /* 行号,从 1 开始 */
    int col;            /* 列号,从 1 开始 */
    double number;      /* 仅 T_NUMBER 有效(is_int 为 0 时) */
    int is_int;         /* T_NUMBER: 1 表示整数字面量(十进制/0x/0b), 值在 ival */
    long ival;          /* 整数字面量的值(范围 int32) */
    const char *error;  /* 仅 T_ERROR 有效:错误信息(静态字符串) */
} Token;

typedef struct {
    const char *src;    /* 源码起点 */
    const char *cur;    /* 当前扫描位置 */
    int line;
    int col;
} Lexer;

void lexer_init(Lexer *lx, const char *src);
Token lexer_next(Lexer *lx);

/* 返回 Token 类型的名称,调试用 */
const char *token_type_name(TokenType t);

/* 解码字符串 Token(去引号、处理转义),返回 malloc 的 C 字符串,调用者负责 free */
char *token_string_value(const Token *t);

#endif
