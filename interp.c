/* 树遍历解释器:递归遍历 AST,按节点类型求值/执行 */
#include "interp.h"
#include "compiler.h"
#include "vm.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH MAX_CALL_DEPTH      /* 最大调用深度,防止 C 栈溢出 */

/* 语句执行结果:用返回值传递 break / continue / return,不用异常 */
typedef enum { SIG_NORMAL, SIG_BREAK, SIG_CONTINUE, SIG_RETURN } Signal;

static Value eval(Interp *in, const Node *n);
static Signal exec(Interp *in, const Node *n);
static Signal exec_import(Interp *in, const Node *n);

/* ================= 错误与异常 ================= */

/* 打印出错源码行及 ^ 指示符 */
static void print_node_snippet(const Node *at) {
    if (!at || !at->line_start || at->col < 1 || at->line <= 0) return;
    const char *end = at->line_start;
    while (*end && *end != '\n' && *end != '\r') end++;
    int len = (int)(end - at->line_start);
    if (len <= 0) return;
    fprintf(stderr, "  %4d | %.*s\n       | ", at->line, len, at->line_start);
    for (int i = 1; i < at->col && i <= len; i++) {
        fputc(at->line_start[i - 1] == '\t' ? '\t' : ' ', stderr);
    }
    fprintf(stderr, "^\n");
}

/* 打印调用栈回溯 */
static void print_stack_trace(const Interp *in, const Node *at) {
    if (in->depth <= 0) return;
    fprintf(stderr, "  调用栈回溯:\n");
    const char *cur_fn = in->current_fn ? in->current_fn : "<main>";
    const char *cur_file = (at && at->file) ? at->file : (in->current_file ? in->current_file : "<repl>");
    fprintf(stderr, "    at %s (%s:%d:%d)\n", cur_fn, cur_file, at ? at->line : 0, at ? at->col : 0);
    int top = in->depth < MAX_TRACE_DEPTH ? in->depth : MAX_TRACE_DEPTH;
    for (int i = top - 1; i >= 0; i--) {
        const Node *cs = in->trace[i].call_site;
        const char *fn = in->trace[i].fn_name ? in->trace[i].fn_name : "<main>";
        const char *f = (cs && cs->file) ? cs->file : (in->current_file ? in->current_file : "<repl>");
        fprintf(stderr, "    at %s (%s:%d:%d)\n", fn, f, cs ? cs->line : 0, cs ? cs->col : 0);
    }
}

/* 构造带完整定位与调用栈的异常 Map 对象 */
static Value build_error_object(Interp *in, const Node *at, const char *err_type,
                                const char *msg, Value base_map) {
    int saved_gc = in->gc_disabled;
    in->gc_disabled = 1;    /* 构造异常对象期间暂停触发 GC */

    Map *m = (base_map.type == V_MAP && base_map.as.map) ? base_map.as.map : map_new(in);
    if (!map_get(m, "type"))
        map_set(m, str_new(in, "type", 4), val_str(str_new(in, err_type, strlen(err_type))));
    if (msg && !map_get(m, "message"))
        map_set(m, str_new(in, "message", 7), val_str(str_new(in, msg, strlen(msg))));

    const char *file = (at && at->file) ? at->file : (in->current_file ? in->current_file : "<repl>");
    if (!map_get(m, "file"))
        map_set(m, str_new(in, "file", 4), val_str(str_new(in, file, strlen(file))));
    if (!map_get(m, "line"))
        map_set(m, str_new(in, "line", 4), val_int(at ? at->line : 0));
    if (!map_get(m, "col"))
        map_set(m, str_new(in, "col", 3), val_int(at ? at->col : 0));

    const char *fn = in->current_fn ? in->current_fn : "<main>";
    if (!map_get(m, "fn"))
        map_set(m, str_new(in, "fn", 2), val_str(str_new(in, fn, strlen(fn))));
    if (!map_get(m, "func"))
        map_set(m, str_new(in, "func", 4), val_str(str_new(in, fn, strlen(fn))));

    if (at && at->line_start && !map_get(m, "source")) {
        const char *s = at->line_start;
        while (*s == ' ' || *s == '\t') s++;
        const char *e = s;
        while (*e && *e != '\n' && *e != '\r') e++;
        map_set(m, str_new(in, "source", 6), val_str(str_new(in, s, (size_t)(e - s))));
    }

    if (!map_get(m, "stack")) {
        Array *st = array_new(in);
        char buf[512];
        snprintf(buf, sizeof buf, "%s (%s:%d:%d)", fn, file, at ? at->line : 0, at ? at->col : 0);
        array_push(st, val_str(str_new(in, buf, strlen(buf))));
        int top = in->depth < MAX_TRACE_DEPTH ? in->depth : MAX_TRACE_DEPTH;
        for (int i = top - 1; i >= 0; i--) {
            const Node *cs = in->trace[i].call_site;
            const char *cfn = in->trace[i].fn_name ? in->trace[i].fn_name : "<main>";
            const char *cf = (cs && cs->file) ? cs->file : (in->current_file ? in->current_file : "<repl>");
            snprintf(buf, sizeof buf, "%s (%s:%d:%d)", cfn, cf, cs ? cs->line : 0, cs ? cs->col : 0);
            array_push(st, val_str(str_new(in, buf, strlen(buf))));
        }
        map_set(m, str_new(in, "stack", 5), val_map(m)); /* 占位防告警, 下一行赋真正数组 */
        map_set(m, str_new(in, "stack", 5), val_array(st));
    }

    in->gc_disabled = saved_gc;
    return val_map(m);
}

