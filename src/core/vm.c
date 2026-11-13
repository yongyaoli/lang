#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 当前活动的 VM(原生回调需要回到同一个值栈上执行字节码闭包) */
static VM *s_active_vm = NULL;

void vm_init(VM *vm, Interp *in) {
    vm->in = in;
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->open_upvalues = NULL;
    vm->try_count = 0;
    vm->echo = 0;
    vm->frame_base = 0;
    /* M7 值栈 GC 根扫描: 把值栈注册为 GC 根集合 */
    in->scan_stack = vm->stack;
    in->scan_stack_top = &vm->stack_top;
}

void vm_free(VM *vm) {
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->open_upvalues = NULL;
    vm->try_count = 0;
    /* 注销值栈扫描注册 */
    if (vm->in && vm->in->scan_stack_top == &vm->stack_top) {
        vm->in->scan_stack = NULL;
        vm->in->scan_stack_top = NULL;
    }
}

static inline void push(VM *vm, Value val) {
    if (vm->stack_top >= vm->stack + L_VM_STACK_MAX) {
        fprintf(stderr, "虚拟机错误: 运算值栈溢出 (Stack Overflow)\n");
        exit(1);
    }
    *vm->stack_top++ = val;
}

static inline Value pop(VM *vm) {
    if (vm->stack_top <= vm->stack) {
        fprintf(stderr, "虚拟机错误: 运算值栈下溢 (Stack Underflow)\n");
        exit(1);
    }
    return *(--vm->stack_top);
}

static inline Value peek(VM *vm, int distance) {
    return *(vm->stack_top - 1 - distance);
}

static Upvalue *capture_upvalue(VM *vm, Value *local) {
    Upvalue *prev = NULL;
    Upvalue *curr = vm->open_upvalues;
    while (curr != NULL && curr->location > local) {
        prev = curr;
        curr = curr->next;
    }
    if (curr != NULL && curr->location == local) {
        return curr;
    }
    Upvalue *created = upvalue_new(vm->in, local);
    created->next = curr;
    if (prev == NULL) {
        vm->open_upvalues = created;
    } else {
        prev->next = created;
    }
    return created;
}

static void close_upvalues(VM *vm, Value *last) {
    while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last) {
        Upvalue *up = vm->open_upvalues;
        up->closed = *up->location;
        up->location = &up->closed;
        vm->open_upvalues = up->next;
    }
}

/* 64 位整型回绕 */
static inline int32_t wrap32(int64_t x) {
    return (int32_t)(uint32_t)(uint64_t)x;
}

/* 用当前指令的行列信息临时构造一个 Node, 供内置函数/错误报告定位使用
 * (VM 里没有 AST 节点, 但行表里保留了 line/col/line_start) */
static Node vm_pos_node(const Chunk *chunk, const CallFrame *frame, const char *file) {
    Node n;
    memset(&n, 0, sizeof n);
    n.kind = N_EXPR_STMT;
    int off = (int)(frame->ip - chunk->code) - 1;
    if (chunk->lines && off >= 0) {
        n.line = chunk->lines[off].line;
        n.col = chunk->lines[off].col;
        n.line_start = chunk->lines[off].line_start;
    }
    n.file = file;
    return n;
}

/* ================= 虚拟机指令主执行引擎 ================= */

static int run_loop_core(VM *vm);

/* 进入一层字节码执行时压入保护块: 原生函数(内置库/回调)里的运行时错误
 * 经 raise_error → longjmp 回到这里, 再转换成 VM 的 try/catch 捕获 */
static int run_loop(VM *vm) {
    VmRecover rec;
    rec.vm = vm;
    rec.entry_try = vm->try_count;
    rec.prev = vm->in->vm_recover;
    vm->in->vm_recover = &rec;

    if (setjmp(rec.jb) != 0) {
        if (vm->try_count > rec.entry_try) {
            /* 本层建立过 try: 恢复栈/帧状态, 再从 catch 起始指令继续执行 */
            VmTryBlock *tb = &vm->try_stack[--vm->try_count];
            vm->frame_count = tb->frame_count;
            vm->stack_top = vm->stack + tb->stack_count;
            vm->open_upvalues = tb->open_upvalues;
            vm->in->temp_root_count = tb->temp_root_count;
            CallFrame *f = &vm->frames[vm->frame_count - 1];
            f->ip = tb->catch_ip;
            push(vm, vm->in->last_error);
            int rc = run_loop_core(vm);
            vm->in->vm_recover = rec.prev;
            return rc;
        }
        /* 本层没有 try: 交回外层保护块继续向上寻找 catch */
        vm->in->vm_recover = rec.prev;
        if (rec.prev) longjmp(rec.prev->jb, 1);
        return 1;
    }

    int rc = run_loop_core(vm);
    vm->in->vm_recover = rec.prev;
    return rc;
}

