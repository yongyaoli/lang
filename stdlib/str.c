/* 标准库:字符串与数组(字符串按字节处理,与 len / 下标一致;split("") 与 chr / ord 按 UTF-8 字符处理) */
#include <stdlib.h>
#include <string.h>

#include "../interp.h"

/* ---------- 辅助 ---------- */

typedef struct { char *buf; size_t len, cap; } Buf;

static void buf_add(Buf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->buf = realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

/* UTF-8 首字节对应的字符字节数 */
static size_t utf8_len(unsigned char c) {
    if (c >= 0xF0) return 4;
    if (c >= 0xE0) return 3;
    if (c >= 0xC0) return 2;
    return 1;
}

/* 可选的第 i 个整数参数,缺省返回 def */
static int opt_int(Interp *in, const Node *at, const char *fn, int argc, Value *argv,
                   int i, int def) {
    return argc > i ? int_arg(in, at, fn, argv, i) : def;
}

/* 负数下标从末尾算起,并限制在 [0, n] 内 */
static int norm_index(int i, int n) {
    if (i < 0) i += n;
    if (i < 0) i = 0;
    if (i > n) i = n;
    return i;
}

/* UTF-8 字符计数 */
static size_t utf8_char_count(const char *s) {
    size_t count = 0;
    while (*s) {
        size_t n = utf8_len((unsigned char)*s);
        size_t rem = strlen(s);
        if (n > rem) n = rem;
        s += n;
        count++;
    }
    return count;
}

/* ulen(s): 按 UTF-8 字符计算长度，如 ulen("你好") 为 2 */
static Value f_ulen(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)argc;
    need_arg(in, at, "ulen", argv, 0, V_STR, "string");
    return val_int((int32_t)utf8_char_count(val_get_cstr(argv[0])));
}

/* usubstr(s, 字符起点, [字符长度]): 按 UTF-8 字符切片，支持负数起点 */
static Value f_usubstr(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "usubstr", argv, 0, V_STR, "string");
    const char *s = val_get_cstr(argv[0]);
    int total_chars = (int)utf8_char_count(s);
    int start = norm_index(int_arg(in, at, "usubstr", argv, 1), total_chars);
    int len = opt_int(in, at, "usubstr", argc, argv, 2, total_chars - start);
    if (len < 0) len = 0;
    if (len > total_chars - start) len = total_chars - start;

    /* 寻找字节起点 */
    const char *p = s;
    for (int i = 0; i < start && *p; i++) {
        p += utf8_len((unsigned char)*p);
    }
    const char *start_ptr = p;
    /* 寻找字节终点 */
    for (int i = 0; i < len && *p; i++) {
        p += utf8_len((unsigned char)*p);
    }
    size_t byte_len = (size_t)(p - start_ptr);
    return val_str(str_new(in, start_ptr, byte_len));
}

/* uat(s, 字符索引): 获取第 i 个字符，负数从末尾算，越界返回 nil */
static Value f_uat(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "uat", argv, 0, V_STR, "string");
    const char *s = val_get_cstr(argv[0]);
    int total_chars = (int)utf8_char_count(s);
    int idx = int_arg(in, at, "uat", argv, 1);
    if (idx < 0) idx += total_chars;
    if (idx < 0 || idx >= total_chars) return val_nil();

    const char *p = s;
    for (int i = 0; i < idx && *p; i++) {
        p += utf8_len((unsigned char)*p);
    }
    size_t clen = utf8_len((unsigned char)*p);
    return val_str(str_new(in, p, clen));
}

/* format(模板, 实参...): 将模板中的 {} 依次替换为参数字符串，支持 {{ 转义为 { */
static Value f_format(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "format", argv, 0, V_STR, "string");
    const char *fmt = val_get_cstr(argv[0]);
    int arg_idx = 1;
    Buf b = {0};

    for (const char *p = fmt; *p; p++) {
        if (*p == '{') {
            if (p[1] == '{') {
                buf_add(&b, "{", 1);
                p++;
            } else if (p[1] == '}') {
                if (arg_idx >= argc) {
                    free(b.buf);
                    rt_error(in, at, "format() 参数不足 (占位符多于传入参数)");
                }
                char *s = value_to_string(argv[arg_idx++], 0);
                buf_add(&b, s, strlen(s));
                free(s);
                p++;
            } else {
                buf_add(&b, p, 1);
            }
        } else if (*p == '}' && p[1] == '}') {
            buf_add(&b, "}", 1);
            p++;
        } else {
            buf_add(&b, p, 1);
        }
    }

    Str *res = str_new(in, b.buf ? b.buf : "", b.len);
    free(b.buf);
    return val_str(res);
}

