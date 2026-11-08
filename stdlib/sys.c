/* 标准库: 系统与运行时交互 (System) */
#include <stddef.h>
#include "../l_config.h"

/* 命令行参数 (由 main.c 设置): 第 0 项是脚本路径 */
int l_script_argc = 0;
char **l_script_argv = NULL;

#if L_WITH_STDLIB_SYS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../interp.h"
#include "../version.h"

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define getcwd _getcwd
#define chdir _chdir
#define getpid _getpid
#else
#include <unistd.h>
#endif

/* sys_platform(): 返回操作系统平台名: "win32", "linux", "darwin" 等 */
static Value f_sys_platform(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
#if defined(_WIN32)
    const char *p = "win32";
#elif defined(__APPLE__)
    const char *p = "darwin";
#elif defined(__linux__)
    const char *p = "linux";
#else
    const char *p = "unknown";
#endif
    return val_str(str_new(in, p, strlen(p)));
}

/* sys_version(): 返回解释器版本号 */
static Value f_sys_version(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    return val_str(str_new(in, L_VERSION, strlen(L_VERSION)));
}

/* sys_getenv(变量名): 获取环境变量值, 不存在返回 nil */
static Value f_sys_getenv(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "sys_getenv", argv, 0, V_STR, "string");
    const char *val = getenv(val_get_cstr(argv[0]));
    if (!val) return val_nil();
    return val_str(str_new(in, val, strlen(val)));
}

/* sys_exec(命令): 执行系统 shell 命令, 返回进程退出码 */
static Value f_sys_exec(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)argc;
    need_arg(in, at, "sys_exec", argv, 0, V_STR, "string");
    fflush(stdout);
    int status = system(val_get_cstr(argv[0]));
    return val_int((int32_t)status);
}

/* sys_cwd(): 获取当前工作目录路径 */
static Value f_sys_cwd(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    char buf[4096];
    if (getcwd(buf, sizeof buf)) {
        return val_str(str_new(in, buf, strlen(buf)));
    }
    return val_nil();
}

/* sys_gc_collect(): 手动触发一次完整垃圾回收 */
static Value f_sys_gc_collect(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    gc_collect(in);
    return val_nil();
}

/* sys_allocated(): 返回当前 GC 托管分配的字节总数 */
static Value f_sys_allocated(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    return val_whole((double)in->bytes_allocated);
}

/* sys_args(): 返回命令行参数数组, [脚本路径, 参数1, ...]; REPL 下为空数组 */
static Value f_sys_args(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    Array *a = array_new(in);
    Value aval = val_array(a);
    gc_push_root(in, aval);
    for (int i = 0; i < l_script_argc; i++) {
        array_push(a, val_str(str_new(in, l_script_argv[i], strlen(l_script_argv[i]))));
    }
    gc_pop_roots(in, 1);
    return aval;
}

/* sys_setenv(变量名, 值): 设置当前进程环境变量 (子进程 sys_exec 可见) */
static Value f_sys_setenv(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "sys_setenv", argv, 0, V_STR, "string");
    need_arg(in, at, "sys_setenv", argv, 1, V_STR, "string");
#ifdef _WIN32
    int rc = _putenv_s(val_get_cstr(argv[0]), val_get_cstr(argv[1]));
#else
    int rc = setenv(val_get_cstr(argv[0]), val_get_cstr(argv[1]), 1);
#endif
    return val_bool(rc == 0);
}

/* sys_chdir(路径): 切换当前工作目录, 成功返回 true */
static Value f_sys_chdir(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "sys_chdir", argv, 0, V_STR, "string");
    return val_bool(chdir(val_get_cstr(argv[0])) == 0);
}

/* sys_pid(): 当前进程号 */
static Value f_sys_pid(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    return val_int((int32_t)getpid());
}

/* sys_script(): 当前正在执行的脚本路径, REPL 下返回 nil */
static Value f_sys_script(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    if (!in->current_file) return val_nil();
    return val_str(str_new(in, in->current_file, strlen(in->current_file)));
}

static const Native sys_natives[] = {
    {"sys_args", 0, 0, f_sys_args, "sys_args()", "命令行参数数组 [脚本路径, 参数...]"},
    {"sys_setenv", 2, 2, f_sys_setenv, "sys_setenv(变量名, 值)", "设置环境变量, 成功返回 true"},
    {"sys_chdir", 1, 1, f_sys_chdir, "sys_chdir(路径)", "切换当前工作目录, 成功返回 true"},
    {"sys_pid", 0, 0, f_sys_pid, "sys_pid()", "当前进程号"},
    {"sys_script", 0, 0, f_sys_script, "sys_script()", "当前脚本路径, REPL 下返回 nil"},
    {"sys_platform", 0, 0, f_sys_platform, "sys_platform()", "返回当前平台 win32 / linux / darwin"},
    {"sys_version", 0, 0, f_sys_version, "sys_version()", "返回解释器版本号"},
    {"sys_getenv", 1, 1, f_sys_getenv, "sys_getenv(变量名)", "获取环境变量, 不存在返回 nil"},
    {"sys_exec", 1, 1, f_sys_exec, "sys_exec(shell命令)", "执行系统命令并返回进程退出状态码"},
    {"sys_cwd", 0, 0, f_sys_cwd, "sys_cwd()", "获取当前工作目录"},
    {"sys_gc_collect", 0, 0, f_sys_gc_collect, "sys_gc_collect()", "强制触发一次垃圾回收"},
    {"sys_allocated", 0, 0, f_sys_allocated, "sys_allocated()", "获取当前堆分配字节数"},
};

const NativeGroup group_sys = {
    "sys", "系统与运行时", sys_natives, sizeof sys_natives / sizeof sys_natives[0]
};

#else
typedef int l_sys_disabled_dummy_t;
#endif /* L_WITH_STDLIB_SYS */