static int run_loop_core(VM *vm) {
    CallFrame *frame = &vm->frames[vm->frame_count - 1];
    Chunk *chunk = &frame->closure->proto->chunk;

#define READ_BYTE() (*frame->ip++)
#define READ_U16()  (frame->ip += 2, (uint16_t)(frame->ip[-2] | (frame->ip[-1] << 8)))
#define READ_I16()  (frame->ip += 2, (int16_t)(frame->ip[-2] | (frame->ip[-1] << 8)))
#define READ_CONST() (chunk->constants[READ_U16()])
#define CURRENT_LINE() (chunk->lines[frame->ip - chunk->code - 1].line)
#define CURRENT_COL()  (chunk->lines[frame->ip - chunk->code - 1].col)
#define CURRENT_LINE_START() (chunk->lines[frame->ip - chunk->code - 1].line_start)
/* 统一的运行时错误报告: 走 rt_error, 因此能被外层 try/catch (VM 保护块) 捕获 */
#define VM_ERROR(...) do { \
        Node _vm_pos = vm_pos_node(chunk, frame, vm->in->current_file); \
        rt_error(vm->in, &_vm_pos, __VA_ARGS__); \
    } while (0)

    for (;;) {
        uint8_t instruction = READ_BYTE();
        switch ((OpCode)instruction) {
        case OP_NOP:
            break;

        case OP_CONST:
            push(vm, READ_CONST());
            break;

        case OP_NIL:
            push(vm, val_nil());
            break;

        case OP_TRUE:
            push(vm, val_bool(1));
            break;

        case OP_FALSE:
            push(vm, val_bool(0));
            break;

        case OP_INT: {
            uint32_t b0 = READ_BYTE();
            uint32_t b1 = READ_BYTE();
            uint32_t b2 = READ_BYTE();
            uint32_t b3 = READ_BYTE();
            int32_t val = (int32_t)(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
            push(vm, val_int(val));
            break;
        }

        case OP_POP:
            pop(vm);
            break;

        case OP_DUP:
            push(vm, peek(vm, 0));
            break;

        case OP_DEF_GLOBAL: {
            uint16_t idx = READ_U16();
            const char *name = val_get_cstr(chunk->constants[idx]);
            Value val = peek(vm, 0);        /* env_define 可能扩容触发 GC, 值需保持为根 */
            env_define(vm->in->globals, name, val);
            pop(vm);
            break;
        }

        case OP_GET_GLOBAL: {
            uint16_t idx = READ_U16();
            const char *name = val_get_cstr(chunk->constants[idx]);
            Value *val = env_find(vm->in->globals, name);
            if (!val && frame->has_self && strcmp(name, "self") == 0) {
                /* 方法调用中的隐式 self (字节码把 self 当作全局名编译) */
                push(vm, frame->self_val);
                break;
            }
            if (!val) {
                /* 查找内置原生函数 */
                const Native *nt = builtins_find_native(name);
                if (nt) {
                    Value nv = val_nil();
                    nv.type = V_NATIVE;
                    nv.as.native = nt;
                    push(vm, nv);
                    break;
                }
                /* 检查是否在当前局部环境 */
                if (vm->in->env && vm->in->env != vm->in->globals) {
                    val = env_find(vm->in->env, name);
                }
            }
            if (!val) {
                VM_ERROR("未定义的变量 '%s'", name);
            }
            push(vm, *val);
            break;
        }

        case OP_SET_GLOBAL: {
            uint16_t idx = READ_U16();
            const char *name = val_get_cstr(chunk->constants[idx]);
            int is_const = 0;
            Value *val = env_find_with_const(vm->in->globals, name, &is_const);
            if (!val) {
                if (vm->in->env && vm->in->env != vm->in->globals) {
                    val = env_find_with_const(vm->in->env, name, &is_const);
                }
            }
            if (!val) {
                env_define(vm->in->globals, name, peek(vm, 0));
            } else if (is_const) {
                Node pos = vm_pos_node(chunk, frame, vm->in->current_file);
                rt_error(vm->in, &pos, "不能修改常量 '%s'", name);
            } else {
                *val = peek(vm, 0);
            }
            break;
        }

        case OP_GET_LOCAL: {
            uint8_t slot = READ_BYTE();
            push(vm, frame->slots[slot]);
            break;
        }

        case OP_SET_LOCAL: {
            uint8_t slot = READ_BYTE();
            frame->slots[slot] = peek(vm, 0);
            break;
        }

        case OP_GET_UPVALUE: {
            uint8_t index = READ_BYTE();
            push(vm, *frame->closure->upvalues[index]->location);
            break;
        }

        case OP_SET_UPVALUE: {
            uint8_t index = READ_BYTE();
            *frame->closure->upvalues[index]->location = peek(vm, 0);
            break;
        }

        case OP_CLOSE_UPVALUE:
            close_upvalues(vm, vm->stack_top - 1);
            pop(vm);
            break;

        case OP_ADD: {
            /* 注意: 操作数在结果压栈前必须保留在值栈上, 否则 str_new 触发的 GC
             * 会把它们当作垃圾回收 (字符串拼接的经典 use-after-free) */
            Value b = peek(vm, 0);
            Value a = peek(vm, 1);
            if (a.type == V_INT && b.type == V_INT) {
                vm->stack_top -= 2;
                push(vm, val_int(wrap32((int64_t)a.as.i + b.as.i)));
            } else if (val_is_number(a) && val_is_number(b)) {
                vm->stack_top -= 2;
                push(vm, val_num(val_to_double(a) + val_to_double(b)));
            } else if (a.type == V_STR && b.type == V_STR) {
                size_t la = a.as.str ? a.as.str->len : 0;
                size_t lb = b.as.str ? b.as.str->len : 0;
                Str *res = str_new(vm->in, NULL, la + lb);
                if (la) memcpy(res->chars, a.as.str->chars, la);
                if (lb) memcpy(res->chars + la, b.as.str->chars, lb);
                res->chars[la + lb] = '\0';
                vm->stack_top -= 2;
                push(vm, val_str(res));
            } else {
                VM_ERROR("'+' 只能用于两个数字或两个字符串");
            }
            break;
        }

        case OP_SUB: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) {
                push(vm, val_int(wrap32((int64_t)a.as.i - b.as.i)));
            } else if (val_is_number(a) && val_is_number(b)) {
                push(vm, val_num(val_to_double(a) - val_to_double(b)));
            } else {
                VM_ERROR("'-' 需要数字");
            }
            break;
        }

        case OP_MUL: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) {
                push(vm, val_int(wrap32((int64_t)a.as.i * b.as.i)));
            } else if (val_is_number(a) && val_is_number(b)) {
                push(vm, val_num(val_to_double(a) * val_to_double(b)));
            } else {
                VM_ERROR("'*' 需要数字");
            }
            break;
        }

        case OP_DIV: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) {
                if (b.as.i == 0) {
                    VM_ERROR("除数不能为 0");
                }
                push(vm, val_int((int32_t)(a.as.i / b.as.i)));
            } else if (val_is_number(a) && val_is_number(b)) {
                double dy = val_to_double(b);
                if (dy == 0.0) {
                    VM_ERROR("除数不能为 0");
                }
                push(vm, val_num(val_to_double(a) / dy));
            } else {
                VM_ERROR("'/' 需要数字");
            }
            break;
        }

        case OP_MOD: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) {
                if (b.as.i == 0) {
                    VM_ERROR("模数不能为 0");
                }
                push(vm, val_int((int32_t)(a.as.i % b.as.i)));
            } else if (val_is_number(a) && val_is_number(b)) {
                /* 与 AST 解释器保持一致: 浮点取模走 fmod */
                double dy = val_to_double(b);
                if (dy == 0.0) {
                    VM_ERROR("取模的除数不能为 0");
                }
                push(vm, val_num(fmod(val_to_double(a), dy)));
            } else {
                VM_ERROR("'%%' 需要整数");
            }
            break;
        }

        case OP_NEG: {
            Value a = pop(vm);
            if (a.type == V_INT) push(vm, val_int(-a.as.i));
            else if (a.type == V_NUM) push(vm, val_num(-a.as.num));
            else {
                VM_ERROR("取负运算符需要数字");
            }
            break;
        }

        case OP_BIT_AND: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) push(vm, val_int(a.as.i & b.as.i));
            else { VM_ERROR("位运算符 需要整数,实际是 %s", type_name(a)); return 1; }
            break;
        }

        case OP_BIT_OR: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) push(vm, val_int(a.as.i | b.as.i));
            else { VM_ERROR("位运算符 需要整数"); return 1; }
            break;
        }

        case OP_BIT_XOR: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) push(vm, val_int(a.as.i ^ b.as.i));
            else { VM_ERROR("位运算符 需要整数"); return 1; }
            break;
        }

        case OP_BIT_NOT: {
            Value a = pop(vm);
            if (a.type == V_INT) push(vm, val_int(~a.as.i));
            else { VM_ERROR("位取反需要整数"); return 1; }
            break;
        }

        case OP_SHL: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) push(vm, val_int((int32_t)((uint32_t)a.as.i << b.as.i)));
            else { VM_ERROR("移位运算需要整数"); return 1; }
            break;
        }

        case OP_SHR: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) push(vm, val_int(a.as.i >> b.as.i));
            else { VM_ERROR("移位运算需要整数"); return 1; }
            break;
        }

        case OP_NOT:
            push(vm, val_bool(!is_truthy(pop(vm))));
            break;

        case OP_EQ: {
            Value b = pop(vm);
            Value a = pop(vm);
            push(vm, val_bool(values_equal(a, b)));
            break;
        }

        case OP_NE: {
            Value b = pop(vm);
            Value a = pop(vm);
            push(vm, val_bool(!values_equal(a, b)));
            break;
        }

        case OP_LT: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (val_is_number(a) && val_is_number(b)) push(vm, val_bool(val_to_double(a) < val_to_double(b)));
            else if (a.type == V_STR && b.type == V_STR) push(vm, val_bool(strcmp(val_get_cstr(a), val_get_cstr(b)) < 0));
            else { VM_ERROR("比较运算需要数字或字符串"); return 1; }
            break;
        }

        case OP_LE: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (val_is_number(a) && val_is_number(b)) push(vm, val_bool(val_to_double(a) <= val_to_double(b)));
            else if (a.type == V_STR && b.type == V_STR) push(vm, val_bool(strcmp(val_get_cstr(a), val_get_cstr(b)) <= 0));
            else { VM_ERROR("比较运算需要数字或字符串"); return 1; }
            break;
        }

        case OP_GT: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (val_is_number(a) && val_is_number(b)) push(vm, val_bool(val_to_double(a) > val_to_double(b)));
            else if (a.type == V_STR && b.type == V_STR) push(vm, val_bool(strcmp(val_get_cstr(a), val_get_cstr(b)) > 0));
            else { VM_ERROR("比较运算需要数字或字符串"); return 1; }
            break;
        }

        case OP_GE: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (val_is_number(a) && val_is_number(b)) push(vm, val_bool(val_to_double(a) >= val_to_double(b)));
            else if (a.type == V_STR && b.type == V_STR) push(vm, val_bool(strcmp(val_get_cstr(a), val_get_cstr(b)) >= 0));
            else { VM_ERROR("比较运算需要数字或字符串"); return 1; }
            break;
        }

        case OP_JUMP: {
            int16_t offset = READ_I16();
            frame->ip += offset;
            break;
        }

        case OP_JUMP_IF_FALSE: {
            int16_t offset = READ_I16();
            Value val = pop(vm);
            if (!is_truthy(val)) frame->ip += offset;
            break;
        }

        case OP_JUMP_IF_TRUE: {
            int16_t offset = READ_I16();
            Value val = pop(vm);
            if (is_truthy(val)) frame->ip += offset;
            break;
        }

        case OP_JUMP_PEEK_FALSE: {
            int16_t offset = READ_I16();
            if (!is_truthy(peek(vm, 0))) frame->ip += offset;
            break;
        }

        case OP_JUMP_PEEK_TRUE: {
            int16_t offset = READ_I16();
            if (is_truthy(peek(vm, 0))) frame->ip += offset;
            break;
        }

        case OP_LOOP: {
            uint16_t offset = READ_U16();
            frame->ip -= offset;
            break;
        }

        case OP_ARRAY: {
            uint16_t count = READ_U16();
            Array *arr = array_new(vm->in);
            /* 容器本身尚未入栈, 用临时根保护: array_push 扩容失败触发的 GC 会回收它 */
            gc_push_root(vm->in, val_array(arr));
            for (int i = 0; i < count; i++) {
                Value item = peek(vm, count - 1 - i);
                array_push(arr, item);
            }
            gc_pop_roots(vm->in, 1);
            vm->stack_top -= count;
            push(vm, val_array(arr));
            break;
        }

        case OP_MAP: {
            uint16_t count = READ_U16();
            Map *m = map_new(vm->in);
            int total_values = count * 2;
            gc_push_root(vm->in, val_map(m));
            for (int i = 0; i < count; i++) {
                Value key = peek(vm, total_values - 1 - (i * 2));
                Value val = peek(vm, total_values - 1 - (i * 2 + 1));
                if (key.type == V_STR) {
                    map_set(m, key.as.str, val);
                }
            }
            gc_pop_roots(vm->in, 1);
            vm->stack_top -= total_values;
            push(vm, val_map(m));
            break;
        }

        case OP_INDEX_GET: {
            /* 操作数保持入栈: 字符串取下标会分配新字符串, 期间 obj/idx 需要作为 GC 根 */
            Value idx = peek(vm, 0);
            Value obj = peek(vm, 1);
            Value res = val_nil();
            if (obj.type == V_ARRAY) {
                int i = idx.type == V_INT ? idx.as.i : (int)val_to_double(idx);
                if (i < 0 || i >= obj.as.array->count) {
                    VM_ERROR("数组下标越界 (索引 %d, 长度 %d)", i, obj.as.array->count);
                }
                res = obj.as.array->items[i];
            } else if (obj.type == V_MAP) {
                if (idx.type != V_STR) {
                    VM_ERROR("map 键必须是字符串");
                }
                Value *v = map_get(obj.as.map, val_get_cstr(idx));
                res = v ? *v : val_nil();
            } else if (obj.type == V_BYTES) {
                int i = idx.type == V_INT ? idx.as.i : (int)val_to_double(idx);
                if (i < 0 || (size_t)i >= obj.as.bytes->len) {
                    VM_ERROR("bytes 索引越界");
                }
                res = val_int(obj.as.bytes->data[i]);
            } else if (obj.type == V_STR) {
                size_t slen = obj.as.str ? obj.as.str->len : 0;
                int i = idx.type == V_INT ? idx.as.i : (int)val_to_double(idx);
                if (i < 0 || (size_t)i >= slen) {
                    VM_ERROR("字符串下标越界 (索引 %d, 长度 %d)", i, (int)slen);
                }
                Str *s = str_new(vm->in, &obj.as.str->chars[i], 1);
                res = val_str(s);
            } else {
                VM_ERROR("%s 类型不支持下标读取", type_name(obj));
            }
            vm->stack_top -= 2;
            push(vm, res);
            break;
        }

        case OP_INDEX_SET: {
            /* 三个操作数保持入栈状态, 避免 map_set 扩容触发 GC 时被回收 */
            Value val = peek(vm, 0);
            Value idx = peek(vm, 1);
            Value obj = peek(vm, 2);
            if (obj.type == V_ARRAY) {
                int i = idx.type == V_INT ? idx.as.i : (int)val_to_double(idx);
                if (i < 0 || i >= obj.as.array->count) {
                    VM_ERROR("数组下标赋值越界");
                }
                obj.as.array->items[i] = val;
            } else if (obj.type == V_MAP) {
                if (idx.type != V_STR) {
                    VM_ERROR("map 键必须是字符串");
                }
                map_set(obj.as.map, idx.as.str, val);
            } else if (obj.type == V_BYTES) {
                int i = idx.type == V_INT ? idx.as.i : (int)val_to_double(idx);
                if (i < 0 || (size_t)i >= obj.as.bytes->len) {
                    VM_ERROR("bytes 赋值越界");
                }
                int byte_val = val.type == V_INT ? val.as.i : (int)val_to_double(val);
                obj.as.bytes->data[i] = (uint8_t)(byte_val & 0xFF);
            }
            vm->stack_top -= 3;
            push(vm, val);
            break;
        }

        case OP_GET_PROP: {
            uint16_t name_idx = READ_U16();
            const char *prop = val_get_cstr(chunk->constants[name_idx]);
            Value obj = peek(vm, 0);        /* 保持为 GC 根: 方法调用内部可能分配内存 */
            Value res;
            if (obj.type == V_MAP) {
                Value *v = map_get(obj.as.map, prop);
                res = v ? *v : val_nil();
            } else {
                Node pos = vm_pos_node(chunk, frame, vm->in->current_file);
                res = interp_invoke_method(vm->in, &pos, obj, prop, 0, NULL);
            }
            pop(vm);
            push(vm, res);
            break;
        }

        case OP_SET_PROP: {
            uint16_t name_idx = READ_U16();
            const char *prop = val_get_cstr(chunk->constants[name_idx]);
            Value val = peek(vm, 0);
            Value obj = peek(vm, 1);
            if (obj.type == V_MAP) {
                Str *k = str_new(vm->in, prop, strlen(prop));   /* 触发 GC 时 obj/val 仍在栈上 */
                map_set(obj.as.map, k, val);
            }
            vm->stack_top -= 2;
            push(vm, val);
            break;
        }

        case OP_INVOKE: {
            uint16_t name_idx = READ_U16();
            uint8_t argc = READ_BYTE();
            const char *mname = val_get_cstr(chunk->constants[name_idx]);
            Value obj = peek(vm, argc);
            Value *args = vm->stack_top - argc;
            Node pos = vm_pos_node(chunk, frame, vm->in->current_file);
            Value res = interp_invoke_method(vm->in, &pos, obj, mname, argc, args);
            vm->stack_top -= (argc + 1);
            push(vm, res);
            break;
        }

        case OP_CALL: {
            uint8_t argc = READ_BYTE();
            Value callee = peek(vm, argc);

            if (callee.type == V_FN) {
                Func *fn = callee.as.fn;
                if (fn->proto) {
                    if (argc != fn->proto->arity) {
                        VM_ERROR("函数期望 %d 个参数, 实际传入 %d 个", fn->proto->arity, argc);
                    }
                    if (vm->frame_count >= L_VM_FRAMES_MAX) {
                        fprintf(stderr, "虚拟机错误: 函数调用深度超限 (栈帧溢出)\n");
                        return 1;
                    }
                    CallFrame *next_frame = &vm->frames[vm->frame_count++];
                    next_frame->closure = fn;
                    next_frame->ip = fn->proto->chunk.code;
                    next_frame->slots = vm->stack_top - argc - 1;
                    next_frame->has_self = 0;
                    next_frame->self_val = val_nil();
                    frame = next_frame;
                    chunk = &frame->closure->proto->chunk;
                    break;
                }
            }

            if (callee.type == V_NATIVE) {
                NativeFn native_fn = callee.as.native->fn;
                Value *args = vm->stack_top - argc;
                Node pos = vm_pos_node(chunk, frame, vm->in->current_file);
                Value res = native_fn(vm->in, &pos, argc, args);
                vm->stack_top -= (argc + 1);
                push(vm, res);
                break;
            }

            /* 如果是其它类型，委托给通用 interp_call */
            Value *args = vm->stack_top - argc;
            Node pos = vm_pos_node(chunk, frame, vm->in->current_file);
            Value res = interp_call(vm->in, &pos, callee, argc, args);
            vm->stack_top -= (argc + 1);
            push(vm, res);
            break;
        }

        case OP_RETURN: {
            Value result = pop(vm);
            close_upvalues(vm, frame->slots);
            vm->frame_count--;
            vm->stack_top = frame->slots;
            push(vm, result);
            if (vm->frame_count <= vm->frame_base) return 0;  /* 顶层或嵌套调用的返回点 */
            frame = &vm->frames[vm->frame_count - 1];
            chunk = &frame->closure->proto->chunk;
            break;
        }

        case OP_CLOSURE: {
            uint16_t proto_idx = READ_U16();
            Value proto_val = chunk->constants[proto_idx];
            Func *proto_fn = proto_val.as.fn;
            Proto *sub_proto = proto_fn->proto;

            Func *closure = func_proto_new(vm->in, sub_proto);
            /* 闭包尚未入栈, capture_upvalue 的分配可能触发 GC, 先临时根保护 */
            gc_push_root(vm->in, val_fn(closure));
            for (int i = 0; i < sub_proto->upvalue_count; i++) {
                uint8_t is_local = READ_BYTE();
                uint8_t index = READ_BYTE();
                if (is_local) {
                    closure->upvalues[i] = capture_upvalue(vm, frame->slots + index);
                } else {
                    closure->upvalues[i] = frame->closure->upvalues[index];
                }
            }
            gc_pop_roots(vm->in, 1);
            push(vm, val_fn(closure));
            break;
        }

        case OP_TRY: {
            int16_t offset = READ_I16();
            if (vm->try_count >= 32) {
                fprintf(stderr, "try 保护嵌套过深\n");
                return 1;
            }
            VmTryBlock *tb = &vm->try_stack[vm->try_count++];
            tb->catch_ip = frame->ip + offset;
            tb->frame_count = vm->frame_count;
            tb->stack_count = (int)(vm->stack_top - vm->stack);
            tb->temp_root_count = vm->in->temp_root_count;
            tb->open_upvalues = vm->open_upvalues;
            break;
        }

        case OP_END_TRY:
            if (vm->try_count > 0) vm->try_count--;
            break;

        case OP_THROW: {
            Value err = pop(vm);
            /* 与 AST 解释器共用抛出逻辑: 构造带定位/栈回溯的异常对象,
             * 由 raise_error 决定跳回最近的 VM 保护块还是打印后中止 */
            Node pos = vm_pos_node(chunk, frame, vm->in->current_file);
            interp_throw_value(vm->in, &pos, err);
            break;  /* 不会执行到这里 */
        }

        case OP_DEF_GLOBAL_CONST: {
            uint16_t idx = READ_U16();
            const char *name = val_get_cstr(chunk->constants[idx]);
            Value val = peek(vm, 0);
            env_define_const(vm->in->globals, name, val, 1);
            pop(vm);
            break;
        }

        case OP_CONST_REASSIGN: {
            uint16_t name_idx = READ_U16();
            const char *name = val_get_cstr(chunk->constants[name_idx]);
            Node pos = vm_pos_node(chunk, frame, vm->in->current_file);
            rt_error(vm->in, &pos, "不能修改常量 '%s'", name);
            break;  /* 不会执行到这里 */
        }

        case OP_ECHO: {
            Value v = pop(vm);
            if (v.type != V_NIL) {
                val_print(v);
                printf("\n");
            }
            break;
        }

        case OP_HALT:
            return 0;

        default:
            fprintf(stderr, "未知字节码操作码: %d\n", instruction);
            return 1;
        }
    }

