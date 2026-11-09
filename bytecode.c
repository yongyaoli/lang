#include "bytecode.h"
#include "interp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void chunk_init(Chunk *c) {
    c->code = NULL;
    c->count = 0;
    c->cap = 0;
    c->lines = NULL;
    c->constants = NULL;
    c->const_count = 0;
    c->const_cap = 0;
}

void chunk_free(Chunk *c) {
    free(c->code);
    free(c->lines);
    free(c->constants);
    chunk_init(c);
}

void chunk_write(Chunk *c, uint8_t byte, int line, int col) {
    if (c->count + 1 > c->cap) {
        int old_cap = c->cap;
        c->cap = old_cap < 16 ? 16 : old_cap * 2;
        c->code = realloc(c->code, c->cap * sizeof(uint8_t));
        c->lines = realloc(c->lines, c->cap * sizeof(LineInfo));
    }
    c->code[c->count] = byte;
    c->lines[c->count].line = line;
    c->lines[c->count].col = col;
    c->count++;
}

void chunk_write_u16(Chunk *c, uint16_t u16, int line, int col) {
    chunk_write(c, (uint8_t)(u16 & 0xFF), line, col);
    chunk_write(c, (uint8_t)((u16 >> 8) & 0xFF), line, col);
}

void chunk_write_i16(Chunk *c, int16_t i16, int line, int col) {
    chunk_write_u16(c, (uint16_t)i16, line, col);
}

int chunk_add_constant(Chunk *c, Value val) {
    /* 检查是否存在相同常量，复用常量池条目 */
    for (int i = 0; i < c->const_count; i++) {
        Value cur = c->constants[i];
        if (cur.type == val.type) {
            if (cur.type == V_INT && cur.as.i == val.as.i) return i;
            if (cur.type == V_NUM && cur.as.num == val.as.num) return i;
            if (cur.type == V_STR && cur.as.str == val.as.str) return i;
        }
    }
    if (c->const_count + 1 > c->const_cap) {
        int old_cap = c->const_cap;
        c->const_cap = old_cap < 8 ? 8 : old_cap * 2;
        c->constants = realloc(c->constants, c->const_cap * sizeof(Value));
    }
    c->constants[c->const_count] = val;
    return c->const_count++;
}

Proto *proto_new(Interp *in, const char *name, const char *source_file) {
    Proto *p = (Proto *)gc_alloc(in, sizeof(Proto), OBJ_PROTO);
    p->name = name ? strdup(name) : NULL;
    p->source_file = source_file ? strdup(source_file) : NULL;
    p->arity = 0;
    p->is_vararg = 0;
    chunk_init(&p->chunk);
    p->upvalues = NULL;
    p->upvalue_count = 0;
    p->upvalue_cap = 0;
    return p;
}

Upvalue *upvalue_new(Interp *in, Value *slot) {
    Upvalue *up = (Upvalue *)gc_alloc(in, sizeof(Upvalue), OBJ_UPVALUE);
    up->location = slot;
    up->closed = val_nil();
    up->next = NULL;
    return up;
}

/* ================= 反汇编打印 ================= */

static int simple_instruction(const char *name, int offset) {
    printf("%-16s\n", name);
    return offset + 1;
}

static int byte_instruction(const char *name, const Chunk *c, int offset) {
    uint8_t slot = c->code[offset + 1];
    printf("%-16s %4d\n", name, (int)slot);
    return offset + 2;
}