/* ---------- 字符串 ---------- */

/* split(s, sep):sep 为空串时按 UTF-8 字符拆分 */
static Value f_split(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "split", argv, 0, V_STR, "string");
    need_arg(in, at, "split", argv, 1, V_STR, "string");
    gc_push_root(in, argv[0]);
    gc_push_root(in, argv[1]);

    const char *s = val_get_cstr(argv[0]);
    const char *sep = val_get_cstr(argv[1]);
    size_t sl = strlen(sep);
    Array *a = array_new(in);
    Value aval = val_array(a);
    gc_push_root(in, aval);

    if (sl == 0) {
        while (*s) {
            size_t n = utf8_len((unsigned char)*s), rem = strlen(s);
            if (n > rem) n = rem;
            Str *sub = str_new(in, s, n);
            array_push(a, val_str(sub));
            s += n;
        }
        gc_pop_roots(in, 3);
        return aval;
    }
    for (;;) {
        const char *p = strstr(s, sep);
        if (!p) {
            Str *sub = str_new(in, s, strlen(s));
            array_push(a, val_str(sub));
            break;
        }
        Str *sub = str_new(in, s, (size_t)(p - s));
        array_push(a, val_str(sub));
        s = p + sl;
    }
    gc_pop_roots(in, 3);
    return aval;
}

/* join(数组, [分隔符]):元素按 print 的方式转成文本 */
static Value f_join(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "join", argv, 0, V_ARRAY, "array");
    const char *sep = "";
    if (argc == 2) {
        need_arg(in, at, "join", argv, 1, V_STR, "string");
        sep = val_get_cstr(argv[1]);
    }
    size_t sl = strlen(sep);
    Array *a = argv[0].as.array;
    Buf b = {0};
    for (int i = 0; i < a->count; i++) {
        if (i) buf_add(&b, sep, sl);
        char *t = value_to_string(a->items[i], 0);
        buf_add(&b, t, strlen(t));
        free(t);
    }
    Str *res = str_new(in, b.buf ? b.buf : "", b.len);
    free(b.buf);
    return val_str(res);
}

/* substr(s, 起点, [长度]):起点可为负数(从末尾算),越界自动截断 */
static Value f_substr(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "substr", argv, 0, V_STR, "string");
    const char *s = val_get_cstr(argv[0]);
    int n = (int)(argv[0].as.str ? argv[0].as.str->len : 0);
    int start = norm_index(int_arg(in, at, "substr", argv, 1), n);
    int len = opt_int(in, at, "substr", argc, argv, 2, n - start);
    if (len < 0) len = 0;
    if (len > n - start) len = n - start;
    return val_str(str_new(in, s + start, (size_t)len));
}

static Value map_chars(Interp *in, const Node *at, const char *fn, Value *argv, int upper) {
    need_arg(in, at, fn, argv, 0, V_STR, "string");
    const char *s = val_get_cstr(argv[0]);
    size_t len = argv[0].as.str ? argv[0].as.str->len : 0;
    Str *res = str_new(in, s, len);
    for (char *p = res->chars; *p; p++) {
        if (upper && *p >= 'a' && *p <= 'z') *p -= 32;
        if (!upper && *p >= 'A' && *p <= 'Z') *p += 32;
    }
    return val_str(res);
}

static Value f_upper(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc; return map_chars(in, at, "upper", argv, 1);
}
static Value f_lower(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc; return map_chars(in, at, "lower", argv, 0);
}

static Value f_trim(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "trim", argv, 0, V_STR, "string");
    const char *s = val_get_cstr(argv[0]);
    const char *e = s + (argv[0].as.str ? argv[0].as.str->len : 0);
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
    return val_str(str_new(in, s, (size_t)(e - s)));
}

