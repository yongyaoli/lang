/*
 * L 语言嵌入式统一虚拟文件系统实现 (FS / File 标准库, M13)
 *
 * 核心目标:
 * 1. 统一 VFS 路径解析: PC 宿主透明映射本地磁盘与 ./flash 目录，
 *    ESP32 嵌入式环境映射 LittleFS 分区 (掉电保护、磨损均衡，单句柄 <512B)；
 * 2. 全局静态快捷 API: FS.exists / read / write / append / remove / list / mkdir / info；
 * 3. 大文件流式句柄: File.open (read, readline, write, flush, seek, tell, eof, close)；
 * 4. 纯 C99 零外部依赖，与 l_heap、GC 根扫描无缝集成。
 */

#include "l_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#include <io.h>
#include <direct.h>
#define access _access
#define F_OK   0
#define mkdir(p) _mkdir(p)
#define rmdir    _rmdir

/* 避免引入 windows.h 导致与 lexer.h 的 TokenType 枚举冲突 */
#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllimport) int __stdcall GetDiskFreeSpaceExA(
    const char *lpDirectoryName,
    unsigned long long *lpFreeBytesAvailableToCaller,
    unsigned long long *lpTotalNumberOfBytes,
    unsigned long long *lpTotalNumberOfFreeBytes);
#ifdef __cplusplus
}
#endif

#else
#include <unistd.h>
#endif

#if defined(_MSC_VER)
#include "dirent_msvc.h"
#else
#include <dirent.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <sys/statvfs.h>
#endif

#include "interp.h"
#include "l_fs.h"
#include "l_heap.h"

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

/* 递归创建多级目录 (如 ./flash/data/logs) */
static int vfs_mkdir_p(const char *dir) {
    if (!dir || !*dir) return 0;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    size_t len = strlen(tmp);
    if (len == 0) return 0;
    if (tmp[len - 1] == '/' || tmp[len - 1] == '\\') {
        tmp[len - 1] = '\0';
        len--;
    }

    /* 逐层检查并创建 */
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char sep = *p;
            *p = '\0';
            /* 跳过 Windows 盘符如 "D:" */
            if (!(p - tmp == 2 && tmp[1] == ':')) {
#if defined(_WIN32)
                _mkdir(tmp);
#else
                mkdir(tmp, 0777);
#endif
            }
            *p = sep;
        }
    }
#if defined(_WIN32)
    return _mkdir(tmp);
#else
    return mkdir(tmp, 0777);
#endif
}

/* 确保指定文件路径的父目录已存在 */
static void vfs_ensure_parent_dir(const char *filepath) {
    if (!filepath) return;
    char parent[512];
    snprintf(parent, sizeof(parent), "%s", filepath);
    char *last_slash = strrchr(parent, '/');
    char *last_bslash = strrchr(parent, '\\');
    char *sep = last_slash > last_bslash ? last_slash : last_bslash;
    if (sep) {
        *sep = '\0';
        vfs_mkdir_p(parent);
    }
}

/* 统一路径映射函数 */
const char *l_vfs_resolve_path(const char *path, char *out_buf, size_t cap) {
    if (!path || !out_buf || cap == 0) return "";

#if L_PLATFORM_HOST
    /* PC 宿主环境: 将 "/flash/..." 自动映射至当前工作区目录下的 "./flash/..." */
    if (strncmp(path, "/flash/", 7) == 0) {
        snprintf(out_buf, cap, "./flash/%s", path + 7);
        return out_buf;
    } else if (strcmp(path, "/flash") == 0) {
        snprintf(out_buf, cap, "./flash");
        return out_buf;
    } else if (strncmp(path, "\\flash\\", 7) == 0) {
        snprintf(out_buf, cap, ".\\flash\\%s", path + 7);
        return out_buf;
    }
    /* 普通相对路径或系统绝对路径原样直通 */
    snprintf(out_buf, cap, "%s", path);
    return out_buf;
#else
    /* ESP32 嵌入式环境: 若未加 /flash 前缀且不是绝对路径，自动路由到 /flash/ 分区 */
    if (path[0] != '/') {
        snprintf(out_buf, cap, "/flash/%s", path);
    } else {
        snprintf(out_buf, cap, "%s", path);
    }
    return out_buf;
#endif
}

