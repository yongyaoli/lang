#include "value.h"
#include "bytecode.h"
#include "interp.h"
#include "l_heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- 构造 ---------- */

Value val_nil(void) { Value v; memset(&v, 0, sizeof v); v.type = V_NIL; return v; }
Value val_bool(int b) { Value v = val_nil(); v.type = V_BOOL; v.as.boolean = b != 0; return v; }
Value val_num(double n) { Value v = val_nil(); v.type = V_NUM; v.as.num = n; return v; }
Value val_int(int32_t n) { Value v = val_nil(); v.type = V_INT; v.as.i = n; return v; }
int val_is_number(Value v) { return v.type == V_INT || v.type == V_NUM; }
/* 取整结果: 值是整数且在 int32 范围内返回 int, 否则保持 float (floor/ceil/round 用) */
Value val_whole(double d) {
    if (d == (double)(int32_t)d && d >= -2147483648.0 && d <= 2147483647.0) return val_int((int32_t)d);
    return val_num(d);
}
double val_to_double(Value v) { return v.type == V_INT ? (double)v.as.i : v.as.num; }
Value val_str(Str *s) { Value v = val_nil(); v.type = V_STR; v.as.str = s; return v; }
Value val_bytes(Bytes *b) { Value v = val_nil(); v.type = V_BYTES; v.as.bytes = b; return v; }
Value val_array(Array *a) { Value v = val_nil(); v.type = V_ARRAY; v.as.array = a; return v; }
Value val_map(Map *m) { Value v = val_nil(); v.type = V_MAP; v.as.map = m; return v; }
Value val_obj(NativeObj *o) { Value v = val_nil(); v.type = V_OBJ; v.as.obj = o; return v; }

const char *val_get_cstr(Value v) {
    if (v.type == V_STR && v.as.str) {
        return v.as.str->chars;
    }
    return "";
}

/* ---------- 对象分配 ---------- */

Str *str_new(Interp *in, const char *chars, size_t len) {
    Str *s = (Str *)gc_alloc(in, sizeof(Str) + len, OBJ_STR);
    s->len = len;
    if (chars) {
        memcpy(s->chars, chars, len);
    }
    s->chars[len] = '\0';
    return s;
}

Bytes *bytes_new(Interp *in, size_t len, const uint8_t *init) {
    Bytes *b = (Bytes *)gc_alloc(in, sizeof(Bytes) + len, OBJ_BYTES);
    b->len = len;
    if (init) {
        memcpy(b->data, init, len);
    } else if (len > 0) {
        memset(b->data, 0, len);
    }
    return b;
}

Array *array_new(Interp *in) {
    Array *a = (Array *)gc_alloc(in, sizeof(Array), OBJ_ARRAY);
    a->items = NULL;
    a->count = 0;
    a->cap = 0;
    return a;
}

Map *map_new(Interp *in) {
    Map *m = (Map *)gc_alloc(in, sizeof(Map), OBJ_MAP);
    m->keys = NULL;
    m->vals = NULL;
    m->count = 0;
    m->cap = 0;
    return m;
}

Func *func_new(Interp *in, const Node *decl, Env *closure) {
    Func *f = (Func *)gc_alloc(in, sizeof(Func), OBJ_FN);
    f->decl = decl;
    f->proto = NULL;
    f->closure = closure;
    f->upvalues = NULL;
    f->upvalue_count = 0;
    return f;
}

Func *func_proto_new(Interp *in, Proto *proto) {
    Func *f = (Func *)gc_alloc(in, sizeof(Func), OBJ_FN);
    f->decl = NULL;
    f->proto = proto;
    f->closure = NULL;
    f->upvalues = NULL;
    f->upvalue_count = proto ? proto->upvalue_count : 0;
    if (f->upvalue_count > 0) {
        f->upvalues = (Upvalue **)l_heap_alloc(f->upvalue_count * sizeof(Upvalue *));
        if (f->upvalues) {
            memset(f->upvalues, 0, f->upvalue_count * sizeof(Upvalue *));
        }
    }
    return f;
}