/* 抛出异常(若在 try 块内则跳转到最近的 catch, 否则打印定位与调用栈并中止) */
static L_NORETURN void raise_error(Interp *in, const Node *at, const char *kind_label,
                                   const char *err_type, const char *msg, Value base_map) {
    if (in->try_top) {
        in->last_error = build_error_object(in, at, err_type, msg, base_map);
        longjmp(in->try_top->jb, 1);
    }
    const char *file = (at && at->file) ? at->file : in->current_file;
    if (at && file && *file)
        fprintf(stderr, "[%s:%d:%d] %s: %s\n", file, at->line, at->col, kind_label, msg);
    else if (at)
        fprintf(stderr, "[%d:%d] %s: %s\n", at->line, at->col, kind_label, msg);
    else
        fprintf(stderr, "%s: %s\n", kind_label, msg);
    print_node_snippet(at);
    print_stack_trace(in, at);
    longjmp(in->jb, 1);
}

void rt_error(Interp *in, const Node *at, const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    raise_error(in, at, "运行时错误", "RuntimeError", msg, val_nil());
}

/* ================= 环境 ================= */

Env *env_new(Interp *in, Env *parent) {
    Env *e = (Env *)gc_alloc(in, sizeof(Env), OBJ_ENV);
    e->names = NULL;
    e->vals = NULL;
    e->consts = NULL;
    e->count = 0;
    e->cap = 0;
    e->parent = parent;
    return e;
}

static int env_find_local_idx(Env *e, const char *name) {
    for (int i = 0; i < e->count; i++)
        if (e->names[i] == name || strcmp(e->names[i], name) == 0) return i;
    return -1;
}

Value *env_find(Env *e, const char *name) {
    for (; e; e = e->parent) {
        int idx = env_find_local_idx(e, name);
        if (idx >= 0) return &e->vals[idx];
    }
    return NULL;
}

static Value *env_find_ex(Env *e, const char *name, int *out_is_const) {
    for (; e; e = e->parent) {
        int idx = env_find_local_idx(e, name);
        if (idx >= 0) {
            if (out_is_const) *out_is_const = e->consts[idx];
            return &e->vals[idx];
        }
    }
    return NULL;
}

static void env_define_ex(Interp *in, const Node *at, Env *e,
                          const char *name, Value v, int is_const) {
    int idx = env_find_local_idx(e, name);
    if (idx >= 0) {
        if (e->consts[idx] && in && at)
            rt_error(in, at, "不能重新定义常量 '%s'", name);
        e->vals[idx] = v;
        e->consts[idx] = (uint8_t)(is_const != 0);
        return;
    }
    if (e->count == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 4;
        e->names = realloc(e->names, sizeof(char *) * (size_t)e->cap);
        e->vals = realloc(e->vals, sizeof(Value) * (size_t)e->cap);
        e->consts = realloc(e->consts, sizeof(uint8_t) * (size_t)e->cap);
    }
    e->names[e->count] = name;
    e->vals[e->count] = v;
    e->consts[e->count] = (uint8_t)(is_const != 0);
    e->count++;
}

void env_define(Env *e, const char *name, Value v) {
    env_define_ex(NULL, NULL, e, name, v, 0);
}

/* ================= 函数调用 ================= */

static Value make_closure(Interp *in, const Node *decl) {
    Func *f = func_new(in, decl, in->env);
    Value v = val_nil();
    v.type = V_FN;
    v.as.fn = f;
    return v;
}

/* 在当前环境中依次执行语句列表 */
static Signal exec_list(Interp *in, const NodeList *l) {
    for (int i = 0; i < l->count; i++) {
        Signal s = exec(in, l->items[i]);
        if (s != SIG_NORMAL) return s;
    }
    return SIG_NORMAL;
}

static Value call_value_ext(Interp *in, const Node *at, Value callee,
                            int has_self, Value self_val, int argc, Value *argv) {
    if (callee.type == V_NATIVE) {
        const Native *nt = callee.as.native;
        if (argc < nt->min_args || (nt->max_args >= 0 && argc > nt->max_args))
            rt_error(in, at, "内置函数 %s 参数数量错误(传入 %d 个)", nt->name, argc);
        return nt->fn(in, at, argc, argv);
    }
    if (callee.type == V_OBJ && argc == 0) {
        return callee;          /* 支持 ESP32().info() 与 ESP32.info() 两种写法 */
    }
    if (callee.type != V_FN)
        rt_error(in, at, "只有函数可以被调用,实际类型是 %s", type_name(callee));

    Func *f = callee.as.fn;
    const NameList *params = &f->decl->as.fn.params;
    /* 方法调用兼容: 若首个形参名为 "self" 且调用方未显式传 self, 自动补入 self_val */
    int prepend_self = (has_self && params->count == argc + 1 &&
                        strcmp(params->items[0], "self") == 0);
    int total_argc = prepend_self ? (argc + 1) : argc;
    if (total_argc != params->count)
        rt_error(in, at, "函数 %s 期望 %d 个参数,实际传入 %d 个",
                 f->decl->as.fn.name ? f->decl->as.fn.name : "<anonymous>",
                 params->count, total_argc);
    if (in->depth >= MAX_DEPTH) rt_error(in, at, "调用栈溢出(递归过深)");

    Env *e = env_new(in, f->closure);
    if (has_self) env_define(e, "self", self_val);
    if (prepend_self) {
        env_define(e, params->items[0], self_val);
        for (int i = 0; i < argc; i++) env_define(e, params->items[i + 1], argv[i]);
    } else {
        for (int i = 0; i < argc; i++) env_define(e, params->items[i], argv[i]);
    }

    Env *saved = in->env;
    const char *saved_fn = in->current_fn;
    if (in->depth < MAX_TRACE_DEPTH) {
        in->trace[in->depth].fn_name = saved_fn ? saved_fn : "<main>";
        in->trace[in->depth].call_site = at;
    }
    in->env = e;
    in->current_fn = f->decl->as.fn.name ? f->decl->as.fn.name : "<anonymous>";
    in->depth++;
    in->call_frames[in->frame_count++] = saved;
    Signal s = exec_list(in, &f->decl->as.fn.body->as.list);   /* 参数与函数体同一作用域 */
    in->frame_count--;
    in->depth--;
    in->current_fn = saved_fn;
    in->env = saved;

    if (s == SIG_BREAK || s == SIG_CONTINUE)
        rt_error(in, f->decl, "break / continue 只能出现在循环中");
    if (s == SIG_RETURN) {
        Value r = in->ret;
        in->ret = val_nil();
        return r;
    }
    return val_nil();
}