/* ================= 1. FS 静态方法实现 ================= */

/* FS.exists(path): 检查文件或目录是否存在 */
static Value m_fs_exists(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self; (void)argc;
    need_arg(in, at, "FS.exists", argv, 0, V_STR, "string");
    char real_path[512];
    l_vfs_resolve_path(val_get_cstr(argv[0]), real_path, sizeof(real_path));
    return val_bool(access(real_path, F_OK) == 0);
}

/* FS.read(path): 读取整个文件为文本字符串 */
static Value m_fs_read(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self; (void)argc;
    need_arg(in, at, "FS.read", argv, 0, V_STR, "string");
    const char *orig_path = val_get_cstr(argv[0]);
    char real_path[512];
    l_vfs_resolve_path(orig_path, real_path, sizeof(real_path));

    FILE *f = fopen(real_path, "rb");
    if (!f) {
        rt_error(in, at, "FS.read() 无法打开文件 \"%s\" (物理路径: %s)", orig_path, real_path);
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0) {
        fclose(f);
        rt_error(in, at, "FS.read() 获取文件大小失败 \"%s\"", orig_path);
    }

    Str *s = str_new(in, NULL, (size_t)sz);
    size_t rd = fread(s->chars, 1, (size_t)sz, f);
    fclose(f);
    s->chars[rd] = '\0';
    s->len = rd;

    return val_str(s);
}

/* FS.write(path, content): 覆盖写入文件内容 */
static Value m_fs_write(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    need_arg(in, at, "FS.write", argv, 0, V_STR, "string");
    if (argc < 2) rt_error(in, at, "FS.write() 缺少写入内容参数");

    const char *data_ptr = "";
    size_t data_len = 0;
    if (argv[1].type == V_STR) {
        data_ptr = val_get_cstr(argv[1]);
        data_len = argv[1].as.str ? argv[1].as.str->len : 0;
    } else if (argv[1].type == V_BYTES) {
        data_ptr = (const char *)argv[1].as.bytes->data;
        data_len = argv[1].as.bytes->len;
    } else {
        rt_error(in, at, "FS.write() 内容必须是 string 或 bytes, 实际是 %s", type_name(argv[1]));
    }

    const char *orig_path = val_get_cstr(argv[0]);
    char real_path[512];
    l_vfs_resolve_path(orig_path, real_path, sizeof(real_path));
    vfs_ensure_parent_dir(real_path);

    FILE *f = fopen(real_path, "wb");
    if (!f) {
        rt_error(in, at, "FS.write() 无法创建或打开写入文件 \"%s\" (物理路径: %s)", orig_path, real_path);
    }

    size_t written = 0;
    if (data_len > 0) {
        written = fwrite(data_ptr, 1, data_len, f);
    }
    fclose(f);
    return val_int((int32_t)written);
}