#undef READ_BYTE
#undef READ_U16
#undef READ_I16
#undef READ_CONST
#undef CURRENT_LINE
#undef CURRENT_COL
}

int vm_run_proto(VM *vm, Proto *proto, int echo) {
    Func *main_closure = func_proto_new(vm->in, proto);
    vm->echo = echo;

    /* 压入顶层闭包并初始化顶层调用帧 */
    push(vm, val_fn(main_closure));
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->closure = main_closure;
    frame->ip = proto->chunk.code;
    frame->slots = vm->stack;
    frame->has_self = 0;
    frame->self_val = val_nil();
    vm->frame_base = 0;

    VM *saved_active = s_active_vm;
    s_active_vm = vm;
    int rc = run_loop(vm);
    s_active_vm = saved_active;
    return rc;
}

Value vm_call_closure_self(Interp *in, const Node *at, Func *fn, int argc, Value *argv,
                           int has_self, Value self_val) {
    VM *vm = s_active_vm;
    if (!vm || !fn->proto)
        rt_error(in, at, "当前没有活动的虚拟机, 无法调用字节码闭包");
    Proto *p = fn->proto;
    /* 方法调用: 形参比实参多一个时自动补入 self (对应 fn(self, ...) 显式写法) */
    int prepend_self = (has_self && p->arity == argc + 1);
    if (argc + (prepend_self ? 1 : 0) != p->arity)
        rt_error(in, at, "函数期望 %d 个参数, 实际传入 %d 个", p->arity,
                 argc + (prepend_self ? 1 : 0));
    if (vm->frame_count >= L_VM_FRAMES_MAX)
        rt_error(in, at, "虚拟机错误: 函数调用深度超限 (栈帧溢出)");

    Value *base = vm->stack_top;
    push(vm, val_fn(fn));               /* slot 0: 闭包自身 */
    if (prepend_self) push(vm, self_val);
    for (int i = 0; i < argc; i++) push(vm, argv[i]);

    int saved_base = vm->frame_base;
    CallFrame *nf = &vm->frames[vm->frame_count++];
    nf->closure = fn;
    nf->ip = p->chunk.code;
    nf->slots = base;
    nf->has_self = has_self;
    nf->self_val = has_self ? self_val : val_nil();
    vm->frame_base = vm->frame_count - 1;

    int rc = run_loop(vm);
    vm->frame_base = saved_base;
    if (rc != 0) {                      /* 出错时已 longjmp 到 run_loop_guarded, 正常不会到这里 */
        vm->stack_top = base;
        return val_nil();
    }
    Value result = pop(vm);
    vm->stack_top = base;
    return result;
}