/* contains(容器, 值):字符串查子串 / 数组查元素 / map 查键 */
static Value f_contains(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    switch (argv[0].type) {
    case V_STR:
        need_arg(in, at, "contains", argv, 1, V_STR, "string");
        return val_bool(strstr(val_get_cstr(argv[0]), val_get_cstr(argv[1])) != NULL);
    case V_ARRAY:
        for (int i = 0; i < argv[0].as.array->count; i++)
            if (values_equal(argv[0].as.array->items[i], argv[1])) return val_bool(1);
        return val_bool(0);
    case V_MAP:
        need_arg(in, at, "contains", argv, 1, V_STR, "string");
        return val_bool(map_get(argv[0].as.map, val_get_cstr(argv[1])) != NULL);
    default:
        rt_error(in, at, "contains() 的第 1 个参数应为 string、array 或 map,实际是 %s",
                 type_name(argv[0]));
    }
}

/* find(容器, 值):返回位置,找不到返回 -1 */
static Value f_find(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    if (argv[0].type == V_STR) {
        need_arg(in, at, "find", argv, 1, V_STR, "string");
        const char *hay = val_get_cstr(argv[0]);
        const char *p = strstr(hay, val_get_cstr(argv[1]));
        return val_int(p ? (int32_t)(p - hay) : -1);
    }
    if (argv[0].type == V_ARRAY) {
        for (int i = 0; i < argv[0].as.array->count; i++)
            if (values_equal(argv[0].as.array->items[i], argv[1])) return val_int(i);
        return val_int(-1);
    }
    rt_error(in, at, "find() 的第 1 个参数应为 string 或 array,实际是 %s", type_name(argv[0]));
}

static Value f_replace(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    for (int i = 0; i < 3; i++) need_arg(in, at, "replace", argv, i, V_STR, "string");
    const char *s = val_get_cstr(argv[0]);
    const char *old = val_get_cstr(argv[1]);
    const char *nw = val_get_cstr(argv[2]);
    size_t ol = strlen(old), nl = strlen(nw);
    if (ol == 0) rt_error(in, at, "replace() 的第 2 个参数不能为空字符串");
    Buf b = {0};
    for (;;) {
        const char *p = strstr(s, old);
        if (!p) { buf_add(&b, s, strlen(s)); break; }
        buf_add(&b, s, (size_t)(p - s));
        buf_add(&b, nw, nl);
        s = p + ol;
    }
    Str *res = str_new(in, b.buf ? b.buf : "", b.len);
    free(b.buf);
    return val_str(res);
}

static Value f_starts_with(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "starts_with", argv, 0, V_STR, "string");
    need_arg(in, at, "starts_with", argv, 1, V_STR, "string");
    return val_bool(strncmp(val_get_cstr(argv[0]), val_get_cstr(argv[1]),
                           argv[1].as.str ? argv[1].as.str->len : 0) == 0);
}

static Value f_ends_with(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "ends_with", argv, 0, V_STR, "string");
    need_arg(in, at, "ends_with", argv, 1, V_STR, "string");
    size_t a = argv[0].as.str ? argv[0].as.str->len : 0;
    size_t b = argv[1].as.str ? argv[1].as.str->len : 0;
    return val_bool(b <= a && strcmp(val_get_cstr(argv[0]) + a - b, val_get_cstr(argv[1])) == 0);
}

static Value f_repeat(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "repeat", argv, 0, V_STR, "string");
    int n = int_arg(in, at, "repeat", argv, 1);
    const char *s = val_get_cstr(argv[0]);
    size_t sl = argv[0].as.str ? argv[0].as.str->len : 0;
    if (n < 0) rt_error(in, at, "repeat() 的次数不能为负数");
    if (sl && (size_t)n > ((size_t)1 << 28) / sl) rt_error(in, at, "repeat() 结果太长");
    Buf b = {0};
    for (int i = 0; i < n; i++) buf_add(&b, s, sl);
    Str *res = str_new(in, b.buf ? b.buf : "", b.len);
    free(b.buf);
    return val_str(res);
}

