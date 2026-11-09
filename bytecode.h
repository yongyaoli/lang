#ifndef L_BYTECODE_H
#define L_BYTECODE_H

#include <stdint.h>
#include <stddef.h>
#include "l_config.h"
#include "value.h"

/* ================= 1. 虚拟机字节码操作码定义 ================= */

typedef enum {
    OP_NOP = 0,
    OP_CONST,           /* [uint16_t const_idx] 推入常量池对象 */
    OP_NIL,             /* 推入 nil */
    OP_TRUE,            /* 推入 true */
    OP_FALSE,           /* 推入 false */
    OP_INT,             /* [int32_t imm] 立即数有符号整数 */

    /* 栈管理 */
    OP_POP,             /* 弹出栈顶 */
    OP_DUP,             /* 复制栈顶 */

    /* 全局变量操作 */
    OP_DEF_GLOBAL,      /* [uint16_t name_idx] 声明全局变量并赋值 */
    OP_GET_GLOBAL,      /* [uint16_t name_idx] 读取全局变量 */
    OP_SET_GLOBAL,      /* [uint16_t name_idx] 写入全局变量 */

    /* 局部变量操作 (直接基于当前调用帧基址 slots[slot] 寻址, O(1)) */
    OP_GET_LOCAL,       /* [uint8_t slot] 读取局部变量 */
    OP_SET_LOCAL,       /* [uint8_t slot] 写入局部变量 */

    /* 闭包自由变量 (Upvalue) */
    OP_GET_UPVALUE,     /* [uint8_t index] 读取 Upvalue */
    OP_SET_UPVALUE,     /* [uint8_t index] 写入 Upvalue */
    OP_CLOSE_UPVALUE,   /* 闭合超出生存期的 Upvalue 到堆上 */

    /* 算术与位运算符 */
    OP_ADD,             /* + (支持整数、浮点与字符串拼接) */
    OP_SUB,             /* - */
    OP_MUL,             /* * */
    OP_DIV,             /* / (整数整除, 浮点实除) */
    OP_MOD,             /* % */
    OP_NEG,             /* - (一元取负) */
    OP_BIT_AND,         /* & */
    OP_BIT_OR,          /* | */
    OP_BIT_XOR,         /* ^ */
    OP_BIT_NOT,         /* ~ (一元按位取反) */
    OP_SHL,             /* << */
    OP_SHR,             /* >> */

    /* 逻辑与比较运算符 */
    OP_NOT,             /* ! (逻辑非) */
    OP_EQ,              /* == */
    OP_NE,              /* != */
    OP_LT,              /* < */
    OP_LE,              /* <= */
    OP_GT,              /* > */
    OP_GE,              /* >= */

    /* 流程控制与跳转 */
    OP_JUMP,            /* [int16_t offset] 无条件跳转 */
    OP_JUMP_IF_FALSE,   /* [int16_t offset] 若栈顶为假则跳转 (弹出栈顶) */
    OP_JUMP_IF_TRUE,    /* [int16_t offset] 若栈顶为真则跳转 (弹出栈顶) */
    OP_JUMP_PEEK_FALSE, /* [int16_t offset] 逻辑与短路: 为假跳转 (保留栈顶) */
    OP_JUMP_PEEK_TRUE,  /* [int16_t offset] 逻辑或短路: 为真跳转 (保留栈顶) */
    OP_LOOP,            /* [uint16_t offset] 循环无条件回跳 */

    /* 集合与下标读写 */
    OP_ARRAY,           /* [uint16_t count] 构造包含 count 个元素的数组 */
    OP_MAP,             /* [uint16_t pair_count] 构造包含 pair_count 对键值的字典 */
    OP_INDEX_GET,       /* 读取下标 a[i] */
    OP_INDEX_SET,       /* 写入下标 a[i] = v */

    /* 对象属性与方法调用 */
    OP_GET_PROP,        /* [uint16_t name_idx] 读取属性 obj.prop */
    OP_SET_PROP,        /* [uint16_t name_idx] 写入属性 obj.prop = v */
    OP_INVOKE,          /* [uint16_t name_idx, uint8_t argc] 直接调用对象方法 obj.method(...) */

    /* 函数调用与闭包 */
    OP_CALL,            /* [uint8_t argc] 调用栈顶函数 */
    OP_RETURN,          /* 从当前函数返回 (栈顶为返回值) */
    OP_CLOSURE,         /* [uint16_t proto_idx] 构造闭包对象并绑定 Upvalue 列表 */

    /* 异常处理 */
    OP_TRY,             /* [int16_t catch_offset] 压入 try 保护块 */
    OP_END_TRY,         /* 正常退出 try 保护块 */
    OP_THROW,           /* 抛出异常 */

    /* 调试与停机 */
    OP_ECHO,            /* REPL 交互模式下输出表达式结果 */
    OP_HALT             /* 虚拟机停机指令 */
} OpCode;

/* ================= 2. 源码行列调试信息 ================= */

typedef struct {
    int line;
    int col;
} LineInfo;

/* ================= 3. 字节码指令块 (Chunk) ================= */

typedef struct Chunk {
    uint8_t   *code;            /* 紧凑字节码指令序列 */
    int        count;           /* 指令字节计数 */
    int        cap;             /* 缓冲区容量 */
    LineInfo  *lines;           /* 每条字节码对应的源码行号与列号 */
    Value     *constants;       /* 常量池数组 (存放字符串、数值、子函数原型等) */
    int        const_count;
    int        const_cap;
} Chunk;

void chunk_init(Chunk *c);
void chunk_free(Chunk *c);
void chunk_write(Chunk *c, uint8_t byte, int line, int col);
void chunk_write_u16(Chunk *c, uint16_t u16, int line, int col);
void chunk_write_i16(Chunk *c, int16_t i16, int line, int col);
int  chunk_add_constant(Chunk *c, Value val);

/* 反汇编打印字节码指令 (用于调试与指令分析) */
void chunk_disassemble(const Chunk *c, const char *name);
int  chunk_disassemble_instruction(const Chunk *c, int offset);

/* ================= 4. 函数原型 (Proto) 与 Upvalue 描述 ================= */

typedef struct UpvalueDesc {
    uint8_t is_local;           /* 1: 捕获上一层局部变量 slot; 0: 捕获上一层 Upvalue */
    uint8_t index;              /* slot 索引或上级 upvalue 索引 */
} UpvalueDesc;

typedef struct Proto {
    GcObj        gc;
    const char  *name;          /* 函数名 (匿名函数为 "<fn>", 顶层为 "<main>") */
    const char  *source_file;   /* 源文件路径 */
    int          arity;         /* 形参个数 */
    int          is_vararg;     /* 是否为可变参数 */
    Chunk        chunk;         /* 字节码指令块 */
    UpvalueDesc *upvalues;      /* 待捕获自由变量描述表 */
    int          upvalue_count;
    int          upvalue_cap;
} Proto;

Proto *proto_new(Interp *in, const char *name, const char *source_file);

/* ================= 5. 运行时 Upvalue 对象 ================= */

typedef struct Upvalue {
    GcObj           gc;
    Value          *location;   /* 指向栈上的值 (打开状态) 或指向 closed (闭合状态) */
    Value           closed;     /* 局部变量离开作用域时保存其拷贝 */
    struct Upvalue *next;       /* 虚拟机维护的未闭合 Upvalue 单链表 */
} Upvalue;

Upvalue *upvalue_new(Interp *in, Value *slot);

#endif /* L_BYTECODE_H */
