/* 递归下降语法分析器,与 L_EBNF.md 一一对应 */
#include "parser.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Lexer lx;
    Token cur;          /* 当前待处理的 Token(向前看 1 个) */
    Token prev;         /* 刚刚消费的 Token */
    int had_error;
    jmp_buf jb;         /* 出错时跳转到最近的恢复点 */
} Parser;

static const char *cur_parse_file = NULL;

/* 打印出错源码行及列号指示符 ^ */
static void print_error_snippet(const Token *t) {
    if (!t || !t->start || t->col < 1 || t->line <= 0) return;
    const char *line_start = t->start - (t->col - 1);
    const char *end = line_start;
    while (*end && *end != '\n' && *end != '\r') end++;
    int len = (int)(end - line_start);
    if (len <= 0) return;
    fprintf(stderr, "  %4d | %.*s\n       | ", t->line, len, line_start);
    for (int i = 1; i < t->col && i <= len; i++) {
        fputc(line_start[i - 1] == '\t' ? '\t' : ' ', stderr);
    }
    fprintf(stderr, "^\n");
}

/* ================= 基础工具 ================= */

static void error_at(Parser *p, const Token *t, const char *msg) {
    p->had_error = 1;
    if (cur_parse_file && *cur_parse_file)
        fprintf(stderr, "[%s:%d:%d] ", cur_parse_file, t->line, t->col);
    else
        fprintf(stderr, "[%d:%d] ", t->line, t->col);

    if (t->type == T_EOF)
        fprintf(stderr, "语法错误(文件末尾): %s\n", msg);
    else if (t->type == T_ERROR)
        fprintf(stderr, "词法错误: %s\n", msg);
    else
        fprintf(stderr, "语法错误(在 '%.*s' 附近): %s\n",
                (int)t->length, t->start, msg);
    print_error_snippet(t);
}

/* 报错并跳转到恢复点 */
static void fail(Parser *p, const char *msg) {
    error_at(p, &p->cur, msg);
    longjmp(p->jb, 1);
}

static void advance(Parser *p) {
    p->prev = p->cur;
    for (;;) {
        p->cur = lexer_next(&p->lx);
        if (p->cur.type != T_ERROR) break;
        error_at(p, &p->cur, p->cur.error);   /* 词法错误:报告后跳过 */
    }
}

static int check(const Parser *p, TokenType t) { return p->cur.type == t; }

static int match(Parser *p, TokenType t) {
    if (!check(p, t)) return 0;
    advance(p);
    return 1;
}

static void consume(Parser *p, TokenType t, const char *msg) {
    if (!match(p, t)) fail(p, msg);
}