/* chr(码点):返回对应的 UTF-8 字符 */
static Value f_chr(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    int cp = int_arg(in, at, "chr", argv, 0);
    if (cp <= 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        rt_error(in, at, "chr() 的码点无效: %d", cp);
    char s[5];
    size_t len = 0;
    if (cp < 0x80) {
        s[0] = (char)cp; len = 1;
    } else if (cp < 0x800) {
        s[0] = (char)(0xC0 | (cp >> 6)); s[1] = (char)(0x80 | (cp & 0x3F)); len = 2;
    } else if (cp < 0x10000) {
        s[0] = (char)(0xE0 | (cp >> 12)); s[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        s[2] = (char)(0x80 | (cp & 0x3F)); len = 3;
    } else {
        s[0] = (char)(0xF0 | (cp >> 18)); s[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        s[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); s[3] = (char)(0x80 | (cp & 0x3F)); len = 4;
    }
    s[len] = '\0';
    return val_str(str_new(in, s, len));
}

/* ord(s):第一个 UTF-8 字符的码点,空串返回 nil */
static Value f_ord(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)argc;
    need_arg(in, at, "ord", argv, 0, V_STR, "string");
    const unsigned char *p = (const unsigned char *)val_get_cstr(argv[0]);
    if (!*p) return val_nil();
    size_t n = utf8_len(*p);
    if (n == 1) return val_int(*p);
    int cp = *p & (0xFF >> (n + 1));
    for (size_t k = 1; k < n; k++) {
        if ((p[k] & 0xC0) != 0x80) return val_int(*p);   /* 非法序列:返回首字节 */
        cp = (cp << 6) | (p[k] & 0x3F);
    }
    return val_int(cp);
}

/* ---------- 数组 ---------- */

#define MAX_RANGE 10000000

/* range(n) / range(起, 止) / range(起, 止, 步长):左闭右开 */
static Value f_range(Interp *in, const Node *at, int argc, Value *argv) {
    /* 全是整数参数 → 产生 int 元素; 只要有 float 就产生 float 元素 */
    int all_int = 1;
    for (int i = 0; i < argc; i++) {
        if (!val_is_number(argv[i])) num_arg(in, at, "range", argv, i);   /* 触发类型错误 */
        if (argv[i].type != V_INT) all_int = 0;
    }
    double from = 0, to, step = 1;
    if (argc == 1) to = val_to_double(argv[0]);
    else { from = val_to_double(argv[0]); to = val_to_double(argv[1]); }
    if (argc == 3) step = val_to_double(argv[2]);
    if (step == 0) rt_error(in, at, "range() 的步长不能为 0");
    Array *a = array_new(in);
    Value aval = val_array(a);
    gc_push_root(in, aval);
    for (double x = from; step > 0 ? x < to : x > to; x += step) {
        if (a->count >= MAX_RANGE) rt_error(in, at, "range() 元素太多(上限 %d)", MAX_RANGE);
        array_push(a, all_int ? val_int((int32_t)x) : val_num(x));
    }
    gc_pop_roots(in, 1);
    return aval;
}

/* slice(数组, 起, [止]):返回新数组,左闭右开,负数从末尾算 */
static Value f_slice(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "slice", argv, 0, V_ARRAY, "array");
    gc_push_root(in, argv[0]);
    Array *src = argv[0].as.array;
    int n = src->count;
    int s = norm_index(int_arg(in, at, "slice", argv, 1), n);
    int e = norm_index(opt_int(in, at, "slice", argc, argv, 2, n), n);
    Array *a = array_new(in);
    Value aval = val_array(a);
    gc_push_root(in, aval);
    for (int i = s; i < e; i++) array_push(a, src->items[i]);
    gc_pop_roots(in, 2);
    return aval;
}

static Value f_reverse(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "reverse", argv, 0, V_ARRAY, "array");
    gc_push_root(in, argv[0]);
    Array *src = argv[0].as.array;
    Array *a = array_new(in);
    Value aval = val_array(a);
    gc_push_root(in, aval);
    for (int i = src->count - 1; i >= 0; i--) array_push(a, src->items[i]);
    gc_pop_roots(in, 2);
    return aval;
}

static int cmp_num(const void *x, const void *y) {
    double a = val_to_double(*(const Value *)x), b = val_to_double(*(const Value *)y);
    return (a > b) - (a < b);
}
static int cmp_str(const void *x, const void *y) {
    return strcmp(val_get_cstr(*(const Value *)x), val_get_cstr(*(const Value *)y));
}