static Value call_value(Interp *in, const Node *at, Value callee, int argc, Value *argv) {
    return call_value_ext(in, at, callee, 0, val_nil(), argc, argv);
}

Value interp_call(Interp *in, const Node *at, Value callee, int argc, Value *argv) {
    return call_value(in, at, callee, argc, argv);
}

/* ================= 表达式 ================= */

static double need_num(Interp *in, const Node *n, Value v, const char *what) {
    if (!val_is_number(v)) rt_error(in, n, "%s 需要数字,实际是 %s", what, type_name(v));
    return val_to_double(v);
}

/* 要求整数: int 直接用, 值恰为整数的 float 也接受(如 2.0), 其余报错 */
static int32_t need_int(Interp *in, const Node *n, Value v, const char *what) {
    if (v.type == V_INT) return v.as.i;
    if (v.type == V_NUM && v.as.num == floor(v.as.num) &&
        v.as.num >= -2147483648.0 && v.as.num <= 2147483647.0) return (int32_t)v.as.num;
    rt_error(in, n, "%s 需要整数,实际是 %s", what, type_name(v));
}

/* 64 位中间结果回绕为 int32 (与 C 的无符号回绕一致, 避免有符号溢出 UB) */
static int32_t wrap32(int64_t x) { return (int32_t)(uint32_t)(uint64_t)x; }

static Value eval_binary(Interp *in, const Node *n) {
    TokenType op = n->as.binary.op;

    /* and / or:短路求值,返回决定结果的那个操作数本身 */
    if (op == T_AND || op == T_OR) {
        Value l = eval(in, n->as.binary.left);
        if (op == T_AND ? !is_truthy(l) : is_truthy(l)) return l;
        return eval(in, n->as.binary.right);
    }

    Value l = eval(in, n->as.binary.left);
    Value r = eval(in, n->as.binary.right);

    switch (op) {
    case T_EQ_EQ:   return val_bool(values_equal(l, r));
    case T_BANG_EQ: return val_bool(!values_equal(l, r));

    case T_PLUS:
        if (l.type == V_INT && r.type == V_INT) return val_int(wrap32((int64_t)l.as.i + r.as.i));
        if (val_is_number(l) && val_is_number(r)) return val_num(val_to_double(l) + val_to_double(r));
        if (l.type == V_STR && r.type == V_STR) {
            size_t a = l.as.str ? l.as.str->len : 0;
            size_t b = r.as.str ? r.as.str->len : 0;
            gc_push_root(in, l);
            gc_push_root(in, r);
            Str *res = str_new(in, NULL, a + b);
            if (a) memcpy(res->chars, l.as.str->chars, a);
            if (b) memcpy(res->chars + a, r.as.str->chars, b);
            res->chars[a + b] = '\0';
            gc_pop_roots(in, 2);
            return val_str(res);
        }
        rt_error(in, n, "'+' 只能用于两个数字或两个字符串,实际是 %s 和 %s",
                 type_name(l), type_name(r));

    case T_LT: case T_LE: case T_GT: case T_GE: {
        int c;
        if (val_is_number(l) && val_is_number(r)) {
            double x = val_to_double(l), y = val_to_double(r);
            c = (x > y) - (x < y);
        }
        else if (l.type == V_STR && r.type == V_STR)
            c = strcmp(val_get_cstr(l), val_get_cstr(r));
        else
            rt_error(in, n, "比较运算只能用于两个数字或两个字符串,实际是 %s 和 %s",
                     type_name(l), type_name(r));
        if (op == T_LT) return val_bool(c < 0);
        if (op == T_LE) return val_bool(c <= 0);
        if (op == T_GT) return val_bool(c > 0);
        return val_bool(c >= 0);
    }

    default: break;
    }

    /* 剩下的都是纯数字运算 */
    if (!val_is_number(l) || !val_is_number(r)) {
        rt_error(in, n, "算术/位运算符需要数字,实际是 %s 和 %s", type_name(l), type_name(r));
    }

    /* 位运算: 只接受 int */
    if (op == T_AMP || op == T_PIPE || op == T_CARET || op == T_SHL || op == T_SHR) {
        int32_t x = need_int(in, n, l, "位运算符"), y = need_int(in, n, r, "位运算符");
        switch (op) {
        case T_AMP:   return val_int(x & y);
        case T_PIPE:  return val_int(x | y);
        case T_CARET: return val_int(x ^ y);
        default:
            if (y < 0 || y > 31) rt_error(in, n, "移位位数必须在 0..31 之间,实际是 %d", (int)y);
            if (op == T_SHL) return val_int((int32_t)((uint32_t)x << y));
            return val_int(x >> y);     /* 算术右移 */
        }
    }

    /* int 与 int: 整数运算 (C 语义: / 截断取整), 结果回绕在 32 位 */
    if (l.type == V_INT && r.type == V_INT) {
        int64_t x = l.as.i, y = r.as.i;
        switch (op) {
        case T_PLUS:  return val_int(wrap32(x + y));
        case T_MINUS: return val_int(wrap32(x - y));
        case T_STAR:  return val_int(wrap32(x * y));
        case T_SLASH:
            if (y == 0) rt_error(in, n, "除数不能为 0");
            return val_int(wrap32(x / y));
        case T_PERCENT:
            if (y == 0) rt_error(in, n, "取模的除数不能为 0");
            return val_int(wrap32(x % y));
        default: break;
        }
    }

    /* 其余(至少一个 float): 浮点运算 */
    double a = val_to_double(l), b = val_to_double(r);
    switch (op) {
    case T_PLUS:  return val_num(a + b);
    case T_MINUS: return val_num(a - b);
    case T_STAR:  return val_num(a * b);
    case T_SLASH:
        if (b == 0) rt_error(in, n, "除数不能为 0");
        return val_num(a / b);
    case T_PERCENT:
        if (b == 0) rt_error(in, n, "取模的除数不能为 0");
        return val_num(fmod(a, b));
    default: break;
    }
    rt_error(in, n, "未知的二元运算符");
}