NativeObj *obj_new(Interp *in, const ObjClass *cls) {
    NativeObj *o = (NativeObj *)gc_alloc(in, sizeof(NativeObj), OBJ_NATIVE);
    o->cls = cls;
    memset(o->fields, 0, sizeof(o->fields));
    o->extra = NULL;
    return o;
}

/* ---------- 数组 / Map ---------- */

void array_push(Array *a, Value v) {
    if (a->count == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 4;
        a->items = (Value *)gc_realloc(NULL, a->items, sizeof(Value) * (size_t)a->cap);
    }
    a->items[a->count++] = v;
}

Value *map_get(const Map *m, const char *key) {
    for (int i = 0; i < m->count; i++) {
        if (m->keys[i] && strcmp(m->keys[i]->chars, key) == 0) {
            return &m->vals[i];
        }
    }
    return NULL;
}

void map_set(Map *m, Str *key, Value v) {
    Value *slot = map_get(m, key->chars);
    if (slot) {
        *slot = v;
        return;
    }
    if (m->count == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 4;
        m->keys = (Str **)gc_realloc(NULL, m->keys, sizeof(Str *) * (size_t)m->cap);
        m->vals = (Value *)gc_realloc(NULL, m->vals, sizeof(Value) * (size_t)m->cap);
    }
    m->keys[m->count] = key;
    m->vals[m->count] = v;
    m->count++;
}

int map_remove(Map *m, const char *key) {
    for (int i = 0; i < m->count; i++) {
        if (m->keys[i] && strcmp(m->keys[i]->chars, key) == 0) {
            for (int j = i; j + 1 < m->count; j++) {
                m->keys[j] = m->keys[j + 1];
                m->vals[j] = m->vals[j + 1];
            }
            m->count--;
            return 1;
        }
    }
    return 0;
}

Value val_fn(Func *f) {
    Value v;
    v.type = V_FN;
    v.as.fn = f;
    return v;
}

void val_print(Value v) {
    char *s = value_to_string(v, 1);
    fputs(s, stdout);
    free(s);
}

/* ---------- 语义 ---------- */

int is_truthy(Value v) {
    if (v.type == V_NIL) return 0;
    if (v.type == V_BOOL) return v.as.boolean;
    return 1;
}

int values_equal(Value a, Value b) {
    /* int 与 float 按数值比较: 1 == 1.0 */
    if (a.type == V_INT && b.type == V_NUM) return (double)a.as.i == b.as.num;
    if (a.type == V_NUM && b.type == V_INT) return a.as.num == (double)b.as.i;
    if (a.type != b.type) return 0;
    switch (a.type) {
    case V_NIL:    return 1;
    case V_BOOL:   return a.as.boolean == b.as.boolean;
    case V_INT:    return a.as.i == b.as.i;
    case V_NUM:    return a.as.num == b.as.num;
    case V_STR:
        if (a.as.str == b.as.str) return 1;
        return strcmp(a.as.str->chars, b.as.str->chars) == 0;
    case V_BYTES:
        if (a.as.bytes == b.as.bytes) return 1;
        if (!a.as.bytes || !b.as.bytes || a.as.bytes->len != b.as.bytes->len) return 0;
        return memcmp(a.as.bytes->data, b.as.bytes->data, a.as.bytes->len) == 0;
    case V_ARRAY:  return a.as.array == b.as.array;    /* 引用比较 */
    case V_MAP:    return a.as.map == b.as.map;
    case V_FN:     return a.as.fn == b.as.fn;
    case V_NATIVE: return a.as.native == b.as.native;
    case V_OBJ:    return a.as.obj == b.as.obj;
    }
    return 0;
}

const char *type_name(Value v) {
    switch (v.type) {
    case V_NIL: return "nil";
    case V_BOOL: return "bool";
    case V_INT: return "int";
    case V_NUM: return "float";
    case V_STR: return "string";
    case V_BYTES: return "bytes";
    case V_ARRAY: return "array";
    case V_MAP: return "map";
    case V_FN: case V_NATIVE: return "function";
    case V_OBJ: return (v.as.obj && v.as.obj->cls) ? v.as.obj->cls->name : "object";
    }
    return "?";
}

/* ---------- 转文本 ---------- */

typedef struct { char *buf; size_t len, cap; } SB;

