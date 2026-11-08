#ifndef L_PARSER_H
#define L_PARSER_H

#include "ast.h"

/* 解析整个源码,返回 N_PROGRAM 节点(调用者用 ast_free 释放)。
 * 有语法错误时,错误信息打印到 stderr,并令 *had_error = 1;
 * 此时返回的 AST 只包含解析成功的部分,不应被执行。 */
Node *parse_program(const char *src, int *had_error);
Node *parse_program_file(const char *src, const char *filename, int *had_error);

#endif
