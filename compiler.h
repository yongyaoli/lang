#ifndef L_COMPILER_H
#define L_COMPILER_H

#include "ast.h"
#include "bytecode.h"
#include "interp.h"

/* 编译给定的 AST 树并生成可由 VM 直接执行的顶层函数原型 Proto */
Proto *compile_to_proto(Interp *in, const Node *program, const char *source_file, int echo);

#endif /* L_COMPILER_H */