/* FS.append(path, content): 追加写入文件内容 */
static Value m_fs_append(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    need_arg(in, at, "FS.append", argv, 0, V_STR, "string");
    if (argc < 2) rt_error(in, at, "FS.append() 缺少追加内容参数");

    const char *data_ptr = "";
    size_t data_len = 0;
    if (argv[1].type == V_STR) {
        data_ptr = val_get_cstr(argv[1]);
        data_len = argv[1].as.str ? argv[1].as.str->len : 0;
    } else if (argv[1].type == V_BYTES) {
        data_ptr = (const char *)argv[1].as.bytes->data;
        data_len = argv[1].as.bytes->len;
    } else {
        rt_error(in, at, "FS.append() 内容必须是 string 或 bytes, 实际是 %s", type_name(argv[1]));
    }

    const char *orig_path = val_get_cstr(argv[0]);
    char real_path[512];
    l_vfs_resolve_path(orig_path, real_path, sizeof(real_path));
    vfs_ensure_parent_dir(real_path);

    FILE *f = fopen(real_path, "ab");
    if (!f) {
        rt_error(in, at, "FS.append() 无法以追加模式打开文件 \"%s\" (物理路径: %s)", orig_path, real_path);
    }

    size_t written = 0;
    if (data_len > 0) {
        written = fwrite(data_ptr, 1, data_len, f);
    }
    fclose(f);
    return val_int((int32_t)written);
}

/* FS.remove(path): 删除文件或空目录 */
static Value m_fs_remove(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self; (void)argc;
    need_arg(in, at, "FS.remove", argv, 0, V_STR, "string");
    char real_path[512];
    l_vfs_resolve_path(val_get_cstr(argv[0]), real_path, sizeof(real_path));

    /* 若不存在直接返回 false */
    if (access(real_path, F_OK) != 0) {
        return val_bool(0);
    }

    int ret = remove(real_path);
    if (ret != 0) {
        /* 尝试作为目录删除 */
        ret = rmdir(real_path);
    }
    return val_bool(ret == 0);
}

/* 字符串数组升序排列比较回调 */
static int vfs_cmp_cstr(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* FS.list([dir_path="/flash"]): 列出目录下所有条目 */
static Value m_fs_list(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)self;
    const char *orig_path = "/flash";
    if (argc > 0 && argv[0].type == V_STR) {
        orig_path = val_get_cstr(argv[0]);
    }

    char real_path[512];
    l_vfs_resolve_path(orig_path, real_path, sizeof(real_path));

    DIR *d = opendir(real_path);
    if (!d) {
        /* 若目录不存在返回空数组 [] */
        Array *empty_arr = array_new(in);
        return val_array(empty_arr);
    }

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

    if (count > 1) {
        qsort(names, count, sizeof(char *), vfs_cmp_cstr);
    }

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

/* FS.mkdir(dir_path): 递归创建目录 */
static Value m_fs_mkdir(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self; (void)argc;
    need_arg(in, at, "FS.mkdir", argv, 0, V_STR, "string");
    char real_path[512];
    l_vfs_resolve_path(val_get_cstr(argv[0]), real_path, sizeof(real_path));
    vfs_mkdir_p(real_path);
    return val_bool(1);
}

/* FS.size(path): 获取文件大小字节数 */
static Value m_fs_size(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self; (void)argc;
    need_arg(in, at, "FS.size", argv, 0, V_STR, "string");
    char real_path[512];
    l_vfs_resolve_path(val_get_cstr(argv[0]), real_path, sizeof(real_path));
    struct stat st;
    if (stat(real_path, &st) != 0) return val_nil();
    return val_whole((double)st.st_size);
}

/* FS.isdir(path): 是否为目录 */
static Value m_fs_isdir(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self; (void)argc;
    need_arg(in, at, "FS.isdir", argv, 0, V_STR, "string");
    char real_path[512];
    l_vfs_resolve_path(val_get_cstr(argv[0]), real_path, sizeof(real_path));
    struct stat st;
    return val_bool(stat(real_path, &st) == 0 && S_ISDIR(st.st_mode));
}

/* FS.rename(old_path, new_path): 重命名或移动文件 */
static Value m_fs_rename(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self; (void)argc;
    need_arg(in, at, "FS.rename", argv, 0, V_STR, "string");
    need_arg(in, at, "FS.rename", argv, 1, V_STR, "string");
    char old_real[512], new_real[512];
    l_vfs_resolve_path(val_get_cstr(argv[0]), old_real, sizeof(old_real));
    l_vfs_resolve_path(val_get_cstr(argv[1]), new_real, sizeof(new_real));
    vfs_ensure_parent_dir(new_real);
    return val_bool(rename(old_real, new_real) == 0);
}

/* FS.copy(src, dst): 复制文件 */
static Value m_fs_copy(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self; (void)argc;
    need_arg(in, at, "FS.copy", argv, 0, V_STR, "string");
    need_arg(in, at, "FS.copy", argv, 1, V_STR, "string");
    char src_real[512], dst_real[512];
    l_vfs_resolve_path(val_get_cstr(argv[0]), src_real, sizeof(src_real));
    l_vfs_resolve_path(val_get_cstr(argv[1]), dst_real, sizeof(dst_real));
    vfs_ensure_parent_dir(dst_real);

    FILE *src = fopen(src_real, "rb");
    if (!src) return val_bool(0);
    FILE *dst = fopen(dst_real, "wb");
    if (!dst) { fclose(src); return val_bool(0); }

    char buf[4096];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof buf, src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) { ok = 0; break; }
    }
    fclose(src);
    fclose(dst);
    return val_bool(ok);
}

