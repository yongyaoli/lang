/*
 * L 语言流式 JSON 编解码器实现
 *
 * 专注于 ESP32 及嵌入式环境的零外部依赖单趟 JSON 解析与序列化实现。
 * 纯 C99 架构，无递归溢出隐患（深度防御），GC 根安全扫描，
 * 支持精确行列定位的错误诊断。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "l_json.h"
#include "interp.h"
#include "value.h"

#define JSON_MAX_PARSE_DEPTH 16
#define JSON_MAX_SERIALIZE_DEPTH 32

/* =========================================================================
 * 1. 动态字符串缓冲区 (用于高效序列化与解析)
 * ========================================================================= */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} JsonBuf;

static void jbuf_init(JsonBuf *b, size_t initial_cap) {
    if (initial_cap < 32) initial_cap = 32;
    b->data = (char *)malloc(initial_cap);
    b->len = 0;
    b->cap = initial_cap;
    if (b->data) b->data[0] = '\0';
}

static void jbuf_grow(JsonBuf *b, size_t need) {
    if (b->len + need + 1 <= b->cap) return;
    size_t new_cap = b->cap * 2;
    while (new_cap < b->len + need + 1) {
        new_cap *= 2;
    }
    char *new_data = (char *)realloc(b->data, new_cap);
    if (!new_data) return;
    b->data = new_data;
    b->cap = new_cap;
}

