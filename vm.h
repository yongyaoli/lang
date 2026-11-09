#ifndef L_VM_H
#define L_VM_H

#include "bytecode.h"
#include "interp.h"

#define L_VM_STACK_MAX  2048
#define L_VM_FRAMES_MAX 256

typedef struct {
    Func     *closure;          /* 当前执行的闭包 */
    uint8_t  *ip;               /* 字节码指令指针 */
    Value    *slots;            /* 在全局值栈上的基址 */
} CallFrame;

typedef struct {
    uint8_t  *catch_ip;         /* 异常捕获跳转目标 */
    int       frame_count;      /* 异常恢复时的栈帧深度 */
    int       stack_count;      /* 异常恢复时的栈高度 */
    Upvalue  *open_upvalues;    /* 待恢复的 upvalues */
} VmTryBlock;

typedef struct VM {
    Interp   *in;
    Value     stack[L_VM_STACK_MAX];
    Value    *stack_top;
    CallFrame frames[L_VM_FRAMES_MAX];
    int       frame_count;
    Upvalue  *open_upvalues;
    VmTryBlock try_stack[32];
    int       try_count;
    int       echo;             /* REPL 回显标志 */
} VM;

void vm_init(VM *vm, Interp *in);
void vm_free(VM *vm);

/* 运行顶层函数原型，成功返回 0，发生运行时错误返回 1 */
int vm_run_proto(VM *vm, Proto *proto, int echo);

/* 导出为 .lc 二进制字节码文件 */
int vm_dump_proto_to_file(Proto *proto, const char *filepath);

/* 从 .lc 字节码文件/内存缓冲区加载 Proto */
Proto *vm_load_proto_from_file(Interp *in, const char *filepath);
Proto *vm_load_proto_from_buffer(Interp *in, const uint8_t *buf, size_t len);

#endif /* L_VM_H */