/* 下标合法性检查:必须是 [0, count) 内的整数 */
static int check_index(Interp *in, const Node *n, Value idx, int count) {
    double d = need_num(in, n, idx, "下标");
    if (d != floor(d) || d < 0 || d >= count)
        rt_error(in, n, "下标越界: %g(长度 %d)", d, count);
    return (int)d;
}

static Value index_get(Interp *in, const Node *n, Value obj, Value idx) {
    switch (obj.type) {
    case V_ARRAY: return obj.as.array->items[check_index(in, n, idx, obj.as.array->count)];
    case V_BYTES: {
        size_t blen = obj.as.bytes ? obj.as.bytes->len : 0;
        int i = check_index(in, n, idx, (int)blen);
        return val_int(obj.as.bytes->data[i]);
    }
    case V_STR: {
        size_t slen = obj.as.str ? obj.as.str->len : 0;
        int i = check_index(in, n, idx, (int)slen);
        gc_push_root(in, obj);
        char ch = obj.as.str->chars[i];
        Str *s = str_new(in, &ch, 1);
        gc_pop_roots(in, 1);
        return val_str(s);
    }
    case V_MAP: {
        if (idx.type != V_STR) rt_error(in, n, "map 的键必须是字符串,实际是 %s", type_name(idx));
        Value *v = map_get(obj.as.map, val_get_cstr(idx));
        return v ? *v : val_nil();  /* 键不存在返回 nil */
    }
    default:
        rt_error(in, n, "%s 类型不支持下标/成员访问", type_name(obj));
    }
}

static void index_set(Interp *in, const Node *n, Value obj, Value idx, Value v) {
    if (obj.type == V_ARRAY) {
        obj.as.array->items[check_index(in, n, idx, obj.as.array->count)] = v;
    } else if (obj.type == V_BYTES) {
        size_t blen = obj.as.bytes ? obj.as.bytes->len : 0;
        int i = check_index(in, n, idx, (int)blen);
        int32_t bv = need_int(in, n, v, "bytes 元素赋值");
        if (bv < 0 || bv > 255)
            rt_error(in, n, "bytes 元素值必须在 0..255 之间,实际是 %d", (int)bv);
        obj.as.bytes->data[i] = (uint8_t)bv;
    } else if (obj.type == V_MAP) {
        if (idx.type != V_STR) rt_error(in, n, "map 的键必须是字符串,实际是 %s", type_name(idx));
        map_set(obj.as.map, idx.as.str, v);
    } else {
        rt_error(in, n, "%s 类型不支持下标赋值", type_name(obj));
    }
}

static Value eval_assign(Interp *in, const Node *n) {
    const Node *t = n->as.assign.target;
    switch (t->kind) {
    case N_IDENT: {
        Value v = eval(in, n->as.assign.value);
        int is_const = 0;
        Value *slot = env_find_ex(in->env, t->as.str, &is_const);
        if (!slot) rt_error(in, t, "未定义的变量 '%s'", t->as.str);
        if (is_const) rt_error(in, t, "不能修改常量 '%s'", t->as.str);
        *slot = v;
        return v;
    }
    case N_INDEX: {
        Value obj = eval(in, t->as.index.obj);
        Value idx = eval(in, t->as.index.index);
        Value v = eval(in, n->as.assign.value);
        index_set(in, t, obj, idx, v);
        return v;
    }
    default: { /* N_MEMBER */
        Value obj = eval(in, t->as.member.obj);
        Value v = eval(in, n->as.assign.value);
        gc_push_root(in, obj);
        gc_push_root(in, v);
        Str *key = str_new(in, t->as.member.name, strlen(t->as.member.name));
        index_set(in, t, obj, val_str(key), v);
        gc_pop_roots(in, 2);
        return v;
    }
    }
}

/* 判断字符串方法名是否允许转发到同名内置函数 */
static int is_str_method(const char *m) {
    static const char *const allowed[] = {
        "len", "ulen", "upper", "lower", "trim", "split", "substr",
        "usubstr", "uat", "find", "replace", "starts_with", "ends_with",
        "repeat", "ord"
    };
    for (size_t i = 0; i < sizeof allowed / sizeof allowed[0]; i++)
        if (strcmp(m, allowed[i]) == 0) return 1;
    return 0;
}

/* 判断数组方法名是否允许转发到同名内置函数 */
static int is_array_method(const char *m) {
    static const char *const allowed[] = {
        "len", "push", "pop", "join", "slice", "reverse",
        "sort", "find", "map", "filter", "reduce"
    };
    for (size_t i = 0; i < sizeof allowed / sizeof allowed[0]; i++)
        if (strcmp(m, allowed[i]) == 0) return 1;
    return 0;
}

