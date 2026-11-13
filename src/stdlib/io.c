/* 标准库: 输入输出与文件系统 (I/O) */
#include "l_config.h"

#if L_WITH_STDLIB_IO

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER)
/* MSVC 无 POSIX dirent.h, 使用极简兼容层 */
#include "dirent_msvc.h"
#else
#include <dirent.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_MSC_VER)
#define mkdir _mkdir
#define rmdir _rmdir
#endif

#include "interp.h"

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#ifdef _WIN32
#include <io.h>
#define access _access
#define F_OK 0
#else
#include <unistd.h>
#endif

/* io_exists(路径): 检查文件或目录是否存在 */
static Value f_io_exists(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_exists", argv, 0, V_STR, "string");
    const char *path = val_get_cstr(argv[0]);
    return val_bool(access(path, F_OK) == 0);
}

/* io_remove(路径): 删除文件, 成功返回 true, 失败返回 false */
static Value f_io_remove(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_remove", argv, 0, V_STR, "string");
    const char *path = val_get_cstr(argv[0]);
    return val_bool(remove(path) == 0);
}

/* io_append(路径, 内容): 向文件末尾追加写入文本 */
static Value f_io_append(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)argc;
    need_arg(in, at, "io_append", argv, 0, V_STR, "string");
    need_arg(in, at, "io_append", argv, 1, V_STR, "string");
    FILE *f = fopen(val_get_cstr(argv[0]), "ab");
    if (!f) return val_bool(0);
    const char *content = val_get_cstr(argv[1]);
    size_t len = argv[1].as.str ? argv[1].as.str->len : 0;
    int ok = fwrite(content, 1, len, f) == len;
    fclose(f);
    return val_bool(ok);
}

/* io_read_lines(路径): 将文件按行读取为字符串数组, 失败返回 nil */
static Value f_io_read_lines(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_read_lines", argv, 0, V_STR, "string");
    FILE *f = fopen(val_get_cstr(argv[0]), "rb");
    if (!f) return val_nil();

    Array *a = array_new(in);
    Value aval = val_array(a);
    gc_push_root(in, aval);

    char buf[4096];
    while (fgets(buf, sizeof buf, f)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
            buf[--n] = '\0';
        }
        Str *s = str_new(in, buf, n);
        array_push(a, val_str(s));
    }

    fclose(f);
    gc_pop_roots(in, 1);
    return aval;
}

/* io_isdir(路径): 是否为目录 */
static Value f_io_isdir(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_isdir", argv, 0, V_STR, "string");
    struct stat st;
    return val_bool(stat(val_get_cstr(argv[0]), &st) == 0 && S_ISDIR(st.st_mode));
}

/* io_size(路径): 文件字节数, 失败返回 nil */
static Value f_io_size(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_size", argv, 0, V_STR, "string");
    struct stat st;
    if (stat(val_get_cstr(argv[0]), &st) != 0) return val_nil();
    return val_whole((double)st.st_size);
}

/* io_mkdir(路径): 创建单层目录, 成功返回 true */
static Value f_io_mkdir(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_mkdir", argv, 0, V_STR, "string");
#ifdef _WIN32
    return val_bool(mkdir(val_get_cstr(argv[0])) == 0);
#else
    return val_bool(mkdir(val_get_cstr(argv[0]), 0777) == 0);
#endif
}

/* io_rmdir(路径): 删除空目录, 成功返回 true */
static Value f_io_rmdir(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_rmdir", argv, 0, V_STR, "string");
    return val_bool(rmdir(val_get_cstr(argv[0])) == 0);
}

/* io_rename(旧路径, 新路径): 重命名/移动, 成功返回 true */
static Value f_io_rename(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_rename", argv, 0, V_STR, "string");
    need_arg(in, at, "io_rename", argv, 1, V_STR, "string");
    return val_bool(rename(val_get_cstr(argv[0]), val_get_cstr(argv[1])) == 0);
}

/* io_copy(源, 目标): 复制文件, 成功返回 true */
static Value f_io_copy(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_copy", argv, 0, V_STR, "string");
    need_arg(in, at, "io_copy", argv, 1, V_STR, "string");
    FILE *src = fopen(val_get_cstr(argv[0]), "rb");
    if (!src) return val_bool(0);
    FILE *dst = fopen(val_get_cstr(argv[1]), "wb");
    if (!dst) { fclose(src); return val_bool(0); }
    char buf[8192];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof buf, src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) { ok = 0; break; }
    }
    fclose(src);
    fclose(dst);
    return val_bool(ok);
}