/* sort(数组, [比较函数]):返回排好序的新数组;无比较函数时元素须全是数字或全是字符串 */
static Value f_sort(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "sort", argv, 0, V_ARRAY, "array");
    Value fn = val_nil();
    if (argc >= 2) {
        if (argv[1].type != V_FN && argv[1].type != V_NATIVE) {
            rt_error(in, at, "sort() 的比较器应为函数,实际是 %s", type_name(argv[1]));
        }
        fn = argv[1];
    }

    gc_push_root(in, argv[0]);
    if (fn.type != V_NIL) gc_push_root(in, fn);

    Array *src = argv[0].as.array;
    Array *a = array_new(in);
    Value aval = val_array(a);
    gc_push_root(in, aval);
    for (int i = 0; i < src->count; i++) array_push(a, src->items[i]);
    if (a->count <= 1) {
        gc_pop_roots(in, fn.type != V_NIL ? 3 : 2);
        return aval;
    }

    if (fn.type != V_NIL) {
        /* 自定义比较函数: 使用插入排序 (稳定且可在比较过程中安全执行解释器函数调用) */
        for (int i = 1; i < a->count; i++) {
            Value key = a->items[i];
            int j = i - 1;
            while (j >= 0) {
                Value call_args[2] = { key, a->items[j] };
                Value cmp_res = interp_call(in, at, fn, 2, call_args);
                /* 如果 cmp_res < 0 则 key 应排在 a->items[j] 前面 */
                double cv = 0;
                if (val_is_number(cmp_res)) {
                    cv = val_to_double(cmp_res);
                } else if (cmp_res.type == V_BOOL) {
                    cv = cmp_res.as.boolean ? -1.0 : 1.0;
                } else {
                    rt_error(in, at, "sort() 自定义比较函数应返回数字或布尔值,实际是 %s", type_name(cmp_res));
                }
                if (cv < 0) {
                    a->items[j + 1] = a->items[j];
                    j--;
                } else {
                    break;
                }
            }
            a->items[j + 1] = key;
        }
    } else {
        /* 默认排序: 元素须全是数字(int/float 可混合)或全是字符串 */
        int numeric = val_is_number(a->items[0]);
        for (int i = 0; i < a->count; i++)
            if (numeric ? !val_is_number(a->items[i]) : a->items[i].type != V_STR)
                rt_error(in, at, "sort() 默认只能排序全是数字或全是字符串的数组");
        qsort(a->items, (size_t)a->count, sizeof(Value), numeric ? cmp_num : cmp_str);
    }

    gc_pop_roots(in, fn.type != V_NIL ? 3 : 2);
    return aval;
}

/* map(数组, 函数): 对每个元素应用 fn(item), 返回新数组 */
static Value f_map(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "map", argv, 0, V_ARRAY, "array");
    if (argv[1].type != V_FN && argv[1].type != V_NATIVE) {
        rt_error(in, at, "map() 的第 2 个参数应为函数,实际是 %s", type_name(argv[1]));
    }
    gc_push_root(in, argv[0]);
    gc_push_root(in, argv[1]);

    Array *src = argv[0].as.array;
    Value fn = argv[1];
    Array *res = array_new(in);
    Value res_val = val_array(res);
    gc_push_root(in, res_val);

    for (int i = 0; i < src->count; i++) {
        Value arg = src->items[i];
        Value out = interp_call(in, at, fn, 1, &arg);
        array_push(res, out);
    }

    gc_pop_roots(in, 3);
    return res_val;
}

/* filter(数组, 谓词函数): 保留使 pred(item) 为真的元素, 返回新数组 */
static Value f_filter(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "filter", argv, 0, V_ARRAY, "array");
    if (argv[1].type != V_FN && argv[1].type != V_NATIVE) {
        rt_error(in, at, "filter() 的第 2 个参数应为函数,实际是 %s", type_name(argv[1]));
    }
    gc_push_root(in, argv[0]);
    gc_push_root(in, argv[1]);

    Array *src = argv[0].as.array;
    Value fn = argv[1];
    Array *res = array_new(in);
    Value res_val = val_array(res);
    gc_push_root(in, res_val);

    for (int i = 0; i < src->count; i++) {
        Value arg = src->items[i];
        Value out = interp_call(in, at, fn, 1, &arg);
        if (is_truthy(out)) {
            array_push(res, arg);
        }
    }

    gc_pop_roots(in, 3);
    return res_val;
}