static int jump_instruction(const char *name, int sign, const Chunk *c, int offset) {
    uint16_t lo = c->code[offset + 1];
    uint16_t hi = c->code[offset + 2];
    int16_t jump = (int16_t)(lo | (hi << 8));
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

static int constant_instruction(const char *name, const Chunk *c, int offset) {
    uint16_t lo = c->code[offset + 1];
    uint16_t hi = c->code[offset + 2];
    uint16_t idx = (uint16_t)(lo | (hi << 8));
    printf("%-16s %4d '", name, (int)idx);
    if (idx < (uint16_t)c->const_count) {
        val_print(c->constants[idx]);
    }
    printf("'\n");
    return offset + 3;
}

int chunk_disassemble_instruction(const Chunk *c, int offset) {
    printf("%04d ", offset);
    if (offset > 0 && c->lines[offset].line == c->lines[offset - 1].line) {
        printf("   | ");
    } else {
        printf("%4d ", c->lines[offset].line);
    }

    uint8_t op = c->code[offset];
    switch ((OpCode)op) {
        case OP_NOP:            return simple_instruction("OP_NOP", offset);
        case OP_CONST:          return constant_instruction("OP_CONST", c, offset);
        case OP_NIL:            return simple_instruction("OP_NIL", offset);
        case OP_TRUE:           return simple_instruction("OP_TRUE", offset);
        case OP_FALSE:          return simple_instruction("OP_FALSE", offset);
        case OP_POP:            return simple_instruction("OP_POP", offset);
        case OP_DUP:            return simple_instruction("OP_DUP", offset);
        case OP_DEF_GLOBAL:     return constant_instruction("OP_DEF_GLOBAL", c, offset);
        case OP_GET_GLOBAL:     return constant_instruction("OP_GET_GLOBAL", c, offset);
        case OP_SET_GLOBAL:     return constant_instruction("OP_SET_GLOBAL", c, offset);
        case OP_GET_LOCAL:      return byte_instruction("OP_GET_LOCAL", c, offset);
        case OP_SET_LOCAL:      return byte_instruction("OP_SET_LOCAL", c, offset);
        case OP_GET_UPVALUE:    return byte_instruction("OP_GET_UPVALUE", c, offset);
        case OP_SET_UPVALUE:    return byte_instruction("OP_SET_UPVALUE", c, offset);
        case OP_CLOSE_UPVALUE:  return simple_instruction("OP_CLOSE_UPVALUE", offset);
        case OP_ADD:            return simple_instruction("OP_ADD", offset);
        case OP_SUB:            return simple_instruction("OP_SUB", offset);
        case OP_MUL:            return simple_instruction("OP_MUL", offset);
        case OP_DIV:            return simple_instruction("OP_DIV", offset);
        case OP_MOD:            return simple_instruction("OP_MOD", offset);
        case OP_NEG:            return simple_instruction("OP_NEG", offset);
        case OP_BIT_AND:        return simple_instruction("OP_BIT_AND", offset);
        case OP_BIT_OR:         return simple_instruction("OP_BIT_OR", offset);
        case OP_BIT_XOR:        return simple_instruction("OP_BIT_XOR", offset);
        case OP_BIT_NOT:        return simple_instruction("OP_BIT_NOT", offset);
        case OP_SHL:            return simple_instruction("OP_SHL", offset);
        case OP_SHR:            return simple_instruction("OP_SHR", offset);
        case OP_NOT:            return simple_instruction("OP_NOT", offset);
        case OP_EQ:             return simple_instruction("OP_EQ", offset);
        case OP_NE:             return simple_instruction("OP_NE", offset);
        case OP_LT:             return simple_instruction("OP_LT", offset);
        case OP_LE:             return simple_instruction("OP_LE", offset);
        case OP_GT:             return simple_instruction("OP_GT", offset);
        case OP_GE:             return simple_instruction("OP_GE", offset);
        case OP_JUMP:           return jump_instruction("OP_JUMP", 1, c, offset);
        case OP_JUMP_IF_FALSE:  return jump_instruction("OP_JUMP_IF_FALSE", 1, c, offset);
        case OP_JUMP_IF_TRUE:   return jump_instruction("OP_JUMP_IF_TRUE", 1, c, offset);
        case OP_JUMP_PEEK_FALSE:return jump_instruction("OP_JUMP_PEEK_FALSE", 1, c, offset);
        case OP_JUMP_PEEK_TRUE: return jump_instruction("OP_JUMP_PEEK_TRUE", 1, c, offset);
        case OP_LOOP:           return jump_instruction("OP_LOOP", -1, c, offset);
        case OP_ARRAY:          return constant_instruction("OP_ARRAY", c, offset);
        case OP_MAP:            return constant_instruction("OP_MAP", c, offset);
        case OP_INDEX_GET:      return simple_instruction("OP_INDEX_GET", offset);
        case OP_INDEX_SET:      return simple_instruction("OP_INDEX_SET", offset);
        case OP_GET_PROP:       return constant_instruction("OP_GET_PROP", c, offset);
        case OP_SET_PROP:       return constant_instruction("OP_SET_PROP", c, offset);
        case OP_INVOKE: {
            uint16_t lo = c->code[offset + 1];
            uint16_t hi = c->code[offset + 2];
            uint16_t idx = (uint16_t)(lo | (hi << 8));
            uint8_t argc = c->code[offset + 3];
            printf("%-16s (%d args) %4d '", "OP_INVOKE", (int)argc, (int)idx);
            if (idx < (uint16_t)c->const_count) val_print(c->constants[idx]);
            printf("'\n");
            return offset + 4;
        }
        case OP_CALL:           return byte_instruction("OP_CALL", c, offset);
        case OP_RETURN:         return simple_instruction("OP_RETURN", offset);
        case OP_CLOSURE: {
            uint16_t lo = c->code[offset + 1];
            uint16_t hi = c->code[offset + 2];
            uint16_t idx = (uint16_t)(lo | (hi << 8));
            printf("%-16s %4d\n", "OP_CLOSURE", (int)idx);
            offset += 3;
            if (idx < (uint16_t)c->const_count && c->constants[idx].type == V_FN) {
                /* 打印各个 upvalue 描述 */
            }
            return offset;
        }
        case OP_TRY:            return jump_instruction("OP_TRY", 1, c, offset);
        case OP_END_TRY:        return simple_instruction("OP_END_TRY", offset);
        case OP_THROW:          return simple_instruction("OP_THROW", offset);
        case OP_ECHO:           return simple_instruction("OP_ECHO", offset);
        case OP_HALT:           return simple_instruction("OP_HALT", offset);
        default:
            printf("未知操作码: %d\n", op);
            return offset + 1;
    }
}

void chunk_disassemble(const Chunk *c, const char *name) {
    printf("== 反汇编代码块: %s ==\n", name ? name : "<anonymous>");
    for (int offset = 0; offset < c->count;) {
        offset = chunk_disassemble_instruction(c, offset);
    }
}