/* 字符串数组排序比较 (io_listdir 结果按名称升序, 保证输出稳定) */
static int cmp_cstr(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* io_listdir(路径): 列出目录下的条目名(不含 . 和 ..), 按名称排序; 失败返回 nil */
static Value f_io_listdir(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_listdir", argv, 0, V_STR, "string");
    DIR *d = opendir(val_get_cstr(argv[0]));
    if (!d) return val_nil();

    /* 先收集到 C 数组再排序, 避免持有 GC 对象期间调用不安全函数 */
    char **names = NULL;
    int count = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (count == cap) {
            cap = cap ? cap * 2 : 16;
            names = realloc(names, sizeof(char *) * cap);
        }
        names[count++] = strdup(e->d_name);
    }
    closedir(d);
    if (count > 1) qsort(names, count, sizeof(char *), cmp_cstr);

    Array *a = array_new(in);
    Value aval = val_array(a);
    gc_push_root(in, aval);
    for (int i = 0; i < count; i++) {
        array_push(a, val_str(str_new(in, names[i], strlen(names[i]))));
        free(names[i]);
    }
    free(names);
    gc_pop_roots(in, 1);
    return aval;
}

/* io_join(路径1, 路径2): 用 / 拼接两段路径 (Windows 也接受 /) */
static Value f_io_join(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_join", argv, 0, V_STR, "string");
    need_arg(in, at, "io_join", argv, 1, V_STR, "string");
    const char *a = val_get_cstr(argv[0]), *b = val_get_cstr(argv[1]);
    size_t la = strlen(a), lb = strlen(b);
    int need_sep = la > 0 && a[la - 1] != '/' && a[la - 1] != '\\';
    char *buf = malloc(la + lb + 2);
    memcpy(buf, a, la);
    if (need_sep) buf[la++] = '/';
    memcpy(buf + la, b, lb + 1);
    Value r = val_str(str_new(in, buf, la + lb));
    free(buf);
    return r;
}

/* 返回路径中最后一个分隔符之后的位置 (无分隔符返回整串) */
static const char *base_part(const char *p) {
    const char *s = p, *q;
    for (q = p; *q; q++) if (*q == '/' || *q == '\\') s = q + 1;
    return s;
}

/* io_basename(路径): 文件名部分 */
static Value f_io_basename(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_basename", argv, 0, V_STR, "string");
    const char *b = base_part(val_get_cstr(argv[0]));
    return val_str(str_new(in, b, strlen(b)));
}

/* io_dirname(路径): 目录部分, 无目录返回 "." */
static Value f_io_dirname(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_dirname", argv, 0, V_STR, "string");
    const char *p = val_get_cstr(argv[0]);
    const char *b = base_part(p);
    size_t n = (size_t)(b - p);
    if (n == 0) return val_str(str_new(in, ".", 1));
    if (n > 1) n--;     /* 去掉末尾分隔符 (根目录 "/" 保留) */
    return val_str(str_new(in, p, n));
}

/* io_ext(路径): 扩展名(含点), 无扩展名返回 "" */
static Value f_io_ext(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "io_ext", argv, 0, V_STR, "string");
    const char *b = base_part(val_get_cstr(argv[0]));
    const char *dot = strrchr(b, '.');
    if (!dot || dot == b) return val_str(str_new(in, "", 0));
    return val_str(str_new(in, dot, strlen(dot)));
}

static const Native io_natives[] = {
    {"io_isdir", 1, 1, f_io_isdir, "io_isdir(路径)", "是否为目录"},
    {"io_size", 1, 1, f_io_size, "io_size(路径)", "文件字节数, 失败返回 nil"},
    {"io_mkdir", 1, 1, f_io_mkdir, "io_mkdir(路径)", "创建单层目录, 成功返回 true"},
    {"io_rmdir", 1, 1, f_io_rmdir, "io_rmdir(路径)", "删除空目录, 成功返回 true"},
    {"io_rename", 2, 2, f_io_rename, "io_rename(旧, 新)", "重命名或移动文件, 成功返回 true"},
    {"io_copy", 2, 2, f_io_copy, "io_copy(源, 目标)", "复制文件, 成功返回 true"},
    {"io_listdir", 1, 1, f_io_listdir, "io_listdir(路径)", "列出目录条目(已排序), 失败返回 nil"},
    {"io_join", 2, 2, f_io_join, "io_join(路径1, 路径2)", "拼接路径"},
    {"io_basename", 1, 1, f_io_basename, "io_basename(路径)", "文件名部分"},
    {"io_dirname", 1, 1, f_io_dirname, "io_dirname(路径)", "目录部分, 无目录返回 \".\""},
    {"io_ext", 1, 1, f_io_ext, "io_ext(路径)", "扩展名(含点), 无则返回空串"},
    {"io_exists", 1, 1, f_io_exists, "io_exists(路径)", "检查文件是否存在"},
    {"io_remove", 1, 1, f_io_remove, "io_remove(路径)", "删除文件, 成功返回 true"},
    {"io_append", 2, 2, f_io_append, "io_append(路径, 内容)", "追加写入文件内容"},
    {"io_read_lines", 1, 1, f_io_read_lines, "io_read_lines(路径)", "按行读取文件为字符串数组"},
};

const NativeGroup group_io = {
    "io", "文件输入输出", io_natives, sizeof io_natives / sizeof io_natives[0]
};

#else
typedef int l_io_disabled_dummy_t;
#endif /* L_WITH_STDLIB_IO */