/* FS.info(): 查询文件系统存储容量信息 */
static Value m_fs_info(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)self; (void)argc; (void)argv;
    double total = 1441792.0; /* 默认 1.4 MB (对应 ESP32 LittleFS 分区标准大小) */
    double free_b = 1048576.0;
    double used = total - free_b;
    const char *fs_type = "Host-VFS";

#if L_PLATFORM_HOST
#if defined(_WIN32)
    unsigned long long free_bytes_avail = 0, total_bytes = 0, total_free = 0;
    if (GetDiskFreeSpaceExA(NULL, &free_bytes_avail, &total_bytes, &total_free)) {
        total = (double)total_bytes;
        free_b = (double)free_bytes_avail;
        used = total - free_b;
    }
#elif defined(__unix__) || defined(__APPLE__)
    struct statvfs sv;
    if (statvfs(".", &sv) == 0) {
        total = (double)sv.f_blocks * sv.f_frsize;
        free_b = (double)sv.f_bavail * sv.f_frsize;
        used = total - free_b;
    }
#endif
#else
    fs_type = "LittleFS";
#endif

    Map *m = map_new(in);
    map_set(m, str_new(in, "total_bytes", 11), val_whole(total));
    map_set(m, str_new(in, "used_bytes", 10), val_whole(used));
    map_set(m, str_new(in, "free_bytes", 10), val_whole(free_b));
    map_set(m, str_new(in, "fs_type", 7), val_str(str_new(in, fs_type, strlen(fs_type))));

    return val_map(m);
}

static const ObjMethod fs_singleton_methods[] = {
    {"exists", 1, 1, m_fs_exists, "FS.exists(path)",             "检查文件或目录是否存在 (bool)"},
    {"read",   1, 1, m_fs_read,   "FS.read(path)",               "读取整个文件内容返回字符串, 失败抛出明确异常"},
    {"write",  2, 2, m_fs_write,  "FS.write(path, content)",     "覆盖写入文件内容, 自动创建多级父目录"},
    {"append", 2, 2, m_fs_append, "FS.append(path, content)",    "追加写入文件内容, 自动创建多级父目录"},
    {"remove", 1, 1, m_fs_remove, "FS.remove(path)",             "删除文件或空目录, 成功返回 true"},
    {"list",   0, 1, m_fs_list,   "FS.list([dir_path=\"/flash\"])", "列出目录下所有条目名称数组 (已排序)"},
    {"mkdir",  1, 1, m_fs_mkdir,  "FS.mkdir(dir_path)",          "递归创建多级目录"},
    {"size",   1, 1, m_fs_size,   "FS.size(path)",               "获取文件大小字节数, 失败返回 nil"},
    {"isdir",  1, 1, m_fs_isdir,  "FS.isdir(path)",              "判断是否为目录 (bool)"},
    {"rename", 2, 2, m_fs_rename, "FS.rename(old, new)",         "重命名或移动文件"},
    {"copy",   2, 2, m_fs_copy,   "FS.copy(src, dst)",           "复制文件"},
    {"info",   0, 0, m_fs_info,   "FS.info()",                   "返回文件系统存储容量信息看板字典"},
};

