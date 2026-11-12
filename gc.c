/* 垃圾回收器: 标记-清除 (Mark-Sweep) GC + 固定静态堆分配器 (M7) */
#include "gc.h"
#include "interp.h"
#include "value.h"
#include "bytecode.h"
#include "l_heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GC_INITIAL_THRESHOLD (1024 * 64)   /* 初始触发阈值: 64KB */
#define L_HEAP_GC_HEADROOM (4 * 1024)      /* 静态池剩余水位线: 低于此值提前回收 */

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
    /* 静态池模式: 按池实际剩余空间提前触发回收 (内部缓冲扩容不计入
     * bytes_allocated, 仅靠对象字节阈值会低估池压力导致耗尽/碎片化) */
    if (!in->gc_disabled && l_heap_capacity() != 0) {
        size_t headroom = l_heap_capacity() / 16;   /* 6% 水位线, 4KB~16KB */
        if (headroom < L_HEAP_GC_HEADROOM) headroom = L_HEAP_GC_HEADROOM;
        if (headroom > 16 * 1024) headroom = 16 * 1024;
        if (l_heap_free_bytes() < size + headroom) {
            if (getenv("L_GC_DEBUG")) {
                fprintf(stderr, "[GC-TRIGGER] size=%zu free=%zu\n",
                        size, l_heap_free_bytes());
            }
            gc_collect(in);
        }
    }

    GcObj *obj = (GcObj *)l_heap_alloc(size);
    if (!obj) {
        /* 内存紧张时尝试强制回收一次 */
        gc_collect(in);
        obj = (GcObj *)l_heap_alloc(size);
        if (!obj) {
            fprintf(stderr, "严重错误: 内存分配失败 (Out of memory, 堆模式: %s, 池剩余: %u 字节)\n",
                    l_heap_mode(), (unsigned)l_heap_free_bytes());
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

/* 带回收重试的内部缓冲扩容 (数组/Map/Env 表等) */
void *gc_realloc(Interp *in, void *p, size_t n) {
    void *np = l_heap_realloc(p, n);
    if (np) return np;
    if (!in) {
        fprintf(stderr, "严重错误: 内存扩容失败 (Out of memory, 请求 %zu 字节, 池剩余 %u)\n",
                n, (unsigned)l_heap_free_bytes());
        l_heap_debug_dump();
        exit(1);
    }
    if (!in->gc_disabled) gc_collect(in);
    np = l_heap_realloc(p, n);
    if (!np) {
        fprintf(stderr, "严重错误: 内存扩容失败 (Out of memory, 堆模式: %s, 池剩余: %u 字节)\n",
                l_heap_mode(), (unsigned)l_heap_free_bytes());
        exit(1);
    }
    return np;
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

    /* 5. VM 值栈整体扫描 (M7 值栈 GC 根扫描):
     * 字节码执行期间栈上所有中间值 (操作数 / 调用参数 / 局部槽位) 自动为根,
     * 修复了 GC 在 VM 执行中触发时误回收栈上临时值的隐患 */
    if (in->scan_stack && in->scan_stack_top) {
        Value *top = *in->scan_stack_top;
        if (top > in->scan_stack) {
            for (Value *p = in->scan_stack; p < top; p++) {
                mark_value(in, *p);
            }
        }
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
        l_heap_free(a->items);
        in->bytes_allocated -= sizeof(Array);
        break;
    }
    case OBJ_MAP: {
        Map *m = (Map *)obj;
        l_heap_free(m->keys);
        l_heap_free(m->vals);
        in->bytes_allocated -= sizeof(Map);
        break;
    }
    case OBJ_FN: {
        Func *f = (Func *)obj;
        l_heap_free(f->upvalues);
        in->bytes_allocated -= sizeof(Func);
        break;
    }
    case OBJ_PROTO: {
        Proto *p = (Proto *)obj;
        l_heap_free((void *)p->name);
        l_heap_free((void *)p->source_file);
        chunk_free(&p->chunk);
        l_heap_free(p->upvalues);
        in->bytes_allocated -= sizeof(Proto);
        break;
    }
    case OBJ_UPVALUE:
        in->bytes_allocated -= sizeof(Upvalue);
        break;
    case OBJ_ENV: {
        Env *e = (Env *)obj;
        l_heap_free(e->names);
        l_heap_free(e->vals);
        l_heap_free(e->consts);
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
    l_heap_free(obj);
}

void gc_collect(Interp *in) {
    size_t before = in->bytes_allocated;
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
    if (getenv("L_GC_DEBUG")) {
        fprintf(stderr, "[GC] tracked %zu -> %zu, pool used=%zu free=%zu, objects live\n",
                before, in->bytes_allocated, l_heap_used_bytes(), l_heap_free_bytes());
    }
}
