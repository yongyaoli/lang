#ifndef L_AST_H
#define L_AST_H

#include "lexer.h"

/* AST 节点类型 */
typedef enum {
    /* 表达式 */
    N_NUMBER, N_INT, N_STRING, N_BOOL, N_NIL, N_IDENT,
    N_UNARY, N_BINARY,          /* 二元含 and / or */
    N_TERNARY,                  /* 三元 cond ? then_expr : else_expr */
    N_ASSIGN, N_CALL, N_INDEX, N_MEMBER,
    N_FN,                       /* 函数:name 非 NULL 为声明,NULL 为匿名函数 */
    N_ARRAY, N_MAP,
    /* 语句 */
    N_LET, N_EXPR_STMT, N_IF, N_WHILE, N_FOR,
    N_RETURN, N_BREAK, N_CONTINUE, N_BLOCK,
    N_IMPORT, N_TRY, N_THROW,
    N_PROGRAM
} NodeKind;

typedef struct Node Node;

/* 动态数组:节点列表 / 名字列表 */
typedef struct { Node **items; int count, cap; } NodeList;
typedef struct { char **items; int count, cap; } NameList;

struct Node {
    NodeKind kind;
    int line, col;              /* 源码位置,运行时报错要用 */
    const char *file;           /* 所属源码文件名(不拥有内存) */
    const char *line_start;     /* 所在源码行起点(用于打印报错上下文与 ^ 指示) */
    union {
        double number;                                  /* N_NUMBER (浮点字面量) */
        long ival;                                      /* N_INT (整数字面量, int32 范围) */
        int boolean;                                    /* N_BOOL */
        char *str;                                      /* N_STRING / N_IDENT(已解码) */
        struct { TokenType op; Node *left, *right; } binary;  /* N_UNARY 只用 left */
        struct { Node *cond, *then_expr, *else_expr; } ternary; /* N_TERNARY */
        struct { Node *target, *value; } assign;
        struct { Node *callee; NodeList args; } call;
        struct { Node *obj, *index; } index;
        struct { Node *obj; char *name; } member;
        struct { char *name; NameList params; Node *body; } fn;
        NodeList list;                                  /* N_ARRAY / N_BLOCK / N_PROGRAM */
        struct { NameList keys; NodeList values; } map;
        struct { char *name; Node *init; int is_const; } let; /* init 可为 NULL, is_const=1 为常量 */
        Node *expr;                                     /* N_EXPR_STMT / N_RETURN(可为 NULL) */
        struct { Node *cond, *then_b, *else_b; } if_stmt;   /* else_b 可为 NULL */
        struct { Node *cond, *body; } while_stmt;
        struct { char *path; char *alias; } import_stmt;
        struct { Node *try_b; char *err_var; Node *catch_b; } try_stmt; /* err_var 可为 NULL */
        struct { Node *init, *cond, *step, *body; } for_stmt; /* 前三者可为 NULL */
    } as;
};

void nodelist_push(NodeList *l, Node *n);
void namelist_push(NameList *l, char *s);

/* 递归释放整棵树 */
void ast_free(Node *n);

/* 以 S 表达式形式打印 AST,调试用 */
void ast_print(const Node *n);

#endif
