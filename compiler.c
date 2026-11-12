#include "compiler.h"
#include "l_heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOCALS 256

typedef struct {
    const char *name;
    int depth;
    int is_captured;
    int is_const;
} Local;

typedef struct Loop {
    int start_offset;
    int scope_depth;
    int *break_jumps;
    int break_count;
    int break_cap;
    int continue_target;
    struct Loop *enclosing;
} Loop;

typedef struct Compiler {
    Interp *in;
    struct Compiler *enclosing;
    Proto *proto;
    Local locals[MAX_LOCALS];
    int local_count;
    int scope_depth;
    Loop *current_loop;
    int echo; /* REPL 回显模式 */
} Compiler;

static void compiler_init(Compiler *c, Interp *in, Compiler *enclosing, Proto *proto, int echo) {
    c->in = in;
    c->enclosing = enclosing;
    c->proto = proto;
    c->local_count = 0;
    c->scope_depth = 0;
    c->current_loop = NULL;
    c->echo = echo;

    /* slot 0 预留给函数自身/闭包对象 */
    Local *l0 = &c->locals[c->local_count++];
    l0->name = "";
    l0->depth = 0;
    l0->is_captured = 0;
    l0->is_const = 0;
}

/* ================= 字节码发射辅助函数 ================= */

static void emit_byte(Compiler *c, uint8_t byte, int line, int col) {
    chunk_write(&c->proto->chunk, byte, line, col);
}

static void emit_u16(Compiler *c, uint16_t u16, int line, int col) {
    chunk_write_u16(&c->proto->chunk, u16, line, col);
}

static void emit_op(Compiler *c, OpCode op, int line, int col) {
    emit_byte(c, (uint8_t)op, line, col);
}

static int emit_constant(Compiler *c, Value val, int line, int col) {
    int idx = chunk_add_constant(&c->proto->chunk, val);
    emit_op(c, OP_CONST, line, col);
    emit_u16(c, (uint16_t)idx, line, col);
    return idx;
}

static int make_ident_constant(Compiler *c, const char *ident) {
    Value s = val_str(str_new(c->in, ident, strlen(ident)));
    return chunk_add_constant(&c->proto->chunk, s);
}

static int emit_jump(Compiler *c, OpCode op, int line, int col) {
    emit_op(c, op, line, col);
    emit_byte(c, 0xFF, line, col);
    emit_byte(c, 0xFF, line, col);
    return c->proto->chunk.count - 2;
}

static void patch_jump(Compiler *c, int offset) {
    int jump = c->proto->chunk.count - offset - 2;
    if (jump > 32767 || jump < -32768) {
        fprintf(stderr, "跳转跨度过大 (溢出 16 位整型)\n");
    }
    c->proto->chunk.code[offset] = (uint8_t)(jump & 0xFF);
    c->proto->chunk.code[offset + 1] = (uint8_t)((jump >> 8) & 0xFF);
}

static void emit_loop(Compiler *c, int loop_start, int line, int col) {
    emit_op(c, OP_LOOP, line, col);
    int offset = c->proto->chunk.count - loop_start + 2;
    emit_u16(c, (uint16_t)offset, line, col);
}

/* ================= 作用域与局部变量管理 ================= */

static void begin_scope(Compiler *c) {
    c->scope_depth++;
}

static void end_scope(Compiler *c, int line, int col) {
    c->scope_depth--;
    while (c->local_count > 0 && c->locals[c->local_count - 1].depth > c->scope_depth) {
        if (c->locals[c->local_count - 1].is_captured) {
            emit_op(c, OP_CLOSE_UPVALUE, line, col);
        } else {
            emit_op(c, OP_POP, line, col);
        }
        c->local_count--;
    }
}