static void jbuf_putc(JsonBuf *b, char c) {
    jbuf_grow(b, 1);
    if (!b->data) return;
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

static void jbuf_puts(JsonBuf *b, const char *s, size_t n) {
    jbuf_grow(b, n);
    if (!b->data) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void jbuf_indent(JsonBuf *b, int level) {
    jbuf_grow(b, (size_t)level * 2);
    if (!b->data) return;
    for (int i = 0; i < level * 2; i++) {
        b->data[b->len++] = ' ';
    }
    b->data[b->len] = '\0';
}

static void jbuf_free(JsonBuf *b) {
    if (b->data) {
        free(b->data);
        b->data = NULL;
    }
    b->len = 0;
    b->cap = 0;
}

/* =========================================================================
 * 2. JSON 序列化器 (Stringify)
 * ========================================================================= */

static void serialize_string(JsonBuf *b, const char *str, size_t len) {
    jbuf_putc(b, '"');
    const unsigned char *p = (const unsigned char *)str;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = p[i];
        switch (c) {
        case '"':  jbuf_puts(b, "\\\"", 2); break;
        case '\\': jbuf_puts(b, "\\\\", 2); break;
        case '\b': jbuf_puts(b, "\\b", 2); break;
        case '\f': jbuf_puts(b, "\\f", 2); break;
        case '\n': jbuf_puts(b, "\\n", 2); break;
        case '\r': jbuf_puts(b, "\\r", 2); break;
        case '\t': jbuf_puts(b, "\\t", 2); break;
        default:
            if (c < 0x20) {
                char hex[8];
                snprintf(hex, sizeof hex, "\\u%04x", c);
                jbuf_puts(b, hex, 6);
            } else {
                /* UTF-8 多字节字符与普通字符直接原样写入 */
                jbuf_putc(b, (char)c);
            }
            break;
        }
    }
    jbuf_putc(b, '"');
}

static void serialize_value(Interp *in, const Node *at, JsonBuf *b, Value v, int pretty, int depth) {
    if (depth > JSON_MAX_SERIALIZE_DEPTH) {
        rt_error(in, at, "JSON 序列化超过最大嵌套深度 (%d 层)，可能存在循环引用", JSON_MAX_SERIALIZE_DEPTH);
    }

    switch (v.type) {
    case V_NIL:
        jbuf_puts(b, "null", 4);
        break;

    case V_BOOL:
        if (v.as.boolean) jbuf_puts(b, "true", 4);
        else jbuf_puts(b, "false", 5);
        break;

    case V_INT: {
        char buf[32];
        snprintf(buf, sizeof buf, "%d", (int)v.as.i);
        jbuf_puts(b, buf, strlen(buf));
        break;
    }

    case V_NUM: {
        double d = v.as.num;
        if (isnan(d) || isinf(d)) {
            jbuf_puts(b, "null", 4);
        } else {
            char buf[40];
            snprintf(buf, sizeof buf, "%.14g", d);
            jbuf_puts(b, buf, strlen(buf));
        }
        break;
    }

    case V_STR: {
        Str *s = v.as.str;
        if (!s) {
            jbuf_puts(b, "\"\"", 2);
        } else {
            serialize_string(b, s->chars, s->len);
        }
        break;
    }

    case V_BYTES: {
        /* bytes 缓冲序列化为字节数值数组: [1, 2, 3] */
        Bytes *bt = v.as.bytes;
        jbuf_putc(b, '[');
        if (bt) {
            for (size_t i = 0; i < bt->len; i++) {
                if (i > 0) jbuf_puts(b, pretty ? ", " : ",", pretty ? 2 : 1);
                char num[16];
                snprintf(num, sizeof num, "%u", (unsigned int)bt->data[i]);
                jbuf_puts(b, num, strlen(num));
            }
        }
        jbuf_putc(b, ']');
        break;
    }

    case V_ARRAY: {
        Array *arr = v.as.array;
        if (!arr || arr->count == 0) {
            jbuf_puts(b, "[]", 2);
            break;
        }
        jbuf_putc(b, '[');
        if (pretty) jbuf_putc(b, '\n');
        for (int i = 0; i < arr->count; i++) {
            if (pretty) jbuf_indent(b, depth + 1);
            serialize_value(in, at, b, arr->items[i], pretty, depth + 1);
            if (i + 1 < arr->count) jbuf_putc(b, ',');
            if (pretty) jbuf_putc(b, '\n');
        }
        if (pretty) jbuf_indent(b, depth);
        jbuf_putc(b, ']');
        break;
    }

    case V_MAP: {
        Map *map = v.as.map;
        if (!map || map->count == 0) {
            jbuf_puts(b, "{}", 2);
            break;
        }
        jbuf_putc(b, '{');
        if (pretty) jbuf_putc(b, '\n');
        int emitted = 0;
        for (int i = 0; i < map->count; i++) {
            if (!map->keys[i]) continue;
            if (emitted > 0) {
                jbuf_putc(b, ',');
                if (pretty) jbuf_putc(b, '\n');
            }
            if (pretty) jbuf_indent(b, depth + 1);
            serialize_string(b, map->keys[i]->chars, map->keys[i]->len);
            jbuf_puts(b, pretty ? ": " : ":", pretty ? 2 : 1);
            serialize_value(in, at, b, map->vals[i], pretty, depth + 1);
            emitted++;
        }
        if (pretty) {
            jbuf_putc(b, '\n');
            jbuf_indent(b, depth);
        }
        jbuf_putc(b, '}');
        break;
    }

    default:
        /* 其他类型 (函数、原生对象等) 在 JSON 中序列化为 null */
        jbuf_puts(b, "null", 4);
        break;
    }
}

Value l_json_stringify(Interp *in, const Node *at, Value val, int pretty) {
    JsonBuf b;
    jbuf_init(&b, 128);
    serialize_value(in, at, &b, val, pretty ? 1 : 0, 0);
    Str *res = str_new(in, b.data ? b.data : "", b.len);
    jbuf_free(&b);
    return val_str(res);
}

/* =========================================================================
 * 3. 流式 JSON 解码器 (Parse)
 * ========================================================================= */

typedef struct {
    Interp *in;
    const Node *at;
    const char *src;
    size_t len;
    size_t pos;
    int line;
    int col;
    int depth;
} JsonParser;

static char p_peek(JsonParser *p) {
    if (p->pos >= p->len) return '\0';
    return p->src[p->pos];
}

static char p_next(JsonParser *p) {
    if (p->pos >= p->len) return '\0';
    char c = p->src[p->pos++];
    if (c == '\n') {
        p->line++;
        p->col = 1;
    } else {
        p->col++;
    }
    return c;
}

static void p_skip_ws(JsonParser *p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            p_next(p);
        } else {
            break;
        }
    }
}

