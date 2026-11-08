#ifndef L_INTERP_H
#define L_INTERP_H

#include <setjmp.h>

#include "l_config.h"
#include "ast.h"
#include "value.h"

#if defined(__GNUC__)
#define L_NORETURN __attribute__((noreturn))
#else
#define L_NORETURN
#endif

#define MAX_CALL_DEPTH L_MAX_CALL_DEPTH /* 最大调用深度,防止 C 栈溢出 */

/* 环境:变量表(线性查找)+ 父作用域指针 */
struct Env {
    GcObj gc;               /* 由 GC 统一管理 */
    const char **names;     /* 指向 AST 标识符名 */
    Value *vals;
    uint8_t *consts;        /* 1 表示 const 常量, 禁止再次赋值或重定义 */
    int count, cap;
    Env *parent;
};

/* try/catch 异常捕获栈帧(分配在 C 栈上, 平时不占 Interp 内存) */
typedef struct TryFrame {
    jmp_buf jb;
    int depth;
    int frame_count;
    int temp_root_count;
    Env *env;
    const char *current_file;
    const char *current_fn;
    struct TryFrame *prev;
} TryFrame;

/* 调用栈回溯记录(用于报错定位与异常对象的 stack 字段) */
#define MAX_TRACE_DEPTH 64
typedef struct {
    const char *fn_name;    /* 调用方所在函数名 */
    const Node *call_site;  /* 调用处 AST 节点(含 file, line, col, line_start) */
} TraceFrame;

struct Interp {
    Env *globals;
    Env *env;               /* 当前作用域 */
    Value ret;              /* return 的返回值 */
    Value last_error;       /* 最近一次异常对象(Map: message/type/file/line/col/fn/stack) */
    TryFrame *try_top;      /* 当前激活的 try 捕获帧(NULL 表示未在 try 中) */
    const char *current_fn; /* 当前执行的函数名(默认 "<main>") */
    TraceFrame trace[MAX_TRACE_DEPTH];
    int depth;              /* 当前调用深度 */
    jmp_buf jb;             /* 顶层未捕获运行时错误的恢复点 */

    /* 调用栈帧环境链 (GC 根遍历) */
    Env *call_frames[MAX_CALL_DEPTH + 10];
    int frame_count;

    /* GC 相关 */
    GcObj *all_objects;     /* 所有托管对象的链表头 */
    Value temp_roots[128];  /* 临时根对象保护栈 */
    int temp_root_count;
    size_t bytes_allocated; /* 已分配字节数 */
    size_t next_gc;         /* 触发下一次 GC 的阈值 */
    int gc_disabled;        /* 暂时禁用 GC 的标志 */

    /* 模块导入缓存 (Map: 规范化文件路径 -> 模块导出 Map) */
    Map *modules;
    const char *current_file; /* 当前正在执行的文件绝对/规范路径 */
};

void interp_init(Interp *in);
void interp_destroy(Interp *in);

/* 读取完整文件内容, 返回 malloc 内存, 失败返回 NULL */
char *read_file_text(const char *path);

/* GC 核心接口 */
void gc_push_root(Interp *in, Value v);
void gc_pop_roots(Interp *in, int count);
void gc_collect(Interp *in);
void *gc_alloc(Interp *in, size_t size, ObjType type);

/* 环境接口 */
Env *env_new(Interp *in, Env *parent);

/* 执行一个 N_PROGRAM。echo=1(REPL)时,非 nil 的表达式语句会打印结果。
 * 返回 0 表示成功,1 表示发生运行时错误(已打印到 stderr)。 */
int interp_run(Interp *in, const Node *program, int echo);

/* 在环境中定义变量(已存在则覆盖) */
void env_define(Env *e, const char *name, Value v);

/* 报告运行时错误并中止当前执行;内置函数也用它 */
L_NORETURN void rt_error(Interp *in, const Node *at, const char *fmt, ...);

/* 在 C 函数中调用语言函数 (用户闭包或内置函数) */
Value interp_call(Interp *in, const Node *at, Value callee, int argc, Value *argv);

/* 直接解析并执行一段内存/Flash 映射中的源码字符串, 返回最后一个表达式的值 */
Value interp_eval_source(Interp *in, const Node *call_site, const char *src, const char *virtual_file);

/* 注册内置函数到全局环境(builtins.c) */
void builtins_install(Interp *in);

/* 内置函数分组:每个 .c 文件提供一组,help 按分组展示 */
typedef struct {
    const char *name;       /* 分组名,help("分组名") 使用 */
    const char *title;      /* 中文标题 */
    const Native *items;
    size_t count;
} NativeGroup;

extern const NativeGroup group_core;    /* builtins.c    基础 */
extern const NativeGroup group_string;  /* stdlib/str.c  字符串与数组 */
extern const NativeGroup group_math;    /* stdlib/math.c 数学 */
extern const NativeGroup group_time;    /* stdlib/time.c 日期时间 */
extern const NativeGroup group_io;      /* stdlib/io.c   文件与输入输出 */
extern const NativeGroup group_sys;     /* stdlib/sys.c  系统与运行时 */
extern const NativeGroup group_hw;      /* stdlib/hw.c   ESP32 硬件外设(Pin 等) */
extern const ObjClass bytes_class;      /* stdlib/hw.c   bytes 方法表 */
void hw_install_globals(Interp *in);    /* 注册 ESP32/esp32 基础功能单例对象 */
Value hw_build_module(Interp *in);      /* 构造内置 esp32/gpio/hw/machine 模块字典 */

/* 按名称查找内置函数(builtins.c 实现, 供方法派发与 help 共用) */
const Native * builtins_find_native(const char *name);

/* 命令行中脚本路径及其后的参数 (main.c 设置, sys_args() 读取) */
extern int l_script_argc;
extern char **l_script_argv;

/* 内置函数的参数检查(builtins.c 实现,各标准库文件共用) */
void need_arg(Interp *in, const Node *at, const char *fn, Value *argv, int i,
              ValueType t, const char *tname);
/* 第 i 个参数必须是整数,返回其 int 值 */
int int_arg(Interp *in, const Node *at, const char *fn, Value *argv, int i);
/* 第 i 个参数必须是数字(int 或 float), 返回 double */
double num_arg(Interp *in, const Node *at, const char *fn, Value *argv, int i);

#endif