Value interp_invoke_method(Interp *in, const Node *n, Value obj, const char *mname, int argc, Value *argv) {
    Value argv_buf[16];
    Value *full_argv = (argc + 2 <= 16) ? argv_buf : malloc(sizeof(Value) * (size_t)(argc + 2));
    full_argv[0] = obj;
    for (int i = 0; i < argc; i++) full_argv[i + 1] = argv[i];

    Value r = val_nil();
    if (obj.type == V_OBJ || obj.type == V_BYTES) {
        const ObjClass *cls = (obj.type == V_BYTES) ? &bytes_class
                            : (obj.as.obj ? obj.as.obj->cls : NULL);
        const ObjMethod *found = NULL;
        if (cls) {
            for (size_t i = 0; i < cls->method_count; i++) {
                if (strcmp(cls->methods[i].name, mname) == 0) {
                    found = &cls->methods[i];
                    break;
                }
            }
        }
        if (!found) {
            if (full_argv != argv_buf) free(full_argv);
            rt_error(in, n, "%s 对象没有方法 '%s'", type_name(obj), mname);
        }
        if (argc < found->min_args || (found->max_args >= 0 && argc > found->max_args)) {
            if (full_argv != argv_buf) free(full_argv);
            rt_error(in, n, "方法 %s.%s() 参数数量错误(传入 %d 个)", cls ? cls->name : "Object", mname, argc);
        }
        r = found->fn(in, n, obj, argc, full_argv + 1);
    } else if (obj.type == V_MAP) {
        Value *slot = map_get(obj.as.map, mname);
        if (slot) {
            r = call_value_ext(in, n, *slot, 1, obj, argc, full_argv + 1);
        } else if (strcmp(mname, "len") == 0 && argc == 0) {
            r = val_int(obj.as.map->count);
        } else if (strcmp(mname, "keys") == 0 && argc == 0) {
            const Native *nt = builtins_find_native("keys");
            r = nt->fn(in, n, 1, full_argv);
        } else if (strcmp(mname, "has") == 0 && argc == 1) {
            need_arg(in, n, "map.has", full_argv + 1, 0, V_STR, "string");
            r = val_bool(map_get(obj.as.map, val_get_cstr(full_argv[1])) != NULL);
        } else if (strcmp(mname, "remove") == 0 && argc == 1) {
            need_arg(in, n, "map.remove", full_argv + 1, 0, V_STR, "string");
            r = val_bool(map_remove(obj.as.map, val_get_cstr(full_argv[1])));
        } else {
            if (full_argv != argv_buf) free(full_argv);
            rt_error(in, n, "map 中不存在键或方法 '%s'", mname);
        }
    } else if ((obj.type == V_STR && is_str_method(mname)) ||
               (obj.type == V_ARRAY && is_array_method(mname))) {
        const Native *nt = builtins_find_native(mname);
        if (!nt) {
            if (full_argv != argv_buf) free(full_argv);
            rt_error(in, n, "%s 类型没有方法 '%s'", type_name(obj), mname);
        }
        Value nv = val_nil();
        nv.type = V_NATIVE;
        nv.as.native = nt;
        r = call_value(in, n, nv, argc + 1, full_argv);
    } else {
        if (full_argv != argv_buf) free(full_argv);
        rt_error(in, n, "%s 类型没有方法 '%s'", type_name(obj), mname);
    }

    if (full_argv != argv_buf) free(full_argv);
    return r;
}

static Value eval_call(Interp *in, const Node *n) {
    int argc = n->as.call.args.count;
    Value argv_buf[16];
    Value *argv = (argc + 2 <= 16) ? argv_buf : malloc(sizeof(Value) * (size_t)(argc + 2));

    /* 方法调用形式: obj.method(args...) */
    if (n->as.call.callee->kind == N_MEMBER) {
        const Node *mem = n->as.call.callee;
        const char *mname = mem->as.member.name;
        Value obj = eval(in, mem->as.member.obj);
        gc_push_root(in, obj);
        for (int i = 0; i < argc; i++) {
            argv[i] = eval(in, n->as.call.args.items[i]);
            gc_push_root(in, argv[i]);
        }
        Value r = interp_invoke_method(in, n, obj, mname, argc, argv);
        gc_pop_roots(in, argc + 1);
        if (argv != argv_buf) free(argv);
        return r;
    }

    Value callee = eval(in, n->as.call.callee);
    gc_push_root(in, callee);
    for (int i = 0; i < argc; i++) {
        argv[i] = eval(in, n->as.call.args.items[i]);
        gc_push_root(in, argv[i]);
    }
    Value r = call_value(in, n, callee, argc, argv);
    gc_pop_roots(in, argc + 1);
    if (argv != argv_buf) free(argv);
    return r;
}

