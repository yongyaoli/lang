#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void vm_init(VM *vm, Interp *in) {
    vm->in = in;
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->open_upvalues = NULL;
    vm->try_count = 0;
    vm->echo = 0;
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

/* ================= 虚拟机指令主执行引擎 ================= */

static int run_loop(VM *vm) {
    CallFrame *frame = &vm->frames[vm->frame_count - 1];
    Chunk *chunk = &frame->closure->proto->chunk;

#define READ_BYTE() (*frame->ip++)
#define READ_U16()  (frame->ip += 2, (uint16_t)(frame->ip[-2] | (frame->ip[-1] << 8)))
#define READ_I16()  (frame->ip += 2, (int16_t)(frame->ip[-2] | (frame->ip[-1] << 8)))
#define READ_CONST() (chunk->constants[READ_U16()])
#define CURRENT_LINE() (chunk->lines[frame->ip - chunk->code - 1].line)
#define CURRENT_COL()  (chunk->lines[frame->ip - chunk->code - 1].col)

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
            Value val = pop(vm);
            env_define(vm->in->globals, name, val);
            break;
        }

        case OP_GET_GLOBAL: {
            uint16_t idx = READ_U16();
            const char *name = val_get_cstr(chunk->constants[idx]);
            Value *val = env_find(vm->in->globals, name);
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
                fprintf(stderr, "[%d:%d] 运行时错误: 未定义的变量 '%s'\n", CURRENT_LINE(), CURRENT_COL(), name);
                return 1;
            }
            push(vm, *val);
            break;
        }

        case OP_SET_GLOBAL: {
            uint16_t idx = READ_U16();
            const char *name = val_get_cstr(chunk->constants[idx]);
            Value *val = env_find(vm->in->globals, name);
            if (!val) {
                if (vm->in->env && vm->in->env != vm->in->globals) {
                    val = env_find(vm->in->env, name);
                }
            }
            if (!val) {
                env_define(vm->in->globals, name, peek(vm, 0));
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
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) {
                push(vm, val_int(wrap32((int64_t)a.as.i + b.as.i)));
            } else if (val_is_number(a) && val_is_number(b)) {
                push(vm, val_num(val_to_double(a) + val_to_double(b)));
            } else if (a.type == V_STR && b.type == V_STR) {
                size_t la = a.as.str ? a.as.str->len : 0;
                size_t lb = b.as.str ? b.as.str->len : 0;
                Str *res = str_new(vm->in, NULL, la + lb);
                if (la) memcpy(res->chars, a.as.str->chars, la);
                if (lb) memcpy(res->chars + la, b.as.str->chars, lb);
                res->chars[la + lb] = '\0';
                push(vm, val_str(res));
            } else {
                fprintf(stderr, "[%d:%d] 运行时错误: '+' 只能用于两个数字或两个字符串\n", CURRENT_LINE(), CURRENT_COL());
                return 1;
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
                fprintf(stderr, "[%d:%d] 运行时错误: '-' 需要数字\n", CURRENT_LINE(), CURRENT_COL());
                return 1;
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
                fprintf(stderr, "[%d:%d] 运行时错误: '*' 需要数字\n", CURRENT_LINE(), CURRENT_COL());
                return 1;
            }
            break;
        }

        case OP_DIV: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) {
                if (b.as.i == 0) {
                    fprintf(stderr, "[%d:%d] 运行时错误: 除数不能为 0\n", CURRENT_LINE(), CURRENT_COL());
                    return 1;
                }
                push(vm, val_int((int32_t)(a.as.i / b.as.i)));
            } else if (val_is_number(a) && val_is_number(b)) {
                double dy = val_to_double(b);
                if (dy == 0.0) {
                    fprintf(stderr, "[%d:%d] 运行时错误: 除数不能为 0\n", CURRENT_LINE(), CURRENT_COL());
                    return 1;
                }
                push(vm, val_num(val_to_double(a) / dy));
            } else {
                fprintf(stderr, "[%d:%d] 运行时错误: '/' 需要数字\n", CURRENT_LINE(), CURRENT_COL());
                return 1;
            }
            break;
        }

        case OP_MOD: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) {
                if (b.as.i == 0) {
                    fprintf(stderr, "[%d:%d] 运行时错误: 模数不能为 0\n", CURRENT_LINE(), CURRENT_COL());
                    return 1;
                }
                push(vm, val_int((int32_t)(a.as.i % b.as.i)));
            } else {
                fprintf(stderr, "[%d:%d] 运行时错误: '%%' 需要整数\n", CURRENT_LINE(), CURRENT_COL());
                return 1;
            }
            break;
        }

        case OP_NEG: {
            Value a = pop(vm);
            if (a.type == V_INT) push(vm, val_int(-a.as.i));
            else if (a.type == V_NUM) push(vm, val_num(-a.as.num));
            else {
                fprintf(stderr, "[%d:%d] 运行时错误: 取负运算符需要数字\n", CURRENT_LINE(), CURRENT_COL());
                return 1;
            }
            break;
        }

        case OP_BIT_AND: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) push(vm, val_int(a.as.i & b.as.i));
            else { fprintf(stderr, "[%d:%d] 运行时错误: 位运算符 需要整数,实际是 %s\n", CURRENT_LINE(), CURRENT_COL(), type_name(a)); return 1; }
            break;
        }

        case OP_BIT_OR: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) push(vm, val_int(a.as.i | b.as.i));
            else { fprintf(stderr, "[%d:%d] 运行时错误: 位运算符 需要整数\n", CURRENT_LINE(), CURRENT_COL()); return 1; }
            break;
        }

        case OP_BIT_XOR: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) push(vm, val_int(a.as.i ^ b.as.i));
            else { fprintf(stderr, "[%d:%d] 运行时错误: 位运算符 需要整数\n", CURRENT_LINE(), CURRENT_COL()); return 1; }
            break;
        }

        case OP_BIT_NOT: {
            Value a = pop(vm);
            if (a.type == V_INT) push(vm, val_int(~a.as.i));
            else { fprintf(stderr, "[%d:%d] 运行时错误: 位取反需要整数\n", CURRENT_LINE(), CURRENT_COL()); return 1; }
            break;
        }

        case OP_SHL: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) push(vm, val_int((int32_t)((uint32_t)a.as.i << b.as.i)));
            else { fprintf(stderr, "[%d:%d] 运行时错误: 移位运算需要整数\n", CURRENT_LINE(), CURRENT_COL()); return 1; }
            break;
        }

        case OP_SHR: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (a.type == V_INT && b.type == V_INT) push(vm, val_int(a.as.i >> b.as.i));
            else { fprintf(stderr, "[%d:%d] 运行时错误: 移位运算需要整数\n", CURRENT_LINE(), CURRENT_COL()); return 1; }
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
            else { fprintf(stderr, "[%d:%d] 运行时错误: 比较运算需要数字或字符串\n", CURRENT_LINE(), CURRENT_COL()); return 1; }
            break;
        }

        case OP_LE: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (val_is_number(a) && val_is_number(b)) push(vm, val_bool(val_to_double(a) <= val_to_double(b)));
            else if (a.type == V_STR && b.type == V_STR) push(vm, val_bool(strcmp(val_get_cstr(a), val_get_cstr(b)) <= 0));
            else { fprintf(stderr, "[%d:%d] 运行时错误: 比较运算需要数字或字符串\n", CURRENT_LINE(), CURRENT_COL()); return 1; }
            break;
        }

        case OP_GT: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (val_is_number(a) && val_is_number(b)) push(vm, val_bool(val_to_double(a) > val_to_double(b)));
            else if (a.type == V_STR && b.type == V_STR) push(vm, val_bool(strcmp(val_get_cstr(a), val_get_cstr(b)) > 0));
            else { fprintf(stderr, "[%d:%d] 运行时错误: 比较运算需要数字或字符串\n", CURRENT_LINE(), CURRENT_COL()); return 1; }
            break;
        }

        case OP_GE: {
            Value b = pop(vm);
            Value a = pop(vm);
            if (val_is_number(a) && val_is_number(b)) push(vm, val_bool(val_to_double(a) >= val_to_double(b)));
            else if (a.type == V_STR && b.type == V_STR) push(vm, val_bool(strcmp(val_get_cstr(a), val_get_cstr(b)) >= 0));
            else { fprintf(stderr, "[%d:%d] 运行时错误: 比较运算需要数字或字符串\n", CURRENT_LINE(), CURRENT_COL()); return 1; }
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
            for (int i = 0; i < count; i++) {
                Value item = peek(vm, count - 1 - i);
                array_push(arr, item);
            }
            vm->stack_top -= count;
            push(vm, val_array(arr));
            break;
        }

        case OP_MAP: {
            uint16_t count = READ_U16();
            Map *m = map_new(vm->in);
            int total_values = count * 2;
            for (int i = 0; i < count; i++) {
                Value key = peek(vm, total_values - 1 - (i * 2));
                Value val = peek(vm, total_values - 1 - (i * 2 + 1));
                if (key.type == V_STR) {
                    map_set(m, key.as.str, val);
                }
            }
            vm->stack_top -= total_values;
            push(vm, val_map(m));
            break;
        }

        case OP_INDEX_GET: {
            Value idx = pop(vm);
            Value obj = pop(vm);
            if (obj.type == V_ARRAY) {
                int i = idx.type == V_INT ? idx.as.i : (int)val_to_double(idx);
                if (i < 0 || i >= obj.as.array->count) {
                    fprintf(stderr, "[%d:%d] 运行时错误: 数组下标越界 (索引 %d, 长度 %d)\n", CURRENT_LINE(), CURRENT_COL(), i, obj.as.array->count);
                    return 1;
                }
                push(vm, obj.as.array->items[i]);
            } else if (obj.type == V_MAP) {
                if (idx.type != V_STR) {
                    fprintf(stderr, "[%d:%d] 运行时错误: map 键必须是字符串\n", CURRENT_LINE(), CURRENT_COL());
                    return 1;
                }
                Value *v = map_get(obj.as.map, val_get_cstr(idx));
                push(vm, v ? *v : val_nil());
            } else if (obj.type == V_BYTES) {
                int i = idx.type == V_INT ? idx.as.i : (int)val_to_double(idx);
                if (i < 0 || (size_t)i >= obj.as.bytes->len) {
                    fprintf(stderr, "[%d:%d] 运行时错误: bytes 索引越界\n", CURRENT_LINE(), CURRENT_COL());
                    return 1;
                }
                push(vm, val_int(obj.as.bytes->data[i]));
            } else {
                fprintf(stderr, "[%d:%d] 运行时错误: %s 类型不支持下标读取\n", CURRENT_LINE(), CURRENT_COL(), type_name(obj));
                return 1;
            }
            break;
        }

        case OP_INDEX_SET: {
            Value val = pop(vm);
            Value idx = pop(vm);
            Value obj = pop(vm);
            if (obj.type == V_ARRAY) {
                int i = idx.type == V_INT ? idx.as.i : (int)val_to_double(idx);
                if (i < 0 || i >= obj.as.array->count) {
                    fprintf(stderr, "[%d:%d] 运行时错误: 数组下标赋值越界\n", CURRENT_LINE(), CURRENT_COL());
                    return 1;
                }
                obj.as.array->items[i] = val;
            } else if (obj.type == V_MAP) {
                if (idx.type != V_STR) {
                    fprintf(stderr, "[%d:%d] 运行时错误: map 键必须是字符串\n", CURRENT_LINE(), CURRENT_COL());
                    return 1;
                }
                map_set(obj.as.map, idx.as.str, val);
            } else if (obj.type == V_BYTES) {
                int i = idx.type == V_INT ? idx.as.i : (int)val_to_double(idx);
                if (i < 0 || (size_t)i >= obj.as.bytes->len) {
                    fprintf(stderr, "[%d:%d] 运行时错误: bytes 赋值越界\n", CURRENT_LINE(), CURRENT_COL());
                    return 1;
                }
                int byte_val = val.type == V_INT ? val.as.i : (int)val_to_double(val);
                obj.as.bytes->data[i] = (uint8_t)(byte_val & 0xFF);
            }
            push(vm, val);
            break;
        }

        case OP_GET_PROP: {
            uint16_t name_idx = READ_U16();
            const char *prop = val_get_cstr(chunk->constants[name_idx]);
            Value obj = pop(vm);
            if (obj.type == V_MAP) {
                Value *v = map_get(obj.as.map, prop);
                push(vm, v ? *v : val_nil());
            } else {
                push(vm, interp_invoke_method(vm->in, NULL, obj, prop, 0, NULL));
            }
            break;
        }

        case OP_SET_PROP: {
            uint16_t name_idx = READ_U16();
            const char *prop = val_get_cstr(chunk->constants[name_idx]);
            Value val = pop(vm);
            Value obj = pop(vm);
            if (obj.type == V_MAP) {
                Str *k = str_new(vm->in, prop, strlen(prop));
                map_set(obj.as.map, k, val);
            }
            push(vm, val);
            break;
        }

        case OP_INVOKE: {
            uint16_t name_idx = READ_U16();
            uint8_t argc = READ_BYTE();
            const char *mname = val_get_cstr(chunk->constants[name_idx]);
            Value obj = peek(vm, argc);
            Value *args = vm->stack_top - argc;
            Value res = interp_invoke_method(vm->in, NULL, obj, mname, argc, args);
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
                        fprintf(stderr, "[%d:%d] 运行时错误: 函数期望 %d 个参数, 实际传入 %d 个\n", CURRENT_LINE(), CURRENT_COL(), fn->proto->arity, argc);
                        return 1;
                    }
                    if (vm->frame_count >= L_VM_FRAMES_MAX) {
                        fprintf(stderr, "虚拟机错误: 函数调用深度超限 (栈帧溢出)\n");
                        return 1;
                    }
                    CallFrame *next_frame = &vm->frames[vm->frame_count++];
                    next_frame->closure = fn;
                    next_frame->ip = fn->proto->chunk.code;
                    next_frame->slots = vm->stack_top - argc - 1;
                    frame = next_frame;
                    chunk = &frame->closure->proto->chunk;
                    break;
                }
            }

            if (callee.type == V_NATIVE) {
                NativeFn native_fn = callee.as.native->fn;
                Value *args = vm->stack_top - argc;
                Value res = native_fn(vm->in, NULL, argc, args);
                vm->stack_top -= (argc + 1);
                push(vm, res);
                break;
            }

            /* 如果是其它类型，委托给通用 interp_call */
            Value *args = vm->stack_top - argc;
            Value res = interp_call(vm->in, NULL, callee, argc, args);
            vm->stack_top -= (argc + 1);
            push(vm, res);
            break;
        }

        case OP_RETURN: {
            Value result = pop(vm);
            close_upvalues(vm, frame->slots);
            vm->frame_count--;
            if (vm->frame_count == 0) {
                push(vm, result);
                return 0;
            }
            vm->stack_top = frame->slots;
            push(vm, result);
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
            for (int i = 0; i < sub_proto->upvalue_count; i++) {
                uint8_t is_local = READ_BYTE();
                uint8_t index = READ_BYTE();
                if (is_local) {
                    closure->upvalues[i] = capture_upvalue(vm, frame->slots + index);
                } else {
                    closure->upvalues[i] = frame->closure->upvalues[index];
                }
            }
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
            tb->open_upvalues = vm->open_upvalues;
            break;
        }

        case OP_END_TRY:
            if (vm->try_count > 0) vm->try_count--;
            break;

        case OP_THROW: {
            Value err = pop(vm);
            if (vm->try_count > 0) {
                VmTryBlock *tb = &vm->try_stack[--vm->try_count];
                vm->frame_count = tb->frame_count;
                frame = &vm->frames[vm->frame_count - 1];
                chunk = &frame->closure->proto->chunk;
                frame->ip = tb->catch_ip;
                vm->stack_top = vm->stack + tb->stack_count;
                push(vm, err);
                break;
            }
            fprintf(stderr, "[%d:%d] 抛出未捕获的异常: ", CURRENT_LINE(), CURRENT_COL());
            val_print(err);
            fprintf(stderr, "\n");
            return 1;
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

    return run_loop(vm);
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
            chunk_write(&p->chunk, code_ptr[i], 1, 1);
        }
    } else {
        for (uint32_t i = 0; i < code_len; i++) {
            uint16_t line = (uint16_t)((*cursor)[0] | ((*cursor)[1] << 8));
            *cursor += 2;
            chunk_write(&p->chunk, code_ptr[i], line, 1);
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