/* reduce(数组, 函数, [初始值]): 折叠数组 */
static Value f_reduce(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "reduce", argv, 0, V_ARRAY, "array");
    if (argv[1].type != V_FN && argv[1].type != V_NATIVE) {
        rt_error(in, at, "reduce() 的第 2 个参数应为函数,实际是 %s", type_name(argv[1]));
    }
    Array *src = argv[0].as.array;
    Value fn = argv[1];

    if (src->count == 0 && argc < 3) {
        rt_error(in, at, "空数组调用 reduce() 时必须提供初始值");
    }

    gc_push_root(in, argv[0]);
    gc_push_root(in, argv[1]);

    Value acc;
    int start_i = 0;
    if (argc >= 3) {
        acc = argv[2];
        start_i = 0;
    } else {
        acc = src->items[0];
        start_i = 1;
    }
    gc_push_root(in, acc);

    for (int i = start_i; i < src->count; i++) {
        Value call_args[2] = { acc, src->items[i] };
        acc = interp_call(in, at, fn, 2, call_args);
        in->temp_roots[in->temp_root_count - 1] = acc; /* 更新栈顶保护的累加值 */
    }

    gc_pop_roots(in, 3);
    return acc;
}

static const Native str_natives[] = {
    {"split", 2, 2, f_split, "split(字符串, 分隔符)", "按分隔符拆成数组;分隔符为 \"\" 时按字符拆"},
    {"join", 1, 2, f_join, "join(数组, [分隔符])", "把数组元素连接成字符串"},
    {"substr", 2, 3, f_substr, "substr(字符串, 起点, [长度])", "截取子串,起点可为负数(从末尾算)"},
    {"upper", 1, 1, f_upper, "upper(字符串)", "转为大写(仅 ASCII 字母)"},
    {"lower", 1, 1, f_lower, "lower(字符串)", "转为小写(仅 ASCII 字母)"},
    {"trim", 1, 1, f_trim, "trim(字符串)", "去掉首尾空白"},
    {"contains", 2, 2, f_contains, "contains(容器, 值)", "字符串含子串 / 数组含元素 / map 含键"},
    {"find", 2, 2, f_find, "find(容器, 值)", "返回位置(字符串或数组),找不到返回 -1"},
    {"replace", 3, 3, f_replace, "replace(字符串, 旧, 新)", "替换所有出现的子串"},
    {"starts_with", 2, 2, f_starts_with, "starts_with(字符串, 前缀)", "是否以前缀开头"},
    {"ends_with", 2, 2, f_ends_with, "ends_with(字符串, 后缀)", "是否以后缀结尾"},
    {"repeat", 2, 2, f_repeat, "repeat(字符串, 次数)", "重复拼接字符串"},
    {"chr", 1, 1, f_chr, "chr(码点)", "码点转 UTF-8 字符,如 chr(65) 得 \"A\""},
    {"ord", 1, 1, f_ord, "ord(字符串)", "第一个字符的码点,空串返回 nil"},
    {"range", 1, 3, f_range, "range(止) / range(起, 止, [步长])", "生成数字数组,左闭右开"},
    {"slice", 2, 3, f_slice, "slice(数组, 起, [止])", "截取数组的一段,返回新数组"},
    {"reverse", 1, 1, f_reverse, "reverse(数组)", "返回反转后的新数组"},
    {"sort", 1, 2, f_sort, "sort(数组, [比较函数])", "返回排序新数组,支持自定义 cmp(a, b)"},
    {"map", 2, 2, f_map, "map(数组, 函数)", "对数组每个元素应用函数, 返回新数组"},
    {"filter", 2, 2, f_filter, "filter(数组, 谓词函数)", "保留使谓词为真的元素, 返回新数组"},
    {"reduce", 2, 3, f_reduce, "reduce(数组, 函数, [初始值])", "用双参函数折叠数组"},
    {"ulen", 1, 1, f_ulen, "ulen(字符串)", "按 UTF-8 字符计算长度"},
    {"usubstr", 2, 3, f_usubstr, "usubstr(字符串, 起点, [长度])", "按 UTF-8 字符切片,不截断多字节字符"},
    {"uat", 2, 2, f_uat, "uat(字符串, 索引)", "获取指定索引处的单个字符,支持负数索引"},
    {"format", 1, -1, f_format, "format(模板, 值...)", "按 {} 占位符格式化插值字符串"},
};

const NativeGroup group_string = {
    "string", "字符串与数组", str_natives, sizeof str_natives / sizeof str_natives[0]
};
