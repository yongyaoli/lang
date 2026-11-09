/* 垃圾回收器: 标记-清除 (Mark-Sweep) GC */
#include "gc.h"
#include "interp.h"
#include "value.h"
#include "bytecode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GC_INITIAL_THRESHOLD (1024 * 64)   /* 初始触发阈值: 64KB */

/* 前向声明 */
static void mark_object(Interp *in, GcObj *obj);
static void mark_value(Interp *in, Value v);
static void mark_env(Interp *in, Env *e);

void gc_push_root(Interp *in, Value v) {
    if (in->temp_root_count < (int)(sizeof(in->temp_roots) / sizeof(in->temp_roots[0]))) {
        in->temp_roots[in->temp_root_count++] = v;
    }
}

void gc_pop_roots(Interp *in, int count) {
    in->temp_root_count -= count;
    if (in->temp_root_count < 0) {
        in->temp_root_count = 0;
    }
}

void *gc_alloc(Interp *in, size_t size, ObjType type) {
    if (in->bytes_allocated + size > in->next_gc && !in->gc_disabled) {
        gc_collect(in);
    }

    GcObj *obj = (GcObj *)malloc(size);
    if (!obj) {
        /* 内存紧张时尝试强制回收一次 */
        gc_collect(in);
        obj = (GcObj *)malloc(size);
        if (!obj) {
            fprintf(stderr, "严重错误: 内存分配失败 (Out of memory)\n");
            exit(1);
        }
    }

    obj->type = type;
    obj->marked = 0;
    obj->next = in->all_objects;
    in->all_objects = obj;

    in->bytes_allocated += size;
    return obj;
}

/* ---------- 标记阶段 (Mark) ---------- */

static void mark_value(Interp *in, Value v) {
    switch (v.type) {
    case V_STR:
        if (v.as.str) mark_object(in, (GcObj *)v.as.str);
        break;
    case V_BYTES:
        if (v.as.bytes) mark_object(in, (GcObj *)v.as.bytes);
        break;
    case V_ARRAY:
        if (v.as.array) mark_object(in, (GcObj *)v.as.array);
        break;
    case V_MAP:
        if (v.as.map) mark_object(in, (GcObj *)v.as.map);
        break;
    case V_FN:
        if (v.as.fn) mark_object(in, (GcObj *)v.as.fn);
        break;
    case V_OBJ:
        if (v.as.obj) mark_object(in, (GcObj *)v.as.obj);
        break;
    default:
        break;
    }
}

static void mark_env(Interp *in, Env *e) {
    while (e) {
        GcObj *obj = (GcObj *)e;
        if (obj->marked) break;
        obj->marked = 1;
        for (int i = 0; i < e->count; i++) {
            mark_value(in, e->vals[i]);
        }
        e = e->parent;
    }
}

static void mark_object(Interp *in, GcObj *obj) {
    if (!obj || obj->marked) return;
    obj->marked = 1;

    switch (obj->type) {
    case OBJ_STR:
    case OBJ_BYTES:
        /* 字符串与字节缓冲是叶子节点，无引出指针 */
        break;
    case OBJ_ARRAY: {
        Array *a = (Array *)obj;
        for (int i = 0; i < a->count; i++) {
            mark_value(in, a->items[i]);
        }
        break;
    }
    case OBJ_MAP: {
        Map *m = (Map *)obj;
        for (int i = 0; i < m->count; i++) {
            if (m->keys[i]) mark_object(in, (GcObj *)m->keys[i]);
            mark_value(in, m->vals[i]);
        }
        break;
    }
    case OBJ_FN: {
        Func *f = (Func *)obj;
        if (f->closure) {
            mark_env(in, f->closure);
        }
        if (f->proto) {
            mark_object(in, (GcObj *)f->proto);
        }
        for (int i = 0; i < f->upvalue_count; i++) {
            if (f->upvalues[i]) mark_object(in, (GcObj *)f->upvalues[i]);
        }
        break;
    }
    case OBJ_PROTO: {
        Proto *p = (Proto *)obj;
        for (int i = 0; i < p->chunk.const_count; i++) {
            mark_value(in, p->chunk.constants[i]);
        }
        break;
    }
    case OBJ_UPVALUE: {
        Upvalue *up = (Upvalue *)obj;
        if (up->location) {
            mark_value(in, *up->location);
        }
        mark_value(in, up->closed);
        break;
    }
    case OBJ_ENV:
        mark_env(in, (Env *)obj);
        break;
    case OBJ_NATIVE:
        break;
    }
}