static void fs_singleton_format(const NativeObj *obj, char *buf, size_t size) {
    (void)obj;
    snprintf(buf, size, "<singleton: FS (Virtual File System)>");
}

static const ObjClass fs_singleton_class = {
    "FS",
    fs_singleton_methods,
    sizeof fs_singleton_methods / sizeof fs_singleton_methods[0],
    fs_singleton_format,
    NULL
};

/* ================= 2. File 流式文件句柄实现 ================= */

typedef struct {
    FILE *fp;
    char path[256];
    char mode[16];
    int closed;
} FileHandle;

static FileHandle *file_handle(NativeObj *o) {
    return (FileHandle *)o->extra;
}

static void file_obj_destroy(NativeObj *o) {
    if (!o || !o->extra) return;
    FileHandle *h = file_handle(o);
    if (!h->closed && h->fp) {
        fclose(h->fp);
        h->fp = NULL;
        h->closed = 1;
    }
    free(h);
    o->extra = NULL;
}

static void file_format(const NativeObj *obj, char *buf, size_t size) {
    FileHandle *h = (FileHandle *)obj->extra;
    if (!h) {
        snprintf(buf, size, "<File: destroyed>");
        return;
    }
    snprintf(buf, size, "<File: path=\"%s\", mode=\"%s\", %s>",
             h->path, h->mode, h->closed ? "closed" : "open");
}

/* file.read([size=-1]) */
static Value m_file_read(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    FileHandle *h = file_handle(self.as.obj);
    if (h->closed || !h->fp) rt_error(in, at, "File.read() 失败: 文件句柄已关闭");

    long want_size = -1;
    if (argc > 0 && argv[0].type == V_INT) {
        want_size = (long)argv[0].as.i;
    }

    if (want_size > 0) {
        /* 读取指定最大字节数 */
        Str *s = str_new(in, NULL, (size_t)want_size);
        size_t n = fread(s->chars, 1, (size_t)want_size, h->fp);
        s->chars[n] = '\0';
        s->len = n;
        return val_str(s);
    }

    /* 读取直到文件末尾 */
    size_t cap = 1024, len = 0;
    char *buf = malloc(cap);
    if (!buf) rt_error(in, at, "File.read() 内存不足");

    char temp[1024];
    size_t nr;
    while ((nr = fread(temp, 1, sizeof(temp), h->fp)) > 0) {
        if (len + nr >= cap) {
            cap = (len + nr) * 2;
            buf = realloc(buf, cap);
            if (!buf) rt_error(in, at, "File.read() 扩容失败");
        }
        memcpy(buf + len, temp, nr);
        len += nr;
    }

    Str *res = str_new(in, buf, len);
    free(buf);
    return val_str(res);
}

/* file.readline(): 逐行读取一行文本 (到达 EOF 返回 nil) */
static Value m_file_readline(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc; (void)argv;
    FileHandle *h = file_handle(self.as.obj);
    if (h->closed || !h->fp) rt_error(in, at, "File.readline() 失败: 文件句柄已关闭");

    size_t cap = 256, len = 0;
    char *buf = malloc(cap);
    if (!buf) rt_error(in, at, "File.readline() 内存不足");

    int ch;
    while ((ch = fgetc(h->fp)) != EOF) {
        if (len + 2 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) rt_error(in, at, "File.readline() 扩容失败");
        }
        buf[len++] = (char)ch;
        if (ch == '\n') break;
    }

    if (len == 0 && ch == EOF) {
        free(buf);
        return val_nil(); /* 到达 EOF 且未读到任何字符 */
    }

    buf[len] = '\0';
    Str *s = str_new(in, buf, len);
    free(buf);
    return val_str(s);
}