Value vm_call_closure(Interp *in, const Node *at, Func *fn, int argc, Value *argv) {
    return vm_call_closure_self(in, at, fn, argc, argv, 0, val_nil());
}

/* ================= 字节码文件序列化与反序列化 (.lc) ================= */

static int dump_proto_inner(Proto *proto, FILE *fp) {
    uint16_t arity = (uint16_t)proto->arity;
    uint16_t upvalues = (uint16_t)proto->upvalue_count;
    fwrite(&arity, 2, 1, fp);
    fwrite(&upvalues, 2, 1, fp);

    /* 1. 常量池 */
    uint16_t const_cnt = (uint16_t)proto->chunk.const_count;
    fwrite(&const_cnt, 2, 1, fp);
    for (int i = 0; i < const_cnt; i++) {
        Value v = proto->chunk.constants[i];
        if (v.type == V_NIL) {
            uint8_t tag = 0;
            fwrite(&tag, 1, 1, fp);
        } else if (v.type == V_BOOL) {
            uint8_t tag = 1;
            uint8_t b = v.as.boolean ? 1 : 0;
            fwrite(&tag, 1, 1, fp);
            fwrite(&b, 1, 1, fp);
        } else if (v.type == V_INT) {
            uint8_t tag = 2;
            int64_t val = (int64_t)v.as.i;
            fwrite(&tag, 1, 1, fp);
            fwrite(&val, 8, 1, fp);
        } else if (v.type == V_NUM) {
            uint8_t tag = 3;
            double val = v.as.num;
            fwrite(&tag, 1, 1, fp);
            fwrite(&val, 8, 1, fp);
        } else if (v.type == V_STR) {
            uint8_t tag = 4;
            const char *s = val_get_cstr(v);
            uint16_t slen = (uint16_t)strlen(s);
            fwrite(&tag, 1, 1, fp);
            fwrite(&slen, 2, 1, fp);
            fwrite(s, 1, slen, fp);
        } else if (v.type == V_FN && v.as.fn && v.as.fn->proto) {
            uint8_t tag = 5;
            fwrite(&tag, 1, 1, fp);
            dump_proto_inner(v.as.fn->proto, fp);
        } else {
            uint8_t tag = 0;
            fwrite(&tag, 1, 1, fp);
        }
    }

    /* 2. 字节码指令 */
    uint32_t code_len = (uint32_t)proto->chunk.count;
    fwrite(&code_len, 4, 1, fp);
    if (code_len > 0) {
        fwrite(proto->chunk.code, 1, code_len, fp);
    }

    /* 3. 行号信息 */
    if (code_len > 0) {
        for (uint32_t i = 0; i < code_len; i++) {
            uint16_t line = (uint16_t)(proto->chunk.lines ? proto->chunk.lines[i].line : 1);
            fwrite(&line, 2, 1, fp);
        }
    }

    return 0;
}