static void mark_roots(Interp *in) {
    /* 1. 全局环境和当前环境 */
    if (in->globals) mark_env(in, in->globals);
    if (in->env && in->env != in->globals) mark_env(in, in->env);

    /* 1.1 模块导入缓存 */
    if (in->modules) mark_object(in, (GcObj *)in->modules);

    /* 2. 调用栈/嵌套块上所有挂起的父环境 (极关键: 否则递归中的父环境会被回收) */
    for (int i = 0; i < in->frame_count; i++) {
        if (in->call_frames[i]) mark_env(in, in->call_frames[i]);
    }

    /* 3. return 与异常正在暂存的值 */
    mark_value(in, in->ret);
    mark_value(in, in->last_error);

    /* 4. C 函数计算过程中的临时根保护栈 */
    for (int i = 0; i < in->temp_root_count; i++) {
        mark_value(in, in->temp_roots[i]);
    }
}

/* ---------- 清除阶段 (Sweep) ---------- */

static void free_object(Interp *in, GcObj *obj) {
    switch (obj->type) {
    case OBJ_STR:
        in->bytes_allocated -= sizeof(Str) + ((Str *)obj)->len;
        break;
    case OBJ_BYTES:
        in->bytes_allocated -= sizeof(Bytes) + ((Bytes *)obj)->len;
        break;
    case OBJ_ARRAY: {
        Array *a = (Array *)obj;
        free(a->items);
        in->bytes_allocated -= sizeof(Array);
        break;
    }
    case OBJ_MAP: {
        Map *m = (Map *)obj;
        free(m->keys);
        free(m->vals);
        in->bytes_allocated -= sizeof(Map);
        break;
    }
    case OBJ_FN: {
        Func *f = (Func *)obj;
        free(f->upvalues);
        in->bytes_allocated -= sizeof(Func);
        break;
    }
    case OBJ_PROTO: {
        Proto *p = (Proto *)obj;
        free((void *)p->name);
        free((void *)p->source_file);
        chunk_free(&p->chunk);
        free(p->upvalues);
        in->bytes_allocated -= sizeof(Proto);
        break;
    }
    case OBJ_UPVALUE:
        in->bytes_allocated -= sizeof(Upvalue);
        break;
    case OBJ_ENV: {
        Env *e = (Env *)obj;
        free(e->names);
        free(e->vals);
        free(e->consts);
        in->bytes_allocated -= sizeof(Env);
        break;
    }
    case OBJ_NATIVE: {
        NativeObj *o = (NativeObj *)obj;
        if (o->cls && o->cls->destroy) o->cls->destroy(o);
        in->bytes_allocated -= sizeof(NativeObj);
        break;
    }
    }
    free(obj);
}

void gc_collect(Interp *in) {
    /* 1. 标记所有根 */
    mark_roots(in);

    /* 2. 扫描堆链表，释放未被标记的对象，复位已标记对象 */
    GcObj **p = &in->all_objects;
    while (*p) {
        GcObj *curr = *p;
        if (!curr->marked) {
            *p = curr->next;
            free_object(in, curr);
        } else {
            curr->marked = 0;   /* 为下一轮 GC 复位 */
            p = &curr->next;
        }
    }

    /* 3. 动态调整下一次 GC 阈值: 当前存活字节的 2 倍 */
    in->next_gc = in->bytes_allocated * 2;
    if (in->next_gc < GC_INITIAL_THRESHOLD) {
        in->next_gc = GC_INITIAL_THRESHOLD;
    }
}