/* file.write(data): 写入字符串或字节数组 */
static Value m_file_write(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    FileHandle *h = file_handle(self.as.obj);
    if (h->closed || !h->fp) rt_error(in, at, "File.write() 失败: 文件句柄已关闭");
    if (argc < 1) rt_error(in, at, "File.write() 缺少写入内容参数");

    const char *data_ptr = "";
    size_t data_len = 0;
    if (argv[0].type == V_STR) {
        data_ptr = val_get_cstr(argv[0]);
        data_len = argv[0].as.str ? argv[0].as.str->len : 0;
    } else if (argv[0].type == V_BYTES) {
        data_ptr = (const char *)argv[0].as.bytes->data;
        data_len = argv[0].as.bytes->len;
    } else {
        rt_error(in, at, "File.write() 写入内容必须是 string 或 bytes, 实际是 %s", type_name(argv[0]));
    }

    size_t written = 0;
    if (data_len > 0) {
        written = fwrite(data_ptr, 1, data_len, h->fp);
    }
    return val_int((int32_t)written);
}

/* file.flush(): 刷新写缓冲区落盘 */
static Value m_file_flush(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc; (void)argv;
    FileHandle *h = file_handle(self.as.obj);
    if (h->closed || !h->fp) rt_error(in, at, "File.flush() 失败: 文件句柄已关闭");
    fflush(h->fp);
    return val_bool(1);
}

/* file.seek(offset, [whence=0]): 定位文件偏移 (whence: 0=头部, 1=当前, 2=末尾) */
static Value m_file_seek(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    FileHandle *h = file_handle(self.as.obj);
    if (h->closed || !h->fp) rt_error(in, at, "File.seek() 失败: 文件句柄已关闭");
    long offset = (argc > 0 && argv[0].type == V_INT) ? (long)argv[0].as.i : 0;
    int whence_arg = (argc > 1 && argv[1].type == V_INT) ? (int)argv[1].as.i : 0;

    int whence = SEEK_SET;
    if (whence_arg == 1) whence = SEEK_CUR;
    else if (whence_arg == 2) whence = SEEK_END;

    return val_bool(fseek(h->fp, offset, whence) == 0);
}

/* file.tell(): 获取当前文件位置偏移 */
static Value m_file_tell(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc; (void)argv;
    FileHandle *h = file_handle(self.as.obj);
    if (h->closed || !h->fp) rt_error(in, at, "File.tell() 失败: 文件句柄已关闭");
    long pos = ftell(h->fp);
    return val_whole((double)pos);
}

/* file.eof(): 查询是否到达文件末尾 */
static Value m_file_eof(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    FileHandle *h = file_handle(self.as.obj);
    if (h->closed || !h->fp) return val_bool(1);
    return val_bool(feof(h->fp) != 0);
}

/* file.close(): 关闭文件句柄 */
static Value m_file_close(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    FileHandle *h = file_handle(self.as.obj);
    if (!h->closed && h->fp) {
        fclose(h->fp);
        h->fp = NULL;
        h->closed = 1;
    }
    return val_bool(1);
}

static const ObjMethod file_methods[] = {
    {"read",     0, 1, m_file_read,     "file.read([size=-1])",            "读取指定大小或剩余全部内容为字符串"},
    {"readline", 0, 0, m_file_readline, "file.readline()",                 "逐行读取文本, 到达 EOF 返回 nil"},
    {"write",    1, 1, m_file_write,    "file.write(data)",                "写入字符串或 bytes, 返回已写入字节数"},
    {"flush",    0, 0, m_file_flush,    "file.flush()",                    "将文件写缓冲区强制刷新落盘"},
    {"seek",     1, 2, m_file_seek,     "file.seek(offset, [whence=0])",   "定位文件指针 (0=起始, 1=当前, 2=末尾)"},
    {"tell",     0, 0, m_file_tell,     "file.tell()",                     "获取当前文件指针字节偏移量"},
    {"eof",      0, 0, m_file_eof,      "file.eof()",                      "判断是否已读到文件末尾 (bool)"},
    {"close",    0, 0, m_file_close,    "file.close()",                    "主动关闭文件句柄并释放底层资源"},
};

