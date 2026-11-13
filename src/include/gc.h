#ifndef L_GC_H
#define L_GC_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    OBJ_STR,
    OBJ_ARRAY,
    OBJ_MAP,
    OBJ_FN,
    OBJ_ENV,
    OBJ_NATIVE,
    OBJ_BYTES,
    OBJ_PROTO,
    OBJ_UPVALUE
} ObjType;

/* 所有堆对象的公共头部 */
typedef struct GcObj {
    ObjType type;
    uint8_t marked;         /* GC 标记: 0 未访问, 1 可达 */
    struct GcObj *next;     /* 链入 GC 托管链表 */
} GcObj;

#endif