static int add_local(Compiler *c, const char *name, int is_const) {
    if (c->local_count >= MAX_LOCALS) {
        fprintf(stderr, "单个函数内局部变量超过上限 %d\n", MAX_LOCALS);
        return -1;
    }
    Local *local = &c->locals[c->local_count++];
    local->name = name;
    local->depth = c->scope_depth;
    local->is_captured = 0;
    local->is_const = is_const;
    return c->local_count - 1;
}

static int resolve_local(Compiler *c, const char *name) {
    for (int i = c->local_count - 1; i >= 0; i--) {
        if (strcmp(c->locals[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int add_upvalue(Compiler *c, uint8_t is_local, uint8_t index) {
    for (int i = 0; i < c->proto->upvalue_count; i++) {
        UpvalueDesc *u = &c->proto->upvalues[i];
        if (u->is_local == is_local && u->index == index) {
            return i;
        }
    }
    if (c->proto->upvalue_count + 1 > c->proto->upvalue_cap) {
        int old_cap = c->proto->upvalue_cap;
        c->proto->upvalue_cap = old_cap < 8 ? 8 : old_cap * 2;
        c->proto->upvalues = (UpvalueDesc *)gc_realloc(c->in, c->proto->upvalues, c->proto->upvalue_cap * sizeof(UpvalueDesc));
    }
    int idx = c->proto->upvalue_count++;
    c->proto->upvalues[idx].is_local = is_local;
    c->proto->upvalues[idx].index = index;
    return idx;
}

static int resolve_upvalue(Compiler *c, const char *name) {
    if (c->enclosing == NULL) return -1;

    int local = resolve_local(c->enclosing, name);
    if (local != -1) {
        c->enclosing->locals[local].is_captured = 1;
        return add_upvalue(c, 1, (uint8_t)local);
    }

    int upval = resolve_upvalue(c->enclosing, name);
    if (upval != -1) {
        return add_upvalue(c, 0, (uint8_t)upval);
    }

    return -1;
}

/* ================= 编译节点 (递归向下遍历 AST) ================= */

static void compile_node(Compiler *c, const Node *n);

static void compile_list(Compiler *c, const NodeList *list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        compile_node(c, list->items[i]);
    }
}

static void compile_expr(Compiler *c, const Node *n) {
    compile_node(c, n);
}

static void compile_fn_body(Compiler *c, const Node *fn_node, int is_anon) {
    Proto *sub_proto = proto_new(c->in, fn_node->as.fn.name ? fn_node->as.fn.name : (is_anon ? "<anonymous>" : "<fn>"), c->proto->source_file);
    sub_proto->arity = fn_node->as.fn.params.count;

    Compiler sub_compiler;
    compiler_init(&sub_compiler, c->in, c, sub_proto, 0);

    /* 压入形参为局部变量 */
    for (int i = 0; i < fn_node->as.fn.params.count; i++) {
        add_local(&sub_compiler, fn_node->as.fn.params.items[i], 0);
    }

    /* 编译函数体 */
    if (fn_node->as.fn.body->kind == N_BLOCK) {
        compile_list(&sub_compiler, &fn_node->as.fn.body->as.list);
    } else {
        compile_node(&sub_compiler, fn_node->as.fn.body);
    }

    /* 缺省末尾返回 nil */
    emit_op(&sub_compiler, OP_NIL, fn_node->line, fn_node->col);
    emit_op(&sub_compiler, OP_RETURN, fn_node->line, fn_node->col);

    /* 在当前外层发射 OP_CLOSURE */
    Func *fn_obj = func_proto_new(c->in, sub_proto);
    int proto_idx = chunk_add_constant(&c->proto->chunk, val_fn(fn_obj));

    emit_op(c, OP_CLOSURE, fn_node->line, fn_node->col);
    emit_u16(c, (uint16_t)proto_idx, fn_node->line, fn_node->col);

    /* 写入所有的 Upvalue 描述子 */
    for (int i = 0; i < sub_proto->upvalue_count; i++) {
        emit_byte(c, sub_proto->upvalues[i].is_local, fn_node->line, fn_node->col);
        emit_byte(c, sub_proto->upvalues[i].index, fn_node->line, fn_node->col);
    }
}

static void compile_node(Compiler *c, const Node *n) {
    if (!n) return;
    int line = n->line;
    int col = n->col;

    switch (n->kind) {
    case N_NIL:
        emit_op(c, OP_NIL, line, col);
        break;

    case N_BOOL:
        emit_op(c, n->as.boolean ? OP_TRUE : OP_FALSE, line, col);
        break;

    case N_INT:
        emit_constant(c, val_int((int32_t)n->as.ival), line, col);
        break;

    case N_NUMBER:
        emit_constant(c, val_num(n->as.number), line, col);
        break;

    case N_STRING: {
        Value s = val_str(str_new(c->in, n->as.str, strlen(n->as.str)));
        emit_constant(c, s, line, col);
        break;
    }

    case N_IDENT: {
        const char *name = n->as.str;
        int local = resolve_local(c, name);
        if (local != -1) {
            emit_op(c, OP_GET_LOCAL, line, col);
            emit_byte(c, (uint8_t)local, line, col);
        } else {
            int upval = resolve_upvalue(c, name);
            if (upval != -1) {
                emit_op(c, OP_GET_UPVALUE, line, col);
                emit_byte(c, (uint8_t)upval, line, col);
            } else {
                int name_idx = make_ident_constant(c, name);
                emit_op(c, OP_GET_GLOBAL, line, col);
                emit_u16(c, (uint16_t)name_idx, line, col);
            }
        }
        break;
    }

    case N_UNARY:
        compile_expr(c, n->as.binary.left);
        switch (n->as.binary.op) {
        case T_MINUS: emit_op(c, OP_NEG, line, col); break;
        case T_BANG:
        case T_NOT:   emit_op(c, OP_NOT, line, col); break;
        case T_TILDE: emit_op(c, OP_BIT_NOT, line, col); break;
        default: break;
        }
        break;

    case N_BINARY: {
        TokenType op = n->as.binary.op;
        if (op == T_AND) {
            compile_expr(c, n->as.binary.left);
            int jump = emit_jump(c, OP_JUMP_PEEK_FALSE, line, col);
            emit_op(c, OP_POP, line, col);
            compile_expr(c, n->as.binary.right);
            patch_jump(c, jump);
            break;
        }
        if (op == T_OR) {
            compile_expr(c, n->as.binary.left);
            int jump = emit_jump(c, OP_JUMP_PEEK_TRUE, line, col);
            emit_op(c, OP_POP, line, col);
            compile_expr(c, n->as.binary.right);
            patch_jump(c, jump);
            break;
        }

        compile_expr(c, n->as.binary.left);
        compile_expr(c, n->as.binary.right);
        switch (op) {
        case T_PLUS:       emit_op(c, OP_ADD, line, col); break;
        case T_MINUS:      emit_op(c, OP_SUB, line, col); break;
        case T_STAR:       emit_op(c, OP_MUL, line, col); break;
        case T_SLASH:      emit_op(c, OP_DIV, line, col); break;
        case T_PERCENT:    emit_op(c, OP_MOD, line, col); break;
        case T_AMP:        emit_op(c, OP_BIT_AND, line, col); break;
        case T_PIPE:       emit_op(c, OP_BIT_OR, line, col); break;
        case T_CARET:      emit_op(c, OP_BIT_XOR, line, col); break;
        case T_SHL:        emit_op(c, OP_SHL, line, col); break;
        case T_SHR:        emit_op(c, OP_SHR, line, col); break;
        case T_EQ_EQ:      emit_op(c, OP_EQ, line, col); break;
        case T_BANG_EQ:    emit_op(c, OP_NE, line, col); break;
        case T_LT:         emit_op(c, OP_LT, line, col); break;
        case T_LE:         emit_op(c, OP_LE, line, col); break;
        case T_GT:         emit_op(c, OP_GT, line, col); break;
        case T_GE:         emit_op(c, OP_GE, line, col); break;
        default: break;
        }
        break;
    }

    case N_TERNARY: {
        compile_expr(c, n->as.ternary.cond);
        int else_jump = emit_jump(c, OP_JUMP_IF_FALSE, line, col);
        compile_expr(c, n->as.ternary.then_expr);
        int end_jump = emit_jump(c, OP_JUMP, line, col);
        patch_jump(c, else_jump);
        compile_expr(c, n->as.ternary.else_expr);
        patch_jump(c, end_jump);
        break;
    }

    case N_ASSIGN: {
        Node *target = n->as.assign.target;
        if (target->kind == N_IDENT) {
            compile_expr(c, n->as.assign.value);
            const char *name = target->as.str;
            int local = resolve_local(c, name);
            if (local != -1) {
                emit_op(c, OP_SET_LOCAL, line, col);
                emit_byte(c, (uint8_t)local, line, col);
            } else {
                int upval = resolve_upvalue(c, name);
                if (upval != -1) {
                    emit_op(c, OP_SET_UPVALUE, line, col);
                    emit_byte(c, (uint8_t)upval, line, col);
                } else {
                    int name_idx = make_ident_constant(c, name);
                    emit_op(c, OP_SET_GLOBAL, line, col);
                    emit_u16(c, (uint16_t)name_idx, line, col);
                }
            }
        } else if (target->kind == N_MEMBER) {
            compile_expr(c, target->as.member.obj);
            compile_expr(c, n->as.assign.value);
            int name_idx = make_ident_constant(c, target->as.member.name);
            emit_op(c, OP_SET_PROP, line, col);
            emit_u16(c, (uint16_t)name_idx, line, col);
        } else if (target->kind == N_INDEX) {
            compile_expr(c, target->as.index.obj);
            compile_expr(c, target->as.index.index);
            compile_expr(c, n->as.assign.value);
            emit_op(c, OP_INDEX_SET, line, col);
        }
        break;
    }

    case N_MEMBER: {
        compile_expr(c, n->as.member.obj);
        int name_idx = make_ident_constant(c, n->as.member.name);
        emit_op(c, OP_GET_PROP, line, col);
        emit_u16(c, (uint16_t)name_idx, line, col);
        break;
    }

    case N_INDEX:
        compile_expr(c, n->as.index.obj);
        compile_expr(c, n->as.index.index);
        emit_op(c, OP_INDEX_GET, line, col);
        break;

    case N_CALL: {
        Node *callee = n->as.call.callee;
        int argc = n->as.call.args.count;

        /* 优化方法调用: obj.method(args...) -> OP_INVOKE */
        if (callee->kind == N_MEMBER) {
            compile_expr(c, callee->as.member.obj);
            for (int i = 0; i < argc; i++) {
                compile_expr(c, n->as.call.args.items[i]);
            }
            int name_idx = make_ident_constant(c, callee->as.member.name);
            emit_op(c, OP_INVOKE, line, col);
            emit_u16(c, (uint16_t)name_idx, line, col);
            emit_byte(c, (uint8_t)argc, line, col);
        } else {
            compile_expr(c, callee);
            for (int i = 0; i < argc; i++) {
                compile_expr(c, n->as.call.args.items[i]);
            }
            emit_op(c, OP_CALL, line, col);
            emit_byte(c, (uint8_t)argc, line, col);
        }
        break;
    }

    case N_ARRAY: {
        int count = n->as.list.count;
        for (int i = 0; i < count; i++) {
            compile_expr(c, n->as.list.items[i]);
        }
        emit_op(c, OP_ARRAY, line, col);
        emit_u16(c, (uint16_t)count, line, col);
        break;
    }

    case N_MAP: {
        int count = n->as.map.keys.count;
        for (int i = 0; i < count; i++) {
            Value k = val_str(str_new(c->in, n->as.map.keys.items[i], strlen(n->as.map.keys.items[i])));
            emit_constant(c, k, line, col);
            compile_expr(c, n->as.map.values.items[i]);
        }
        emit_op(c, OP_MAP, line, col);
        emit_u16(c, (uint16_t)count, line, col);
        break;
    }

    case N_FN: {
        compile_fn_body(c, n, n->as.fn.name == NULL);
        if (n->as.fn.name) {
            if (c->scope_depth > 0) {
                add_local(c, n->as.fn.name, 0);
            } else {
                int name_idx = make_ident_constant(c, n->as.fn.name);
                emit_op(c, OP_DEF_GLOBAL, line, col);
                emit_u16(c, (uint16_t)name_idx, line, col);
            }
        }
        break;
    }

    case N_LET: {
        if (n->as.let.init) {
            compile_expr(c, n->as.let.init);
        } else {
            emit_op(c, OP_NIL, line, col);
        }
        if (c->scope_depth > 0) {
            add_local(c, n->as.let.name, n->as.let.is_const);
        } else {
            int name_idx = make_ident_constant(c, n->as.let.name);
            emit_op(c, OP_DEF_GLOBAL, line, col);
            emit_u16(c, (uint16_t)name_idx, line, col);
        }
        break;
    }

    case N_EXPR_STMT:
        compile_expr(c, n->as.expr);
        if (c->echo && c->scope_depth == 0) {
            emit_op(c, OP_ECHO, line, col);
        } else {
            emit_op(c, OP_POP, line, col);
        }
        break;

    case N_IF: {
        compile_expr(c, n->as.if_stmt.cond);
        int else_jump = emit_jump(c, OP_JUMP_IF_FALSE, line, col);
        compile_node(c, n->as.if_stmt.then_b);
        if (n->as.if_stmt.else_b) {
            int exit_jump = emit_jump(c, OP_JUMP, line, col);
            patch_jump(c, else_jump);
            compile_node(c, n->as.if_stmt.else_b);
            patch_jump(c, exit_jump);
        } else {
            patch_jump(c, else_jump);
        }
        break;
    }

    case N_WHILE: {
        Loop loop;
        loop.start_offset = c->proto->chunk.count;
        loop.scope_depth = c->scope_depth;
        loop.break_jumps = NULL;
        loop.break_count = 0;
        loop.break_cap = 0;
        loop.continue_target = loop.start_offset;
        loop.enclosing = c->current_loop;
        c->current_loop = &loop;

        compile_expr(c, n->as.while_stmt.cond);
        int exit_jump = emit_jump(c, OP_JUMP_IF_FALSE, line, col);

        compile_node(c, n->as.while_stmt.body);
        emit_loop(c, loop.start_offset, line, col);

        patch_jump(c, exit_jump);
        for (int i = 0; i < loop.break_count; i++) {
            patch_jump(c, loop.break_jumps[i]);
        }
        free(loop.break_jumps);
        c->current_loop = loop.enclosing;
        break;
    }

    case N_FOR: {
        begin_scope(c);
        if (n->as.for_stmt.init) {
            compile_node(c, n->as.for_stmt.init);
        }

        Loop loop;
        loop.start_offset = c->proto->chunk.count;
        loop.scope_depth = c->scope_depth;
        loop.break_jumps = NULL;
        loop.break_count = 0;
        loop.break_cap = 0;
        loop.enclosing = c->current_loop;
        c->current_loop = &loop;

        int exit_jump = -1;
        if (n->as.for_stmt.cond) {
            compile_expr(c, n->as.for_stmt.cond);
            exit_jump = emit_jump(c, OP_JUMP_IF_FALSE, line, col);
        }

        compile_node(c, n->as.for_stmt.body);

        loop.continue_target = c->proto->chunk.count;
        if (n->as.for_stmt.step) {
            compile_expr(c, n->as.for_stmt.step);
            emit_op(c, OP_POP, line, col);
        }

        emit_loop(c, loop.start_offset, line, col);

        if (exit_jump != -1) patch_jump(c, exit_jump);
        for (int i = 0; i < loop.break_count; i++) {
            patch_jump(c, loop.break_jumps[i]);
        }
        free(loop.break_jumps);
        c->current_loop = loop.enclosing;

        end_scope(c, line, col);
        break;
    }

    case N_RETURN:
        if (n->as.expr) {
            compile_expr(c, n->as.expr);
        } else {
            emit_op(c, OP_NIL, line, col);
        }
        emit_op(c, OP_RETURN, line, col);
        break;

    case N_BREAK:
        if (!c->current_loop) {
            fprintf(stderr, "[%d:%d] 语法错误: break 只能在循环体内部使用\n", line, col);
            break;
        }
        /* 离开循环前先清理当前内层作用域局部变量 */
        for (int i = c->local_count - 1; i >= 0 && c->locals[i].depth > c->current_loop->scope_depth; i--) {
            if (c->locals[i].is_captured) emit_op(c, OP_CLOSE_UPVALUE, line, col);
            else emit_op(c, OP_POP, line, col);
        }
        if (c->current_loop->break_count + 1 > c->current_loop->break_cap) {
            int old_cap = c->current_loop->break_cap;
            c->current_loop->break_cap = old_cap < 4 ? 4 : old_cap * 2;
            c->current_loop->break_jumps = realloc(c->current_loop->break_jumps, c->current_loop->break_cap * sizeof(int));
        }
        c->current_loop->break_jumps[c->current_loop->break_count++] = emit_jump(c, OP_JUMP, line, col);
        break;

    case N_CONTINUE:
        if (!c->current_loop) {
            fprintf(stderr, "[%d:%d] 语法错误: continue 只能在循环体内部使用\n", line, col);
            break;
        }
        for (int i = c->local_count - 1; i >= 0 && c->locals[i].depth > c->current_loop->scope_depth; i--) {
            if (c->locals[i].is_captured) emit_op(c, OP_CLOSE_UPVALUE, line, col);
            else emit_op(c, OP_POP, line, col);
        }
        emit_loop(c, c->current_loop->continue_target, line, col);
        break;

    case N_BLOCK:
        begin_scope(c);
        compile_list(c, &n->as.list);
        end_scope(c, line, col);
        break;

    case N_TRY: {
        int try_jump = emit_jump(c, OP_TRY, line, col);
        compile_node(c, n->as.try_stmt.try_b);
        emit_op(c, OP_END_TRY, line, col);
        int after_catch = emit_jump(c, OP_JUMP, line, col);

        patch_jump(c, try_jump);
        begin_scope(c);
        if (n->as.try_stmt.err_var) {
            add_local(c, n->as.try_stmt.err_var, 0);
        } else {
            emit_op(c, OP_POP, line, col);
        }
        compile_node(c, n->as.try_stmt.catch_b);
        end_scope(c, line, col);
        patch_jump(c, after_catch);
        break;
    }

    case N_THROW:
        compile_expr(c, n->as.expr);
        emit_op(c, OP_THROW, line, col);
        break;

    case N_PROGRAM:
        compile_list(c, &n->as.list);
        break;

    case N_IMPORT:
        /* import 语句在 VM 下由辅助内置函数或顶层处理 */
        break;
    }
}

Proto *compile_to_proto(Interp *in, const Node *program, const char *source_file, int echo) {
    Proto *proto = proto_new(in, "<main>", source_file);
    Compiler compiler;
    compiler_init(&compiler, in, NULL, proto, echo);

    compile_node(&compiler, program);

    /* 顶层结尾默认停机或返回 nil */
    emit_op(&compiler, OP_NIL, program ? program->line : 1, program ? program->col : 1);
    emit_op(&compiler, OP_HALT, program ? program->line : 1, program ? program->col : 1);

    return proto;
}