static L_NORETURN void p_error(JsonParser *p, const char *msg) {
    rt_error(p->in, p->at, "[JSON 解析错误 行:%d 列:%d] %s", p->line, p->col, msg);
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void encode_utf8(uint32_t cp, JsonBuf *b) {
    if (cp <= 0x7F) {
        jbuf_putc(b, (char)cp);
    } else if (cp <= 0x7FF) {
        jbuf_putc(b, (char)(0xC0 | (cp >> 6)));
        jbuf_putc(b, (char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        jbuf_putc(b, (char)(0xE0 | (cp >> 12)));
        jbuf_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        jbuf_putc(b, (char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        jbuf_putc(b, (char)(0xF0 | (cp >> 18)));
        jbuf_putc(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
        jbuf_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        jbuf_putc(b, (char)(0x80 | (cp & 0x3F)));
    }
}

static Value parse_value(JsonParser *p);

static Value parse_string(JsonParser *p) {
    p_next(p); /* 消耗开头的 '"' */
    JsonBuf b;
    jbuf_init(&b, 32);

    while (1) {
        char c = p_peek(p);
        if (c == '\0') {
            jbuf_free(&b);
            p_error(p, "未闭合的字符串字面量");
        }
        if (c == '"') {
            p_next(p); /* 消耗结尾的 '"' */
            break;
        }
        if ((unsigned char)c < 0x20) {
            jbuf_free(&b);
            p_error(p, "字符串中包含未转义的控制字符");
        }

        if (c == '\\') {
            p_next(p); /* 消耗 '\\' */
            char esc = p_next(p);
            switch (esc) {
            case '"':  jbuf_putc(&b, '"'); break;
            case '\\': jbuf_putc(&b, '\\'); break;
            case '/':  jbuf_putc(&b, '/'); break;
            case 'b':  jbuf_putc(&b, '\b'); break;
            case 'f':  jbuf_putc(&b, '\f'); break;
            case 'n':  jbuf_putc(&b, '\n'); break;
            case 'r':  jbuf_putc(&b, '\r'); break;
            case 't':  jbuf_putc(&b, '\t'); break;
            case 'u': {
                uint32_t cp = 0;
                for (int i = 0; i < 4; i++) {
                    int h = hex_val(p_next(p));
                    if (h < 0) {
                        jbuf_free(&b);
                        p_error(p, "无效的 Unicode 转义序列 (\\uXXXX)");
                    }
                    cp = (cp << 4) | (uint32_t)h;
                }
                /* 处理 UTF-16 代理对 */
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (p_peek(p) == '\\') {
                        size_t saved_pos = p->pos;
                        int saved_col = p->col;
                        p_next(p);
                        if (p_peek(p) == 'u') {
                            p_next(p);
                            uint32_t low = 0;
                            int valid_low = 1;
                            for (int i = 0; i < 4; i++) {
                                int h = hex_val(p_next(p));
                                if (h < 0) { valid_low = 0; break; }
                                low = (low << 4) | (uint32_t)h;
                            }
                            if (valid_low && low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            } else {
                                p->pos = saved_pos;
                                p->col = saved_col;
                            }
                        } else {
                            p->pos = saved_pos;
                            p->col = saved_col;
                        }
                    }
                }
                encode_utf8(cp, &b);
                break;
            }
            default:
                jbuf_free(&b);
                p_error(p, "非法的反斜杠转义字符");
            }
        } else {
            jbuf_putc(&b, c);
            p_next(p);
        }
    }

    Str *s = str_new(p->in, b.data ? b.data : "", b.len);
    jbuf_free(&b);
    return val_str(s);
}

static Value parse_number(JsonParser *p) {
    size_t start = p->pos;
    if (p_peek(p) == '-') {
        p_next(p);
    }

    char first = p_peek(p);
    if (!isdigit((unsigned char)first)) {
        p_error(p, "数字格式错误");
    }

    if (first == '0') {
        p_next(p);
        if (isdigit((unsigned char)p_peek(p))) {
            p_error(p, "JSON 不允许出现前导零的整数");
        }
    } else {
        while (isdigit((unsigned char)p_peek(p))) {
            p_next(p);
        }
    }

    int is_float = 0;
    if (p_peek(p) == '.') {
        is_float = 1;
        p_next(p);
        if (!isdigit((unsigned char)p_peek(p))) {
            p_error(p, "小数点后必须跟数字");
        }
        while (isdigit((unsigned char)p_peek(p))) {
            p_next(p);
        }
    }

    if (p_peek(p) == 'e' || p_peek(p) == 'E') {
        is_float = 1;
        p_next(p);
        if (p_peek(p) == '+' || p_peek(p) == '-') {
            p_next(p);
        }
        if (!isdigit((unsigned char)p_peek(p))) {
            p_error(p, "指数符号后必须跟数字");
        }
        while (isdigit((unsigned char)p_peek(p))) {
            p_next(p);
        }
    }

    size_t nlen = p->pos - start;
    char num_buf[64];
    if (nlen >= sizeof num_buf) {
        p_error(p, "数值过长");
    }
    memcpy(num_buf, p->src + start, nlen);
    num_buf[nlen] = '\0';

    if (is_float) {
        char *end = NULL;
        double d = strtod(num_buf, &end);
        return val_num(d);
    } else {
        char *end = NULL;
        long long ll = strtoll(num_buf, &end, 10);
        if (ll >= -2147483648LL && ll <= 2147483647LL) {
            return val_int((int32_t)ll);
        } else {
            return val_num((double)ll);
        }
    }
}

static Value parse_array(JsonParser *p) {
    p_next(p); /* 消耗 '[' */
    p->depth++;
    if (p->depth > JSON_MAX_PARSE_DEPTH) {
        char err_msg[64];
        snprintf(err_msg, sizeof err_msg, "JSON 嵌套层次过深 (超过 %d 层)", JSON_MAX_PARSE_DEPTH);
        p_error(p, err_msg);
    }

    Array *arr = array_new(p->in);
    Value arr_val = val_array(arr);
    gc_push_root(p->in, arr_val);

    p_skip_ws(p);
    if (p_peek(p) == ']') {
        p_next(p);
        p->depth--;
        gc_pop_roots(p->in, 1);
        return arr_val;
    }

    while (1) {
        p_skip_ws(p);
        Value item = parse_value(p);
        array_push(arr, item);

        p_skip_ws(p);
        char c = p_peek(p);
        if (c == ',') {
            p_next(p);
            p_skip_ws(p);
            if (p_peek(p) == ']') {
                gc_pop_roots(p->in, 1);
                p_error(p, "JSON 数组不允许尾随逗号 (trailing comma)");
            }
        } else if (c == ']') {
            p_next(p);
            break;
        } else {
            gc_pop_roots(p->in, 1);
            p_error(p, "数组元素后期望 ',' 或 ']'");
        }
    }

    p->depth--;
    gc_pop_roots(p->in, 1);
    return arr_val;
}

static Value parse_object(JsonParser *p) {
    p_next(p); /* 消耗 '{' */
    p->depth++;
    if (p->depth > JSON_MAX_PARSE_DEPTH) {
        char err_msg[64];
        snprintf(err_msg, sizeof err_msg, "JSON 嵌套层次过深 (超过 %d 层)", JSON_MAX_PARSE_DEPTH);
        p_error(p, err_msg);
    }

    Map *map = map_new(p->in);
    Value map_val = val_map(map);
    gc_push_root(p->in, map_val);

    p_skip_ws(p);
    if (p_peek(p) == '}') {
        p_next(p);
        p->depth--;
        gc_pop_roots(p->in, 1);
        return map_val;
    }

    while (1) {
        p_skip_ws(p);
        if (p_peek(p) != '"') {
            gc_pop_roots(p->in, 1);
            p_error(p, "对象属性名必须是以双引号包围的字符串");
        }

        Value key_val = parse_string(p);
        gc_push_root(p->in, key_val);

        p_skip_ws(p);
        if (p_peek(p) != ':') {
            gc_pop_roots(p->in, 2);
            p_error(p, "对象键名后期望冒号 ':'");
        }
        p_next(p); /* 消耗 ':' */

        p_skip_ws(p);
        Value val = parse_value(p);
        map_set(map, key_val.as.str, val);
        gc_pop_roots(p->in, 1); /* 弹出 key_val */

        p_skip_ws(p);
        char c = p_peek(p);
        if (c == ',') {
            p_next(p);
            p_skip_ws(p);
            if (p_peek(p) == '}') {
                gc_pop_roots(p->in, 1);
                p_error(p, "JSON 对象不允许尾随逗号 (trailing comma)");
            }
        } else if (c == '}') {
            p_next(p);
            break;
        } else {
            gc_pop_roots(p->in, 1);
            p_error(p, "对象属性后期望 ',' 或 '}'");
        }
    }

    p->depth--;
    gc_pop_roots(p->in, 1);
    return map_val;
}

static Value parse_value(JsonParser *p) {
    p_skip_ws(p);
    char c = p_peek(p);

    if (c == 'n') {
        if (p->pos + 4 <= p->len && strncmp(p->src + p->pos, "null", 4) == 0) {
            for (int i = 0; i < 4; i++) p_next(p);
            return val_nil();
        }
        p_error(p, "意外的标识符，期望 'null'");
    }

    if (c == 't') {
        if (p->pos + 4 <= p->len && strncmp(p->src + p->pos, "true", 4) == 0) {
            for (int i = 0; i < 4; i++) p_next(p);
            return val_bool(1);
        }
        p_error(p, "意外的标识符，期望 'true'");
    }

    if (c == 'f') {
        if (p->pos + 5 <= p->len && strncmp(p->src + p->pos, "false", 5) == 0) {
            for (int i = 0; i < 5; i++) p_next(p);
            return val_bool(0);
        }
        p_error(p, "意外的标识符，期望 'false'");
    }

    if (c == '"') return parse_string(p);
    if (c == '[') return parse_array(p);
    if (c == '{') return parse_object(p);
    if (c == '-' || isdigit((unsigned char)c)) return parse_number(p);

    if (c == '\0') p_error(p, "遇到意外的输入结束 (EOF)");

    char err_buf[64];
    snprintf(err_buf, sizeof err_buf, "意外的字符 '%c'", c);
    p_error(p, err_buf);
}

Value l_json_parse(Interp *in, const Node *at, const char *json_str, size_t len) {
    if (!json_str) {
        rt_error(in, at, "JSON.parse() 输入不能为空");
    }

    JsonParser p;
    p.in = in;
    p.at = at;
    p.src = json_str;
    p.len = len;
    p.pos = 0;
    p.line = 1;
    p.col = 1;
    p.depth = 0;

    p_skip_ws(&p);
    if (p_peek(&p) == '\0') {
        rt_error(in, at, "[JSON 解析错误] 空文本不是合法的 JSON");
    }

    Value result = parse_value(&p);

    p_skip_ws(&p);
    if (p_peek(&p) != '\0') {
        p_error(&p, "根值之后存在多余字符");
    }

    return result;
}

/* =========================================================================
 * 4. 内置方法与原生对象类 (JSON / json)
 * ========================================================================= */

static Value m_json_parse(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    if (argc < 1 || argv[0].type != V_STR) {
        rt_error(in, at, "JSON.parse(str) 的参数必须为字符串");
    }
    Str *s = argv[0].as.str;
    const char *chars = s ? s->chars : "";
    size_t len = s ? s->len : 0;
    return l_json_parse(in, at, chars, len);
}

static Value m_json_stringify(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    if (argc < 1) {
        rt_error(in, at, "JSON.stringify(val[, pretty]) 至少需要 1 个参数");
    }
    int pretty = (argc >= 2 && is_truthy(argv[1]));
    return l_json_stringify(in, at, argv[0], pretty);
}

static const ObjMethod json_methods[] = {
    {"parse",     1, 1, m_json_parse,     "JSON.parse(字符串)",          "将 JSON 字符串解析为原生 Map/Array/值"},
    {"stringify", 1, 2, m_json_stringify, "JSON.stringify(值[, 美化])", "将原生数据序列化为 JSON 字符串"},
};

static void json_format(const NativeObj *obj, char *buf, size_t size) {
    (void)obj;
    snprintf(buf, size, "<JSON 内置编解码器>");
}

static const ObjClass json_class = {
    "JSON",
    json_methods,
    sizeof(json_methods) / sizeof(json_methods[0]),
    json_format,
    NULL
};

/* =========================================================================
 * 5. 便捷全局函数与模块导出
 * ========================================================================= */

static Value b_json_parse(Interp *in, const Node *at, int argc, Value *argv) {
    return m_json_parse(in, at, val_nil(), argc, argv);
}

static Value b_json_stringify(Interp *in, const Node *at, int argc, Value *argv) {
    return m_json_stringify(in, at, val_nil(), argc, argv);
}

static const Native json_natives[] = {
    {"json_parse",     1, 1, b_json_parse,     "json_parse(字符串)",          "将 JSON 字符串解析为原生 Map/Array/值"},
    {"json_stringify", 1, 2, b_json_stringify, "json_stringify(值[, 美化])", "将原生数据序列化为 JSON 字符串"},
};

void json_install_globals(Interp *in) {
    /* 1. 注册全局单例对象 JSON 与 json */
    NativeObj *jo = obj_new(in, &json_class);
    Value jv = val_obj(jo);
    env_define(in->globals, "JSON", jv);
    env_define(in->globals, "json", jv);

    /* 2. 注册全局便捷函数 json_parse 与 json_stringify */
    for (size_t i = 0; i < sizeof(json_natives) / sizeof(json_natives[0]); i++) {
        Value v = val_nil();
        v.type = V_NATIVE;
        v.as.native = &json_natives[i];
        env_define(in->globals, json_natives[i].name, v);
    }
}

Value json_build_module(Interp *in) {
    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);

    NativeObj *jo = obj_new(in, &json_class);
    Value jv = val_obj(jo);
    map_set(m, str_new(in, "JSON", 4), jv);
    map_set(m, str_new(in, "json", 4), jv);

    for (size_t i = 0; i < sizeof(json_natives) / sizeof(json_natives[0]); i++) {
        Value v = val_nil();
        v.type = V_NATIVE;
        v.as.native = &json_natives[i];
        map_set(m, str_new(in, json_natives[i].name, strlen(json_natives[i].name)), v);
    }

    /* 模块方法别名: parse 与 stringify */
    Value p_val = val_nil();
    p_val.type = V_NATIVE;
    p_val.as.native = &json_natives[0];
    map_set(m, str_new(in, "parse", 5), p_val);

    Value s_val = val_nil();
    s_val.type = V_NATIVE;
    s_val.as.native = &json_natives[1];
    map_set(m, str_new(in, "stringify", 9), s_val);

    gc_pop_roots(in, 1);
    return mv;
}