static Value eval(Interp *in, const Node *n) {
    switch (n->kind) {
    case N_NUMBER: return val_num(n->as.number);
    case N_INT:    return val_int((int32_t)n->as.ival);
    case N_STRING: return val_str(str_new(in, n->as.str, strlen(n->as.str)));
    case N_BOOL:   return val_bool(n->as.boolean);
    case N_NIL:    return val_nil();

    case N_IDENT: {
        Value *v = env_find(in->env, n->as.str);
        if (!v) rt_error(in, n, "未定义的变量 '%s'", n->as.str);
        return *v;
    }
    case N_UNARY: {
        Value v = eval(in, n->as.binary.left);
        if (n->as.binary.op == T_NOT) return val_bool(!is_truthy(v));
        if (n->as.binary.op == T_TILDE) return val_int(~need_int(in, n, v, "一元 '~'"));
        if (v.type == V_INT) return val_int(wrap32(-(int64_t)v.as.i));
        return val_num(-need_num(in, n, v, "一元 '-'"));
    }
    case N_BINARY: return eval_binary(in, n);
    case N_TERNARY: {
        Value c = eval(in, n->as.ternary.cond);
        if (is_truthy(c)) {
            return eval(in, n->as.ternary.then_expr);
        } else {
            return eval(in, n->as.ternary.else_expr);
        }
    }
    case N_ASSIGN: return eval_assign(in, n);
    case N_CALL:   return eval_call(in, n);
    case N_INDEX:
        return index_get(in, n, eval(in, n->as.index.obj), eval(in, n->as.index.index));
    case N_MEMBER: {
        Value obj = eval(in, n->as.member.obj);
        gc_push_root(in, obj);
        Str *key = str_new(in, n->as.member.name, strlen(n->as.member.name));
        Value res = index_get(in, n, obj, val_str(key));
        gc_pop_roots(in, 1);
        return res;
    }
    case N_FN:     return make_closure(in, n);

    case N_ARRAY: {
        Array *a = array_new(in);
        Value aval = val_array(a);
        gc_push_root(in, aval);
        for (int i = 0; i < n->as.list.count; i++) {
            Value item = eval(in, n->as.list.items[i]);
            array_push(a, item);
        }
        gc_pop_roots(in, 1);
        return aval;
    }
    case N_MAP: {
        Map *m = map_new(in);
        Value mval = val_map(m);
        gc_push_root(in, mval);
        for (int i = 0; i < n->as.map.keys.count; i++) {
            Str *k = str_new(in, n->as.map.keys.items[i], strlen(n->as.map.keys.items[i]));
            gc_push_root(in, val_str(k));
            Value v = eval(in, n->as.map.values.items[i]);
            map_set(m, k, v);
            gc_pop_roots(in, 1);
        }
        gc_pop_roots(in, 1);
        return mval;
    }
    default:
        rt_error(in, n, "内部错误:无法对该节点求值");
    }
}

/* ================= 语句 ================= */

/* 进入新作用域执行语句列表 */
static Signal exec_scoped(Interp *in, const NodeList *l) {
    Env *saved = in->env;
    Env *e = env_new(in, saved);
    in->env = e;
    in->call_frames[in->frame_count++] = saved;
    Signal s = exec_list(in, l);
    in->frame_count--;
    in->env = saved;
    return s;
}

static Signal exec_for(Interp *in, const Node *n) {
    Env *saved = in->env;
    Env *e = env_new(in, saved);        /* for 头部的变量放在独立作用域 */
    in->env = e;
    in->call_frames[in->frame_count++] = saved;
    Signal result = SIG_NORMAL;

    if (n->as.for_stmt.init) exec(in, n->as.for_stmt.init);
    for (;;) {
        if (n->as.for_stmt.cond && !is_truthy(eval(in, n->as.for_stmt.cond))) break;
        Signal s = exec(in, n->as.for_stmt.body);
        if (s == SIG_BREAK) break;
        if (s == SIG_RETURN) { result = s; break; }
        /* SIG_CONTINUE 与正常结束一样,继续执行 step */
        if (n->as.for_stmt.step) eval(in, n->as.for_stmt.step);
    }

    in->frame_count--;
    in->env = saved;
    return result;
}

static Signal exec_try(Interp *in, const Node *n) {
    TryFrame tf;
    tf.depth = in->depth;
    tf.frame_count = in->frame_count;
    tf.temp_root_count = in->temp_root_count;
    tf.env = in->env;
    tf.current_file = in->current_file;
    tf.current_fn = in->current_fn;
    tf.prev = in->try_top;
    in->try_top = &tf;

    if (setjmp(tf.jb) == 0) {
        Signal s = exec_scoped(in, &n->as.try_stmt.try_b->as.list);
        in->try_top = tf.prev;
        return s;
    }

    /* 发生异常或运行时错误: 恢复栈状态并执行 catch 块 */
    in->try_top = tf.prev;
    in->depth = tf.depth;
    in->frame_count = tf.frame_count;
    in->temp_root_count = tf.temp_root_count;
    in->env = tf.env;
    in->current_file = tf.current_file;
    in->current_fn = tf.current_fn;

    Env *saved = in->env;
    Env *catch_env = env_new(in, saved);
    if (n->as.try_stmt.err_var) {
        env_define(catch_env, n->as.try_stmt.err_var, in->last_error);
    }
    in->env = catch_env;
    in->call_frames[in->frame_count++] = saved;
    Signal s = exec_list(in, &n->as.try_stmt.catch_b->as.list);
    in->frame_count--;
    in->env = saved;
    return s;
}

static Signal exec_throw(Interp *in, const Node *n) {
    Value v = eval(in, n->as.expr);
    gc_push_root(in, v);
    if (v.type == V_MAP) {
        Value *mp = map_get(v.as.map, "message");
        const char *msg = (mp && mp->type == V_STR) ? val_get_cstr(*mp) : "用户抛出异常";
        raise_error(in, n, "未捕获的异常", "ThrowError", msg, v);
    } else if (v.type == V_STR) {
        raise_error(in, n, "未捕获的异常", "ThrowError", val_get_cstr(v), val_nil());
    } else {
        char *s = value_to_string(v, 0);
        char buf[512];
        snprintf(buf, sizeof buf, "%s", s);
        free(s);
        raise_error(in, n, "未捕获的异常", "ThrowError", buf, val_nil());
    }
}