static void sb_add(SB *sb, const char *s) {
    size_t n = strlen(s);
    if (sb->len + n + 1 > sb->cap) {
        sb->cap = (sb->len + n + 1) * 2;
        sb->buf = realloc(sb->buf, sb->cap);
    }
    memcpy(sb->buf + sb->len, s, n + 1);
    sb->len += n;
}

#define MAX_PRINT_DEPTH 16   /* 防止自引用的数组/map 无限递归 */

static void write_value(SB *sb, Value v, int quoted, int depth) {
    char tmp[64];
    switch (v.type) {
    case V_NIL:  sb_add(sb, "nil"); break;
    case V_BOOL: sb_add(sb, v.as.boolean ? "true" : "false"); break;
    case V_INT:  snprintf(tmp, sizeof tmp, "%ld", (long)v.as.i); sb_add(sb, tmp); break;
    case V_NUM:
        snprintf(tmp, sizeof tmp, "%.14g", v.as.num);
        /* 浮点数若看起来像整数, 补 ".0" 以便和 int 区分 (inf/nan/科学计数法除外) */
        if (strpbrk(tmp, ".eEnNiI") == NULL) strcat(tmp, ".0");
        sb_add(sb, tmp);
        break;
    case V_STR:
        if (quoted) {
            sb_add(sb, "\"");
            sb_add(sb, v.as.str ? v.as.str->chars : "");
            sb_add(sb, "\"");
        } else {
            sb_add(sb, v.as.str ? v.as.str->chars : "");
        }
        break;
    case V_BYTES: {
        size_t n = v.as.bytes ? v.as.bytes->len : 0;
        snprintf(tmp, sizeof tmp, "<bytes(%zu)", n);
        sb_add(sb, tmp);
        size_t show = n > 16 ? 16 : n;
        for (size_t i = 0; i < show; i++) {
            snprintf(tmp, sizeof tmp, " %02X", v.as.bytes->data[i]);
            sb_add(sb, tmp);
        }
        if (n > show) sb_add(sb, " ...");
        sb_add(sb, ">");
        break;
    }
    case V_ARRAY:
        if (depth > MAX_PRINT_DEPTH) { sb_add(sb, "[...]"); break; }
        sb_add(sb, "[");
        for (int i = 0; i < v.as.array->count; i++) {
            if (i) sb_add(sb, ", ");
            write_value(sb, v.as.array->items[i], 1, depth + 1);
        }
        sb_add(sb, "]");
        break;
    case V_MAP:
        if (depth > MAX_PRINT_DEPTH) { sb_add(sb, "{...}"); break; }
        sb_add(sb, "{");
        for (int i = 0; i < v.as.map->count; i++) {
            if (i) sb_add(sb, ", ");
            sb_add(sb, v.as.map->keys[i] ? v.as.map->keys[i]->chars : "");
            sb_add(sb, ": ");
            write_value(sb, v.as.map->vals[i], 1, depth + 1);
        }
        sb_add(sb, "}");
        break;
    case V_FN:
        sb_add(sb, "<fn ");
        if (v.as.fn->decl && v.as.fn->decl->as.fn.name) {
            sb_add(sb, v.as.fn->decl->as.fn.name);
        } else if (v.as.fn->proto && v.as.fn->proto->name) {
            sb_add(sb, v.as.fn->proto->name);
        } else {
            sb_add(sb, "anonymous");
        }
        sb_add(sb, ">");
        break;
    case V_NATIVE:
        sb_add(sb, "<native ");
        sb_add(sb, v.as.native->name);
        sb_add(sb, ">");
        break;
    case V_OBJ:
        if (v.as.obj && v.as.obj->cls && v.as.obj->cls->format) {
            char obuf[128];
            v.as.obj->cls->format(v.as.obj, obuf, sizeof obuf);
            sb_add(sb, obuf);
        } else {
            sb_add(sb, "<");
            sb_add(sb, type_name(v));
            sb_add(sb, ">");
        }
        break;
    }
}

char *value_to_string(Value v, int quoted) {
    SB sb = {0};
    sb_add(&sb, "");            /* 保证 buf 非 NULL */
    write_value(&sb, v, quoted, 0);
    return sb.buf;
}
