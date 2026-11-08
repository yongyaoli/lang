#include "ast.h"

#include <stdio.h>
#include <stdlib.h>

/* ---------- 动态数组 ---------- */

void nodelist_push(NodeList *l, Node *n) {
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->items = realloc(l->items, sizeof(Node *) * (size_t)l->cap);
    }
    l->items[l->count++] = n;
}

void namelist_push(NameList *l, char *s) {
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->items = realloc(l->items, sizeof(char *) * (size_t)l->cap);
    }
    l->items[l->count++] = s;
}

static void nodelist_free(NodeList *l) {
    for (int i = 0; i < l->count; i++) ast_free(l->items[i]);
    free(l->items);
}

static void namelist_free(NameList *l) {
    for (int i = 0; i < l->count; i++) free(l->items[i]);
    free(l->items);
}

/* ---------- 释放 ---------- */

void ast_free(Node *n) {
    if (!n) return;
    switch (n->kind) {
    case N_STRING: case N_IDENT: free(n->as.str); break;
    case N_UNARY: ast_free(n->as.binary.left); break;
    case N_BINARY:
        ast_free(n->as.binary.left);
        ast_free(n->as.binary.right);
        break;
    case N_TERNARY:
        ast_free(n->as.ternary.cond);
        ast_free(n->as.ternary.then_expr);
        ast_free(n->as.ternary.else_expr);
        break;
    case N_ASSIGN:
        ast_free(n->as.assign.target);
        ast_free(n->as.assign.value);
        break;
    case N_CALL:
        ast_free(n->as.call.callee);
        nodelist_free(&n->as.call.args);
        break;
    case N_INDEX:
        ast_free(n->as.index.obj);
        ast_free(n->as.index.index);
        break;
    case N_MEMBER:
        ast_free(n->as.member.obj);
        free(n->as.member.name);
        break;
    case N_FN:
        free(n->as.fn.name);
        namelist_free(&n->as.fn.params);
        ast_free(n->as.fn.body);
        break;
    case N_ARRAY: case N_BLOCK: case N_PROGRAM:
        nodelist_free(&n->as.list);
        break;
    case N_MAP:
        namelist_free(&n->as.map.keys);
        nodelist_free(&n->as.map.values);
        break;
    case N_LET:
        free(n->as.let.name);
        ast_free(n->as.let.init);
        break;
    case N_EXPR_STMT: case N_RETURN: case N_THROW: ast_free(n->as.expr); break;
    case N_IF:
        ast_free(n->as.if_stmt.cond);
        ast_free(n->as.if_stmt.then_b);
        ast_free(n->as.if_stmt.else_b);
        break;
    case N_WHILE:
        ast_free(n->as.while_stmt.cond);
        ast_free(n->as.while_stmt.body);
        break;
    case N_FOR:
        ast_free(n->as.for_stmt.init);
        ast_free(n->as.for_stmt.cond);
        ast_free(n->as.for_stmt.step);
        ast_free(n->as.for_stmt.body);
        break;
    case N_IMPORT:
        free(n->as.import_stmt.path);
        free(n->as.import_stmt.alias);
        break;
    case N_TRY:
        ast_free(n->as.try_stmt.try_b);
        free(n->as.try_stmt.err_var);
        ast_free(n->as.try_stmt.catch_b);
        break;
    default: break;
    }
    free(n);
}

/* ---------- 打印 ---------- */

static const char *op_str(TokenType t) {
    switch (t) {
    case T_PLUS: return "+";   case T_MINUS: return "-";
    case T_STAR: return "*";   case T_SLASH: return "/";
    case T_PERCENT: return "%";
    case T_AMP: return "&";    case T_PIPE: return "|";
    case T_CARET: return "^";  case T_TILDE: return "~";
    case T_SHL: return "<<";   case T_SHR: return ">>";
    case T_EQ_EQ: return "=="; case T_BANG_EQ: return "!=";
    case T_LT: return "<";     case T_LE: return "<=";
    case T_GT: return ">";     case T_GE: return ">=";
    case T_AND: return "and";  case T_OR: return "or";
    case T_NOT: return "not";
    default: return "?";
    }
}

static void indent(int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
}

static void print_node(const Node *n, int depth);

static void print_list(const NodeList *l, int depth) {
    for (int i = 0; i < l->count; i++) {
        putchar(' ');
        print_node(l->items[i], depth);
    }
}

/* 语句序列:每个子节点独占一行 */
static void print_block(const NodeList *l, int depth) {
    for (int i = 0; i < l->count; i++) {
        putchar('\n');
        indent(depth + 1);
        print_node(l->items[i], depth + 1);
    }
}

static void print_opt(const Node *n, int depth) {
    if (n) print_node(n, depth); else printf("_");
}