static Signal exec(Interp *in, const Node *n) {
    switch (n->kind) {
    case N_LET:
        env_define_ex(in, n, in->env, n->as.let.name,
                      n->as.let.init ? eval(in, n->as.let.init) : val_nil(),
                      n->as.let.is_const);
        return SIG_NORMAL;
    case N_FN:  /* 具名函数声明 */
        env_define(in->env, n->as.fn.name, make_closure(in, n));
        return SIG_NORMAL;
    case N_EXPR_STMT:
        eval(in, n->as.expr);
        return SIG_NORMAL;
    case N_IF:
        if (is_truthy(eval(in, n->as.if_stmt.cond)))
            return exec(in, n->as.if_stmt.then_b);
        if (n->as.if_stmt.else_b) return exec(in, n->as.if_stmt.else_b);
        return SIG_NORMAL;
    case N_WHILE:
        while (is_truthy(eval(in, n->as.while_stmt.cond))) {
            Signal s = exec(in, n->as.while_stmt.body);
            if (s == SIG_BREAK) break;
            if (s == SIG_RETURN) return s;
        }
        return SIG_NORMAL;
    case N_FOR:      return exec_for(in, n);
    case N_IMPORT:   return exec_import(in, n);
    case N_TRY:      return exec_try(in, n);
    case N_THROW:    return exec_throw(in, n);
    case N_RETURN:
        in->ret = n->as.expr ? eval(in, n->as.expr) : val_nil();
        return SIG_RETURN;
    case N_BREAK:    return SIG_BREAK;
    case N_CONTINUE: return SIG_CONTINUE;
    case N_BLOCK:    return exec_scoped(in, &n->as.list);
    default:
        rt_error(in, n, "内部错误:无法执行该节点");
    }
}

#include "parser.h"
#include <ctype.h>

#ifdef _WIN32
__declspec(dllimport) unsigned long __stdcall GetFullPathNameA(const char *lpFileName, unsigned long nBufferLength, char *lpBuffer, char **lpFilePart);
#else
#include <limits.h>
#include <unistd.h>
#endif

char *read_file_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

#if L_PLATFORM_ESP_IDF
#define L_PATH_MAX 256
#else
#define L_PATH_MAX 4096
#endif

/* 解析相对路径或绝对路径为规范化绝对路径 */
static char *resolve_module_path(const char *base_file, const char *import_path) {
    char combined[L_PATH_MAX];
    char resolved[L_PATH_MAX];

    /* 检查 import_path 是否已自带 .l 后缀,若无则自动补上 */
    char with_ext[L_PATH_MAX];
    size_t iplen = strlen(import_path);
    if (iplen < 2 || strcmp(import_path + iplen - 2, ".l") != 0) {
        snprintf(with_ext, sizeof with_ext, "%s.l", import_path);
    } else {
        snprintf(with_ext, sizeof with_ext, "%s", import_path);
    }

    /* 如果是相对路径且有基准文件, 则以基准文件所在目录拼接 */
    int is_rel = 1;
#ifdef _WIN32
    if ((isalpha((unsigned char)with_ext[0]) && with_ext[1] == ':') || with_ext[0] == '\\' || with_ext[0] == '/') {
        is_rel = 0;
    }
#else
    if (with_ext[0] == '/') {
        is_rel = 0;
    }
#endif

    if (is_rel && base_file && *base_file) {
        char dir[L_PATH_MAX];
        snprintf(dir, sizeof dir, "%s", base_file);
        char *last_slash = strrchr(dir, '/');
        char *last_bslash = strrchr(dir, '\\');
        char *slash = last_slash > last_bslash ? last_slash : last_bslash;
        if (slash) {
            *(slash + 1) = '\0';
            snprintf(combined, sizeof combined, "%.*s%.*s",
                     (int)(sizeof(combined) / 2 - 1), dir,
                     (int)(sizeof(combined) / 2 - 1), with_ext);
        } else {
            snprintf(combined, sizeof combined, "./%.*s", (int)(sizeof(combined) - 4), with_ext);
        }
    } else {
        snprintf(combined, sizeof combined, "%s", with_ext);
    }

#if defined(_WIN32)
    unsigned long len = GetFullPathNameA(combined, sizeof resolved, resolved, NULL);
    if (len == 0 || len >= (unsigned long)sizeof resolved) {
        return strdup(combined);
    }
    /* 将 Windows 路径里的 '\' 统一转成 '/' 方便比对 */
    for (char *p = resolved; *p; p++) if (*p == '\\') *p = '/';
    return strdup(resolved);
#elif L_PLATFORM_ESP_IDF
    (void)resolved;
    return strdup(combined);
#else
    if (realpath(combined, resolved)) {
        return strdup(resolved);
    }
    return strdup(combined);
#endif
}

/* 从路径中推导默认模块名: 如 "./utils/math.l" -> "math" */
static char *derive_module_name(const char *path) {
    const char *last_slash = strrchr(path, '/');
    const char *last_bslash = strrchr(path, '\\');
    const char *start = last_slash > last_bslash ? last_slash : last_bslash;
    if (start) start++; else start = path;

    char *name = strdup(start);
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
    return name;
}

