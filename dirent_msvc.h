/* 极简 dirent 兼容层: 仅为 MSVC (无 POSIX dirent.h) 提供 opendir/readdir/closedir。
 * 基于 <io.h> 的 _findfirst/_findnext 实现, 刻意不包含 <windows.h>
 * (windows.h 中的枚举名会与 lexer.h 的 TokenType 冲突)。
 * MinGW/GCC 与 ESP-IDF 不使用本文件, 直接包含系统 dirent.h。 */
#ifndef L_DIRENT_MSVC_H
#define L_DIRENT_MSVC_H

#include <io.h>
#include <string.h>
#include <stdlib.h>

typedef struct dirent {
    char d_name[260];
} dirent;

typedef struct DIR {
    intptr_t          handle;   /* _findfirst 句柄, -1 表示无效 */
    struct _finddata_t data;
    dirent            cur;
    int               has_data;
} DIR;

static DIR *opendir(const char *path) {
    char pattern[1024];
    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) return NULL;
    int n = snprintf(pattern, sizeof pattern, "%s\\*", path);
    if (n <= 0 || n >= (int)sizeof pattern) { free(d); return NULL; }
    d->handle = _findfirst(pattern, &d->data);
    if (d->handle == -1) { free(d); return NULL; }
    d->has_data = 1;
    return d;
}

static struct dirent *readdir(DIR *d) {
    if (!d || d->handle == -1 || !d->has_data) return NULL;
    size_t n = strlen(d->data.name);
    if (n >= sizeof d->cur.d_name) n = sizeof d->cur.d_name - 1;
    memcpy(d->cur.d_name, d->data.name, n);
    d->cur.d_name[n] = '\0';
    if (_findnext(d->handle, &d->data) != 0) d->has_data = 0;
    return &d->cur;
}

static void closedir(DIR *d) {
    if (!d) return;
    if (d->handle != -1) _findclose(d->handle);
    free(d);
}

#endif /* L_DIRENT_MSVC_H */