static char *dup_n(const char *s, size_t n) {
    char *r = malloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static Node *new_node(NodeKind kind, const Token *t) {
    Node *n = calloc(1, sizeof(Node));
    n->kind = kind;
    n->line = t->line;
    n->col = t->col;
    n->file = cur_parse_file;
    n->line_start = (t->start && t->col >= 1) ? (t->start - (t->col - 1)) : NULL;
    return n;
}

/* 出错后同步:跳到下一条语句的开头,避免连锁报错 */
static void synchronize(Parser *p, int in_block) {
    int first = 1;  /* 首轮不检查 prev:上一条语句的 ';' 不代表本次错误已被越过,否则可能死循环 */
    for (;;) {
        if (check(p, T_EOF)) return;
        if (!first && p->prev.type == T_SEMICOLON) return;
        first = 0;
        if (check(p, T_RBRACE)) {
            if (in_block) return;       /* 留给 block 自己消费 */
            advance(p);                 /* 顶层的多余 '}':跳过 */
            continue;
        }
        switch (p->cur.type) {
        case T_LET: case T_CONST: case T_FN: case T_IF: case T_WHILE: case T_FOR:
        case T_RETURN: case T_BREAK: case T_CONTINUE: case T_IMPORT:
        case T_TRY: case T_THROW:
            return;
        default: break;
        }
        advance(p);
    }
}

static Node *expression(Parser *p);
static Node *declaration(Parser *p);
static Node *block(Parser *p);

/* ================= 表达式 ================= */

/* parameters = IDENT { "," IDENT } ;  已消费 "(" 之后调用,负责读到 ")" */
static void parse_params(Parser *p, NameList *params) {
    if (!check(p, T_RPAREN)) {
        do {
            consume(p, T_IDENT, "期望参数名");
            namelist_push(params, dup_n(p->prev.start, p->prev.length));
        } while (match(p, T_COMMA));
    }
    consume(p, T_RPAREN, "期望 ')'");
}

/* 函数的 "(" params ")" block 部分;name 可为 NULL(匿名函数) */
static Node *function_rest(Parser *p, const Token *start, char *name) {
    Node *n = new_node(N_FN, start);
    n->as.fn.name = name;
    consume(p, T_LPAREN, "期望 '('");
    parse_params(p, &n->as.fn.params);
    n->as.fn.body = block(p);
    return n;
}

static Node *array_literal(Parser *p) {
    Node *n = new_node(N_ARRAY, &p->prev);          /* prev 是 '[' */
    while (!check(p, T_RBRACKET)) {
        nodelist_push(&n->as.list, expression(p));
        if (!match(p, T_COMMA)) break;              /* 允许尾逗号 */
    }
    consume(p, T_RBRACKET, "期望 ']'");
    return n;
}

static Node *map_literal(Parser *p) {
    Node *n = new_node(N_MAP, &p->prev);            /* prev 是 '{' */
    while (!check(p, T_RBRACE)) {
        if (match(p, T_IDENT)) {
            namelist_push(&n->as.map.keys, dup_n(p->prev.start, p->prev.length));
        } else if (match(p, T_STRING)) {
            namelist_push(&n->as.map.keys, token_string_value(&p->prev));
        } else {
            fail(p, "期望 map 的键(标识符或字符串)");
        }
        consume(p, T_COLON, "期望 ':'");
        nodelist_push(&n->as.map.values, expression(p));
        if (!match(p, T_COMMA)) break;
    }
    consume(p, T_RBRACE, "期望 '}'");
    return n;
}

static Node *primary(Parser *p) {
    Node *n;
    if (match(p, T_NUMBER)) {
        if (p->prev.is_int) {
            n = new_node(N_INT, &p->prev);
            n->as.ival = p->prev.ival;
            return n;
        }
        n = new_node(N_NUMBER, &p->prev);
        n->as.number = p->prev.number;
        return n;
    }
    if (match(p, T_STRING)) {
        n = new_node(N_STRING, &p->prev);
        n->as.str = token_string_value(&p->prev);
        return n;
    }
    if (match(p, T_TRUE) || match(p, T_FALSE)) {
        n = new_node(N_BOOL, &p->prev);
        n->as.boolean = (p->prev.type == T_TRUE);
        return n;
    }
    if (match(p, T_NIL)) return new_node(N_NIL, &p->prev);
    if (match(p, T_IDENT)) {
        n = new_node(N_IDENT, &p->prev);
        n->as.str = dup_n(p->prev.start, p->prev.length);
        return n;
    }
    if (match(p, T_FN)) {
        Token start = p->prev;
        return function_rest(p, &start, NULL);
    }
    if (match(p, T_LBRACKET)) return array_literal(p);
    if (match(p, T_LBRACE)) return map_literal(p);
    if (match(p, T_LPAREN)) {
        n = expression(p);
        consume(p, T_RPAREN, "期望 ')'");
        return n;
    }
    fail(p, "期望表达式");
    return NULL;
}

/* call_or_index = primary { "(" args ")" | "[" expr "]" | "." IDENT } */
static Node *postfix(Parser *p) {
    Node *n = primary(p);
    for (;;) {
        if (match(p, T_LPAREN)) {
            Node *c = new_node(N_CALL, &p->prev);
            c->as.call.callee = n;
            n = c;      /* 先挂上去,中途出错时不丢失(便于释放) */
            if (!check(p, T_RPAREN)) {
                do {
                    nodelist_push(&c->as.call.args, expression(p));
                } while (match(p, T_COMMA));
            }
            consume(p, T_RPAREN, "期望 ')'");
        } else if (match(p, T_LBRACKET)) {
            Node *i = new_node(N_INDEX, &p->prev);
            i->as.index.obj = n;
            n = i;
            i->as.index.index = expression(p);
            consume(p, T_RBRACKET, "期望 ']'");
        } else if (match(p, T_DOT)) {
            Node *m = new_node(N_MEMBER, &p->prev);
            m->as.member.obj = n;
            n = m;
            if (p->cur.type == T_IDENT || (p->cur.type >= T_LET && p->cur.type <= T_THROW)) {
                advance(p);
            } else {
                fail(p, "'.' 后期望属性名");
            }
            m->as.member.name = dup_n(p->prev.start, p->prev.length);
        } else {
            return n;
        }
    }
}

/* unary = ( "not" | "!" | "-" | "~" ) unary | postfix ;  "!" 与 "not" 统一为 T_NOT */
static Node *unary(Parser *p) {
    if (match(p, T_NOT) || match(p, T_BANG) || match(p, T_MINUS) || match(p, T_TILDE)) {
        Token op = p->prev;
        Node *n = new_node(N_UNARY, &op);
        n->as.binary.op = (op.type == T_MINUS || op.type == T_TILDE) ? op.type : T_NOT;
        n->as.binary.left = unary(p);
        return n;
    }
    return postfix(p);
}

/* 二元运算符优先级,0 表示不是二元运算符(数值越大绑定越紧) */
static int precedence(TokenType t) {
    switch (t) {
    case T_OR: return 1;
    case T_AND: return 2;
    case T_EQ_EQ: case T_BANG_EQ: return 3;
    case T_LT: case T_LE: case T_GT: case T_GE: return 4;
    /* 位运算优先级高于比较(与 Python/Rust 一致, 避开 C 中 x & 1 == 0 的陷阱) */
    case T_PIPE: return 5;
    case T_CARET: return 6;
    case T_AMP: return 7;
    case T_SHL: case T_SHR: return 8;
    case T_PLUS: case T_MINUS: return 9;
    case T_STAR: case T_SLASH: case T_PERCENT: return 10;
    default: return 0;
    }
}

/* 优先级爬升:全部左结合 */
static Node *binary(Parser *p, int min_prec) {
    Node *left = unary(p);
    for (;;) {
        int prec = precedence(p->cur.type);
        if (prec == 0 || prec < min_prec) return left;
        advance(p);
        Token op = p->prev;
        Node *n = new_node(N_BINARY, &op);
        n->as.binary.op = op.type;
        n->as.binary.left = left;
        left = n;
        n->as.binary.right = binary(p, prec + 1);
    }
}

static Node *ternary(Parser *p) {
    Node *expr = binary(p, 1);
    if (match(p, T_QUESTION)) {
        Token q = p->prev;
        Node *then_e = expression(p);
        consume(p, T_COLON, "三元表达式期望 ':'");
        Node *else_e = ternary(p); /* 右结合: a ? b : c ? d : e */
        Node *n = new_node(N_TERNARY, &q);
        n->as.ternary.cond = expr;
        n->as.ternary.then_expr = then_e;
        n->as.ternary.else_expr = else_e;
        return n;
    }
    return expr;
}

/* assignment = ternary [ "=" assignment ] ;  右结合 */
static Node *assignment(Parser *p) {
    Node *left = ternary(p);
    if (match(p, T_ASSIGN)) {
        Token eq = p->prev;
        if (left->kind != N_IDENT && left->kind != N_INDEX && left->kind != N_MEMBER) {
            ast_free(left);
            error_at(p, &eq, "无效的赋值目标");
            longjmp(p->jb, 1);
        }
        Node *n = new_node(N_ASSIGN, &eq);
        n->as.assign.target = left;
        n->as.assign.value = assignment(p);
        return n;
    }
    return left;
}

static Node *expression(Parser *p) { return assignment(p); }

/* ================= 语句 ================= */

static Node *var_decl(Parser *p, int is_const) {
    Token start = p->prev;                          /* 'let' 或 'const' */
    consume(p, T_IDENT, is_const ? "'const' 后期望常量名" : "'let' 后期望变量名");
    Node *n = new_node(N_LET, &start);
    n->as.let.name = dup_n(p->prev.start, p->prev.length);
    n->as.let.is_const = is_const;
    if (is_const) {
        consume(p, T_ASSIGN, "const 常量声明必须同时赋初值 '='");
        n->as.let.init = expression(p);
    } else if (match(p, T_ASSIGN)) {
        n->as.let.init = expression(p);
    }
    consume(p, T_SEMICOLON, "变量/常量声明后期望 ';'");
    return n;
}

static Node *expr_stmt(Parser *p) {
    Node *n = new_node(N_EXPR_STMT, &p->cur);
    n->as.expr = expression(p);
    consume(p, T_SEMICOLON, "表达式后期望 ';'");
    return n;
}

static Node *if_stmt(Parser *p) {
    Token start = p->prev;                          /* 'if' */
    Node *n = new_node(N_IF, &start);
    n->as.if_stmt.cond = expression(p);
    n->as.if_stmt.then_b = block(p);
    if (match(p, T_ELSE)) {
        if (match(p, T_IF)) n->as.if_stmt.else_b = if_stmt(p);
        else                n->as.if_stmt.else_b = block(p);
    }
    return n;
}

static Node *while_stmt(Parser *p) {
    Node *n = new_node(N_WHILE, &p->prev);
    n->as.while_stmt.cond = expression(p);
    n->as.while_stmt.body = block(p);
    return n;
}

static Node *for_stmt(Parser *p) {
    Node *n = new_node(N_FOR, &p->prev);
    if (match(p, T_SEMICOLON)) {
        n->as.for_stmt.init = NULL;
    } else if (match(p, T_LET)) {
        n->as.for_stmt.init = var_decl(p, 0);
    } else {
        n->as.for_stmt.init = expr_stmt(p);
    }
    if (!check(p, T_SEMICOLON)) n->as.for_stmt.cond = expression(p);
    consume(p, T_SEMICOLON, "for 条件后期望 ';'");
    if (!check(p, T_LBRACE)) n->as.for_stmt.step = expression(p);
    n->as.for_stmt.body = block(p);
    return n;
}

static Node *return_stmt(Parser *p) {
    Node *n = new_node(N_RETURN, &p->prev);
    if (!check(p, T_SEMICOLON)) n->as.expr = expression(p);
    consume(p, T_SEMICOLON, "return 后期望 ';'");
    return n;
}

static Node *throw_stmt(Parser *p) {
    Node *n = new_node(N_THROW, &p->prev);
    n->as.expr = expression(p);
    consume(p, T_SEMICOLON, "throw 后期望 ';'");
    return n;
}

/* try { ... } catch [(e)] { ... } */
static Node *try_stmt(Parser *p) {
    Node *n = new_node(N_TRY, &p->prev);
    n->as.try_stmt.try_b = block(p);
    consume(p, T_CATCH, "try 块后期望 'catch'");
    if (match(p, T_LPAREN)) {
        consume(p, T_IDENT, "catch '(' 后期望错误变量名");
        n->as.try_stmt.err_var = dup_n(p->prev.start, p->prev.length);
        consume(p, T_RPAREN, "catch 变量名后期望 ')'");
    }
    n->as.try_stmt.catch_b = block(p);
    return n;
}

/* block = "{" { declaration } "}" ;  块内自带错误恢复点 */
static Node *block(Parser *p) {
    consume(p, T_LBRACE, "期望 '{'");
    Node *blk = new_node(N_BLOCK, &p->prev);

    jmp_buf saved;
    memcpy(&saved, &p->jb, sizeof(jmp_buf));
    while (!check(p, T_RBRACE) && !check(p, T_EOF)) {
        if (setjmp(p->jb) == 0) nodelist_push(&blk->as.list, declaration(p));
        else                    synchronize(p, 1);
    }
    memcpy(&p->jb, &saved, sizeof(jmp_buf));        /* 先恢复外层恢复点,再检查 '}' */
    consume(p, T_RBRACE, "期望 '}'");
    return blk;
}

static Node *statement(Parser *p) {
    if (match(p, T_IF))       return if_stmt(p);
    if (match(p, T_WHILE))    return while_stmt(p);
    if (match(p, T_FOR))      return for_stmt(p);
    if (match(p, T_TRY))      return try_stmt(p);
    if (match(p, T_THROW))    return throw_stmt(p);
    if (match(p, T_RETURN))   return return_stmt(p);
    if (match(p, T_BREAK)) {
        Node *n = new_node(N_BREAK, &p->prev);
        consume(p, T_SEMICOLON, "break 后期望 ';'");
        return n;
    }
    if (match(p, T_CONTINUE)) {
        Node *n = new_node(N_CONTINUE, &p->prev);
        consume(p, T_SEMICOLON, "continue 后期望 ';'");
        return n;
    }
    if (check(p, T_LBRACE))   return block(p);
    return expr_stmt(p);
}

static Node *import_stmt(Parser *p) {
    Token start = p->prev; /* 'import' */
    consume(p, T_STRING, "'import' 后期望模块路径字符串,如 \"./math.l\"");
    Node *n = new_node(N_IMPORT, &start);
    n->as.import_stmt.path = token_string_value(&p->prev);
    n->as.import_stmt.alias = NULL;

    /* 可选的 as 别名: import "./math.l" as m; (若 as 是普通标识符) */
    if (check(p, T_IDENT) && p->cur.length == 2 && memcmp(p->cur.start, "as", 2) == 0) {
        advance(p);
        consume(p, T_IDENT, "'as' 后期望别名标识符");
        n->as.import_stmt.alias = dup_n(p->prev.start, p->prev.length);
    }
    consume(p, T_SEMICOLON, "import 语句后期望 ';'");
    return n;
}

static Node *declaration(Parser *p) {
    if (match(p, T_IMPORT)) return import_stmt(p);
    if (match(p, T_LET)) return var_decl(p, 0);
    if (match(p, T_CONST)) return var_decl(p, 1);
    if (match(p, T_FN)) {
        Token start = p->prev;
        if (!check(p, T_IDENT)) fail(p, "函数声明需要名字(匿名函数请用在表达式中)");
        advance(p);
        return function_rest(p, &start, dup_n(p->prev.start, p->prev.length));
    }
    return statement(p);
}

/* ================= 入口 ================= */

Node *parse_program_file(const char *src, const char *filename, int *had_error) {
    const char *prev_file = cur_parse_file;
    cur_parse_file = filename;

    Parser p;
    memset(&p, 0, sizeof(p));
    lexer_init(&p.lx, src);
    advance(&p);                                    /* 预读第一个 Token */

    Node *prog = new_node(N_PROGRAM, &p.cur);
    while (!check(&p, T_EOF)) {
        if (setjmp(p.jb) == 0) nodelist_push(&prog->as.list, declaration(&p));
        else                   synchronize(&p, 0);
    }
    *had_error = p.had_error;
    cur_parse_file = prev_file;
    return prog;
}

Node *parse_program(const char *src, int *had_error) {
    return parse_program_file(src, NULL, had_error);
}