static void print_node(const Node *n, int depth) {
    switch (n->kind) {
    case N_NUMBER: printf("%g", n->as.number); break;
    case N_INT:    printf("%ld", n->as.ival); break;
    case N_STRING: printf("\"%s\"", n->as.str); break;
    case N_BOOL:   printf(n->as.boolean ? "true" : "false"); break;
    case N_NIL:    printf("nil"); break;
    case N_IDENT:  printf("%s", n->as.str); break;
    case N_UNARY:
        printf("(%s ", op_str(n->as.binary.op));
        print_node(n->as.binary.left, depth);
        printf(")");
        break;
    case N_BINARY:
        printf("(%s ", op_str(n->as.binary.op));
        print_node(n->as.binary.left, depth);
        putchar(' ');
        print_node(n->as.binary.right, depth);
        printf(")");
        break;
    case N_TERNARY:
        printf("(? ");
        print_node(n->as.ternary.cond, depth);
        putchar(' ');
        print_node(n->as.ternary.then_expr, depth);
        putchar(' ');
        print_node(n->as.ternary.else_expr, depth);
        printf(")");
        break;
    case N_ASSIGN:
        printf("(= ");
        print_node(n->as.assign.target, depth);
        putchar(' ');
        print_node(n->as.assign.value, depth);
        printf(")");
        break;
    case N_CALL:
        printf("(call ");
        print_node(n->as.call.callee, depth);
        print_list(&n->as.call.args, depth);
        printf(")");
        break;
    case N_INDEX:
        printf("(index ");
        print_node(n->as.index.obj, depth);
        putchar(' ');
        print_node(n->as.index.index, depth);
        printf(")");
        break;
    case N_MEMBER:
        printf("(. ");
        print_node(n->as.member.obj, depth);
        printf(" %s)", n->as.member.name);
        break;
    case N_FN:
        printf("(fn %s (", n->as.fn.name ? n->as.fn.name : "<anon>");
        for (int i = 0; i < n->as.fn.params.count; i++)
            printf(i ? " %s" : "%s", n->as.fn.params.items[i]);
        printf(") ");
        print_node(n->as.fn.body, depth);
        printf(")");
        break;
    case N_ARRAY:
        printf("(array");
        print_list(&n->as.list, depth);
        printf(")");
        break;
    case N_MAP:
        printf("(map");
        for (int i = 0; i < n->as.map.keys.count; i++) {
            printf(" %s:", n->as.map.keys.items[i]);
            print_node(n->as.map.values.items[i], depth);
        }
        printf(")");
        break;
    case N_LET:
        printf("(%s %s ", n->as.let.is_const ? "const" : "let", n->as.let.name);
        print_opt(n->as.let.init, depth);
        printf(")");
        break;
    case N_EXPR_STMT:
        printf("(expr ");
        print_node(n->as.expr, depth);
        printf(")");
        break;
    case N_IF:
        printf("(if ");
        print_node(n->as.if_stmt.cond, depth);
        putchar(' ');
        print_node(n->as.if_stmt.then_b, depth);
        if (n->as.if_stmt.else_b) {
            putchar(' ');
            print_node(n->as.if_stmt.else_b, depth);
        }
        printf(")");
        break;
    case N_WHILE:
        printf("(while ");
        print_node(n->as.while_stmt.cond, depth);
        putchar(' ');
        print_node(n->as.while_stmt.body, depth);
        printf(")");
        break;
    case N_FOR:
        printf("(for ");
        print_opt(n->as.for_stmt.init, depth);
        putchar(' ');
        print_opt(n->as.for_stmt.cond, depth);
        putchar(' ');
        print_opt(n->as.for_stmt.step, depth);
        putchar(' ');
        print_node(n->as.for_stmt.body, depth);
        printf(")");
        break;
    case N_IMPORT:
        printf("(import \"%s\"", n->as.import_stmt.path);
        if (n->as.import_stmt.alias) printf(" as %s", n->as.import_stmt.alias);
        printf(")");
        break;
    case N_RETURN:
        printf("(return ");
        print_opt(n->as.expr, depth);
        printf(")");
        break;
    case N_THROW:
        printf("(throw ");
        print_opt(n->as.expr, depth);
        printf(")");
        break;
    case N_TRY:
        printf("(try ");
        print_node(n->as.try_stmt.try_b, depth);
        printf(" catch (%s) ", n->as.try_stmt.err_var ? n->as.try_stmt.err_var : "_");
        print_node(n->as.try_stmt.catch_b, depth);
        printf(")");
        break;
    case N_BREAK:    printf("(break)"); break;
    case N_CONTINUE: printf("(continue)"); break;
    case N_BLOCK:
        printf("(block");
        print_block(&n->as.list, depth);
        printf(")");
        break;
    case N_PROGRAM:
        printf("(program");
        print_block(&n->as.list, depth);
        printf(")");
        break;
    }
}

void ast_print(const Node *n) {
    print_node(n, 0);
    putchar('\n');
}