/* 执行 import 语句 */
static Signal exec_import(Interp *in, const Node *n) {
    const char *raw_path = n->as.import_stmt.path;
    /* 内置硬件模块: 无需文件系统即可 import "esp32" / "gpio" / "hw" / "machine" */
    if (strcmp(raw_path, "esp32") == 0 || strcmp(raw_path, "gpio") == 0 ||
        strcmp(raw_path, "hw") == 0 || strcmp(raw_path, "machine") == 0) {
        Value mod_val = hw_build_module(in);
        const char *bname = n->as.import_stmt.alias ? n->as.import_stmt.alias : raw_path;
        env_define(in->env, strdup(bname), mod_val);
        return SIG_NORMAL;
    }
    char *full_path = resolve_module_path(in->current_file, raw_path);

    /* 1. 检查模块是否已经加载在缓存中 */
    Value *cached = map_get(in->modules, full_path);
    Value mod_map_val;
    if (cached) {
        mod_map_val = *cached;
    } else {
        /* 2. 读文件内容 */
        char *src = read_file_text(full_path);
        if (!src) {
            char path_buf[1024];
            snprintf(path_buf, sizeof path_buf, "%s", full_path ? full_path : "");
            free(full_path);
            rt_error(in, n, "无法加载模块文件: \"%s\" (解析路径: %s)", raw_path, path_buf);
        }

        /* 3. 解析模块文件 AST (保留 src 与 persistent_path, 供运行时报错定位使用) */
        int had_err = 0;
        char *persistent_path = strdup(full_path);
        Node *mod_ast = parse_program_file(src, persistent_path, &had_err);
        if (had_err || !mod_ast) {
            free(src);
            free(persistent_path);
            free(full_path);
            rt_error(in, n, "模块文件语法错误: \"%s\"", raw_path);
        }

        /* 4. 创建模块独立全局环境并执行 */
        Env *mod_env = env_new(in, in->globals);
        const char *old_file = in->current_file;
        Env *old_env = in->env;

        in->current_file = persistent_path;
        in->env = mod_env;
        in->call_frames[in->frame_count++] = old_env;

        Signal sig = exec_list(in, &mod_ast->as.list);

        in->frame_count--;
        in->env = old_env;
        in->current_file = old_file;

        if (sig == SIG_BREAK || sig == SIG_CONTINUE) {
            free(full_path);
            rt_error(in, n, "模块顶级作用域中出现非法的 break/continue");
        }

        /* 5. 搜集模块顶层定义为 Map 对象 (导出模块命名空间) */
        Map *mod_map = map_new(in);
        mod_map_val = val_map(mod_map);
        gc_push_root(in, mod_map_val);

        for (int i = 0; i < mod_env->count; i++) {
            Str *k = str_new(in, mod_env->names[i], strlen(mod_env->names[i]));
            map_set(mod_map, k, mod_env->vals[i]);
        }

        /* 6. 存入模块缓存 */
        Str *path_key = str_new(in, full_path, strlen(full_path));
        map_set(in->modules, path_key, mod_map_val);
        gc_pop_roots(in, 1);
    }

    /* 7. 在当前环境中绑定变量: import "..." as alias; 或 import "..."; */
    char *bind_name = NULL;
    if (n->as.import_stmt.alias) {
        bind_name = strdup(n->as.import_stmt.alias);
    } else {
        bind_name = derive_module_name(raw_path);
    }

    /* 必须持久化保存 bind_name，因为 Env 的 names 只存指针 */
    env_define(in->env, strdup(bind_name), mod_map_val);
    free(bind_name);
    free(full_path);
    return SIG_NORMAL;
}

void interp_init(Interp *in) {
    memset(in, 0, sizeof *in);
    in->next_gc = L_GC_INIT_THRESHOLD;
    in->globals = env_new(in, NULL);
    in->env = in->globals;
    in->ret = val_nil();
    in->last_error = val_nil();
    in->try_top = NULL;
    in->current_fn = "<main>";
    in->modules = map_new(in);      /* 初始化模块缓存 */
    builtins_install(in);
}

Value interp_eval_source(Interp *in, const Node *call_site, const char *src, const char *virtual_file) {
    int had_err = 0;
    const char *vfile = virtual_file ? virtual_file : "<eval>";
    Node *prog = parse_program_file(src, vfile, &had_err);
    if (had_err || !prog) {
        rt_error(in, call_site, "脚本语法错误 (%s)", vfile);
    }
    const char *old_file = in->current_file;
    in->current_file = vfile;
    Value last = val_nil();
    for (int i = 0; i < prog->as.list.count; i++) {
        const Node *stmt = prog->as.list.items[i];
        if (stmt->kind == N_EXPR_STMT) {
            last = eval(in, stmt->as.expr);
        } else {
            Signal s = exec(in, stmt);
            if (s == SIG_RETURN) {
                last = in->ret;
                break;
            }
            if (s != SIG_NORMAL) {
                in->current_file = old_file;
                rt_error(in, stmt, "break / continue 只能出现在循环中");
            }
        }
    }
    in->current_file = old_file;
    return last;
}

void interp_destroy(Interp *in) {
    /* 强制全部清除 */
    in->globals = NULL;
    in->env = NULL;
    in->modules = NULL;
    in->ret = val_nil();
    in->last_error = val_nil();
    in->try_top = NULL;
    in->temp_root_count = 0;
    gc_collect(in);
}

int interp_run(Interp *in, const Node *program, int echo) {
    in->env = in->globals;
    in->depth = 0;
    in->frame_count = 0;
    in->temp_root_count = 0;
    in->try_top = NULL;
    in->current_fn = "<main>";
    if (setjmp(in->jb)) {           /* 运行时错误会跳回这里 */
        in->env = in->globals;
        in->depth = 0;
        in->frame_count = 0;
        in->temp_root_count = 0;
        in->try_top = NULL;
        in->current_fn = "<main>";
        return 1;
    }
    for (int i = 0; i < program->as.list.count; i++) {
        const Node *stmt = program->as.list.items[i];
        if (echo && stmt->kind == N_EXPR_STMT) {
            Value v = eval(in, stmt->as.expr);
            if (v.type != V_NIL) {
                char *s = value_to_string(v, 1);
                printf("%s\n", s);
                free(s);
            }
            continue;
        }
        Signal s = exec(in, stmt);
        if (s == SIG_RETURN)
            rt_error(in, stmt, "return 只能出现在函数中");
        if (s != SIG_NORMAL)
            rt_error(in, stmt, "break / continue 只能出现在循环中");
    }
    return 0;
}

int interp_run_vm(Interp *in, const Node *program, int echo) {
    in->env = in->globals;
    Proto *proto = compile_to_proto(in, program, in->current_file, echo);
    VM vm;
    vm_init(&vm, in);
    int res = vm_run_proto(&vm, proto, echo);
    vm_free(&vm);
    return res;
}