static const ObjClass file_class = {
    "File",
    file_methods,
    sizeof file_methods / sizeof file_methods[0],
    file_format,
    file_obj_destroy
};

/* File.open(path, [mode="r"]): 打开文件并返回 File 原生句柄对象 */
static Value f_file_open(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "File.open", argv, 0, V_STR, "string");
    const char *orig_path = val_get_cstr(argv[0]);
    const char *mode = "r";
    if (argc > 1 && argv[1].type == V_STR) {
        mode = val_get_cstr(argv[1]);
    }

    char real_path[512];
    l_vfs_resolve_path(orig_path, real_path, sizeof(real_path));

    /* 若是以写或追加模式打开，确保多级父目录存在 */
    if (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+')) {
        vfs_ensure_parent_dir(real_path);
    }

    FILE *fp = fopen(real_path, mode);
    if (!fp) {
        rt_error(in, at, "File.open() 无法以模式 \"%s\" 打开文件 \"%s\" (物理路径: %s)",
                 mode, orig_path, real_path);
    }

    NativeObj *o = obj_new(in, &file_class);
    o->extra = malloc(sizeof(FileHandle));
    if (!o->extra) {
        fclose(fp);
        rt_error(in, at, "File.open() 内存不足, 无法创建文件句柄对象");
    }

    FileHandle *h = file_handle(o);
    memset(h, 0, sizeof(*h));
    h->fp = fp;
    h->closed = 0;
    snprintf(h->path, sizeof(h->path), "%s", orig_path);
    snprintf(h->mode, sizeof(h->mode), "%s", mode);

    return val_obj(o);
}

/* File 原生函数包装 (支持 File.open 与 File(path, mode) 构造) */
static const Native file_open_native = {
    "open", 1, 2, f_file_open,
    "File.open(path, [mode=\"r\"])", "打开文件并返回流式 File 句柄对象"
};

/* ================= 3. 全局与模块注册 ================= */

void fs_install_globals(Interp *in) {
    /* 1. 注册全局静态单例对象 FS */
    NativeObj *fso = obj_new(in, &fs_singleton_class);
    Value fsv = val_obj(fso);
    env_define(in->globals, "FS", fsv);
    env_define(in->globals, "fs", fsv);

    /* 2. 注册 File 类与构造器/方法 */
    Map *file_mod = map_new(in);
    Value f_open_val = val_nil();
    f_open_val.type = V_NATIVE;
    f_open_val.as.native = &file_open_native;
    map_set(file_mod, str_new(in, "open", 4), f_open_val);

    env_define(in->globals, "File", val_map(file_mod));
}

Value fs_build_module(Interp *in) {
    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);

    /* 包含 FS 单例 */
    NativeObj *fso = obj_new(in, &fs_singleton_class);
    Value fsv = val_obj(fso);
    map_set(m, str_new(in, "FS", 2), fsv);
    map_set(m, str_new(in, "fs", 2), fsv);

    /* 包含 File 类 */
    Map *file_mod = map_new(in);
    Value f_open_val = val_nil();
    f_open_val.type = V_NATIVE;
    f_open_val.as.native = &file_open_native;
    map_set(file_mod, str_new(in, "open", 4), f_open_val);
    map_set(m, str_new(in, "File", 4), val_map(file_mod));

    /* 快捷函数别名导出 */
    map_set(m, str_new(in, "open", 4), f_open_val);

    gc_pop_roots(in, 1);
    return mv;
}