int vm_dump_proto_to_file(Proto *proto, const char *filepath) {
    FILE *fp = fopen(filepath, "wb");
    if (!fp) return -1;

    uint32_t magic = L_SCRIPT_MAGIC_BC; /* "LBC1" */
    fwrite(&magic, 4, 1, fp);

    int rc = dump_proto_inner(proto, fp);
    fclose(fp);
    return rc;
}

static Proto *load_proto_inner(Interp *in, const uint8_t **cursor, const uint8_t *end) {
    if (*cursor + 4 > end) return NULL;
    uint16_t arity = (uint16_t)((*cursor)[0] | ((*cursor)[1] << 8));
    uint16_t upvalues = (uint16_t)((*cursor)[2] | ((*cursor)[3] << 8));
    *cursor += 4;

    Proto *p = proto_new(in, "<proto>", "<bytecode>");
    p->arity = arity;
    p->upvalue_count = upvalues;

    /* 1. 常量池 */
    if (*cursor + 2 > end) return NULL;
    uint16_t const_cnt = (uint16_t)((*cursor)[0] | ((*cursor)[1] << 8));
    *cursor += 2;

    for (int i = 0; i < const_cnt; i++) {
        if (*cursor >= end) return NULL;
        uint8_t tag = *(*cursor)++;
        if (tag == 0) {
            chunk_add_constant(&p->chunk, val_nil());
        } else if (tag == 1) {
            if (*cursor >= end) return NULL;
            uint8_t b = *(*cursor)++;
            chunk_add_constant(&p->chunk, val_bool(b));
        } else if (tag == 2) {
            if (*cursor + 8 > end) return NULL;
            int64_t val;
            memcpy(&val, *cursor, 8);
            *cursor += 8;
            chunk_add_constant(&p->chunk, val_int((long long)val));
        } else if (tag == 3) {
            if (*cursor + 8 > end) return NULL;
            double val;
            memcpy(&val, *cursor, 8);
            *cursor += 8;
            chunk_add_constant(&p->chunk, val_num(val));
        } else if (tag == 4) {
            if (*cursor + 2 > end) return NULL;
            uint16_t slen = (uint16_t)((*cursor)[0] | ((*cursor)[1] << 8));
            *cursor += 2;
            if (*cursor + slen > end) return NULL;
            char *s = malloc(slen + 1);
            memcpy(s, *cursor, slen);
            s[slen] = '\0';
            *cursor += slen;
            Value str_val = val_str(str_new(in, s, slen));
            free(s);
            chunk_add_constant(&p->chunk, str_val);
        } else if (tag == 5) {
            Proto *sub = load_proto_inner(in, cursor, end);
            if (!sub) return NULL;
            Func *fn = func_proto_new(in, sub);
            chunk_add_constant(&p->chunk, val_fn(fn));
        }
    }

    /* 2. 字节码指令 */
    if (*cursor + 4 > end) return NULL;
    uint32_t code_len = (uint32_t)((*cursor)[0] | ((*cursor)[1] << 8) | ((*cursor)[2] << 16) | ((*cursor)[3] << 24));
    *cursor += 4;
    if (*cursor + code_len > end) return NULL;

    const uint8_t *code_ptr = *cursor;
    *cursor += code_len;

    /* 3. 行号信息 */
    if (*cursor + (code_len * 2) > end) {
        for (uint32_t i = 0; i < code_len; i++) {
            chunk_write(&p->chunk, code_ptr[i], 1, 1, NULL);
        }
    } else {
        for (uint32_t i = 0; i < code_len; i++) {
            uint16_t line = (uint16_t)((*cursor)[0] | ((*cursor)[1] << 8));
            *cursor += 2;
            chunk_write(&p->chunk, code_ptr[i], line, 1, NULL);
        }
    }

    return p;
}

Proto *vm_load_proto_from_buffer(Interp *in, const uint8_t *buf, size_t len) {
    if (len < 4) return NULL;
    uint32_t magic = (uint32_t)(buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
    if (magic != L_SCRIPT_MAGIC_BC) return NULL;

    const uint8_t *cursor = buf + 4;
    const uint8_t *end = buf + len;
    return load_proto_inner(in, &cursor, end);
}

Proto *vm_load_proto_from_file(Interp *in, const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t *buf = malloc(sz);
    if (!buf) { fclose(fp); return NULL; }
    fread(buf, 1, sz, fp);
    fclose(fp);

    Proto *p = vm_load_proto_from_buffer(in, buf, sz);
    free(buf);
    return p;
}
