#ifndef L_VALUE_H
#define L_VALUE_H

#include "ast.h"
#include <stdint.h>
#include "gc.h"

/* 前向声明:Value 会引用它们 */
typedef struct Env Env;
typedef struct Interp Interp;
typedef struct Str Str;

/* 数字分两种: V_INT = 32 位有符号整数(MCU 上快速、省内存), V_NUM = 浮点(双精度) */
typedef enum {
    V_NIL, V_BOOL, V_INT, V_NUM, V_STR, V_BYTES, V_ARRAY, V_MAP, V_FN, V_NATIVE, V_OBJ
} ValueType;

typedef struct Value Value;
typedef struct Bytes Bytes;
typedef struct Array Array;
typedef struct Map Map;
typedef struct Func Func;
typedef struct Proto Proto;
typedef struct Upvalue Upvalue;
typedef struct Native Native;
typedef struct NativeObj NativeObj;
typedef struct ObjClass ObjClass;
typedef struct ObjMethod ObjMethod;

struct Str {
    GcObj gc;
    size_t len;
    char chars[1];          /* 柔性数组成员, 包含结尾 '\0' */
};

/* 定长字节缓冲区(每字节 1B, 用于 I2C/SPI/UART 等硬件通信, 避免 Array 每元素 16B 的开销) */
struct Bytes {
    GcObj gc;
    size_t len;
    uint8_t data[1];
};

struct Value {
    ValueType type;
    union {
        int boolean;
        int32_t i;          /* V_INT */
        double num;         /* V_NUM (浮点) */
        Str *str;           /* GC 托管的字符串对象 */
        Bytes *bytes;       /* GC 托管的定长字节缓冲 */
        Array *array;
        Map *map;
        Func *fn;
        const Native *native;
        NativeObj *obj;     /* 原生/硬件对象, 如 Pin(2, "out") */
    } as;
};

struct Array {
    GcObj gc;
    Value *items;
    int count, cap;
};

/* Map: 键为 GC 字符串对象, 保持插入顺序 */
struct Map {
    GcObj gc;
    Str **keys;
    Value *vals;
    int count, cap;
};

/* 用户函数 = 函数声明节点 (AST模式) 或编译字节码原型 (VM模式) + 闭包捕获 */
struct Func {
    GcObj gc;
    const Node *decl;       /* AST 声明节点 (AST 模式) */
    Proto *proto;           /* 编译字节码原型 (VM 模式) */
    Env *closure;           /* 作用域环境指针 (兼容) */
    Upvalue **upvalues;     /* 运行时捕获的 Upvalue 数组 (VM 模式) */
    int upvalue_count;
};

/* 内置函数 */
typedef Value (*NativeFn)(Interp *in, const Node *at, int argc, Value *argv);
struct Native {
    const char *name;
    int min_args;
    int max_args;           /* -1 表示不限 */
    NativeFn fn;
    const char *usage;      /* help 用:调用形式,如 "push(数组, 值)" */
    const char *doc;        /* help 用:一句话说明 */
};

/* 原生对象方法表(常驻 Flash/.rodata, 零 RAM 开销) */
typedef Value (*MethodFn)(Interp *in, const Node *at, Value self, int argc, Value *argv);
struct ObjMethod {
    const char *name;
    int min_args;
    int max_args;           /* -1 表示不限 */
    MethodFn fn;
    const char *usage;      /* help 用:如 "pin.on()" */
    const char *doc;        /* help 用:一句话说明 */
};

struct ObjClass {
    const char *name;           /* 类型名, 如 "Pin" */
    const ObjMethod *methods;   /* 方法表 */
    size_t method_count;
    void (*format)(const NativeObj *obj, char *buf, size_t size);
    void (*destroy)(NativeObj *obj);
};

/* 紧凑原生对象: 4 个 32 位内联槽位足够描述 Pin/PWM/ADC/Timer 等硬件句柄, 免二次 malloc */
struct NativeObj {
    GcObj gc;
    const ObjClass *cls;
    int32_t fields[4];
    void *extra;
};

/* 构造与值包装 */
Value val_nil(void);
Value val_bool(int b);
Value val_num(double n);            /* 浮点 */
Value val_int(int32_t n);           /* 整数 */
int val_is_number(Value v);         /* int 或 float */
Value val_whole(double d);          /* 整数值且在 int32 内 → int, 否则 float */
double val_to_double(Value v);      /* int/float 统一转 double (调用前需确认 val_is_number) */
Value val_str(Str *s);
Str *str_new(Interp *in, const char *chars, size_t len);
Value val_bytes(Bytes *b);
Value val_array(Array *a);
Value val_map(Map *m);
Value val_obj(NativeObj *o);
Value val_fn(Func *f);
void  val_print(Value v);

/* 获取字符串底层 C 字符串指针 */
const char *val_get_cstr(Value v);

/* 对象创建(分配并链入 GC) */
Str *str_new(Interp *in, const char *chars, size_t len);
Bytes *bytes_new(Interp *in, size_t len, const uint8_t *init);
Array *array_new(Interp *in);
Map *map_new(Interp *in);
Func *func_new(Interp *in, const Node *decl, Env *closure);
Func *func_proto_new(Interp *in, Proto *proto);
NativeObj *obj_new(Interp *in, const ObjClass *cls);

void array_push(Array *a, Value v);
Value *map_get(const Map *m, const char *key);   /* 找不到返回 NULL */
void map_set(Map *m, Str *key, Value v);
int map_remove(Map *m, const char *key);         /* 删除键, 存在返回 1 */

/* 语义 */
int is_truthy(Value v);                 /* 只有 false 和 nil 为假 */
int values_equal(Value a, Value b);
const char *type_name(Value v);

/* 转成文本(malloc 返回,调用者负责 free);quoted=1 时字符串带引号 */
char *value_to_string(Value v, int quoted);

#endif
