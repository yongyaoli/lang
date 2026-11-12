/* 固定静态堆分配器实现 (M7): 首次适配 + 空闲块合并
 * 见 l_heap.h 头注释。 */
#include "l_heap.h"
#include "l_config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if L_STATIC_HEAP_SIZE > 0

#define L_HEAP_ALIGN 8u
#define L_HEAP_MAGIC 0x4C48454Eu  /* "LHEN" */

/* 内存块头部: 以双向链表维护, 保持地址有序以便释放时相邻合并 */
typedef struct HBlock {
    size_t          size;   /* 数据区大小(不含头部, 已对齐) */
    int             used;
    unsigned int    magic;  /* 调试金丝雀: 头部被踩则立即发现 */
    struct HBlock  *prev;
    struct HBlock  *next;
} HBlock;

static void heap_corrupt(const HBlock *b, const char *where) {
    fprintf(stderr, "[l_heap] 堆损坏: %s (block=%p size=%zu magic=0x%X)\n",
            where, (const void *)b, b->size, b->magic);
    abort();
}

#define L_HEAP_CHECK(b, w) do { if ((b)->magic != L_HEAP_MAGIC) heap_corrupt((b), (w)); } while (0)

static uint8_t s_pool[L_STATIC_HEAP_SIZE] __attribute__((aligned(8)));
static HBlock *s_head    = NULL;
static size_t  s_used    = 0;    /* 已分配数据区字节 */
static int     s_blocks  = 0;    /* 块总数 (用于精确扣除头部开销) */
static int     s_inited  = 0;

static size_t align_up(size_t n) {
    return (n + (L_HEAP_ALIGN - 1)) & ~((size_t)L_HEAP_ALIGN - 1);
}

static int in_pool(const void *p) {
    const uint8_t *q = (const uint8_t *)p;
    return q >= s_pool && q < s_pool + L_STATIC_HEAP_SIZE;
}

void l_heap_init(void) {
    s_head = (HBlock *)s_pool;
    s_head->size = L_STATIC_HEAP_SIZE - sizeof(HBlock);
    s_head->used = 0;
    s_head->magic = L_HEAP_MAGIC;
    s_head->prev = NULL;
    s_head->next = NULL;
    s_used = 0;
    s_blocks = 1;
    s_inited = 1;
}

void *l_heap_alloc(size_t n) {
    if (!s_inited) l_heap_init();
    if (n == 0) n = 1;
    n = align_up(n);

    for (HBlock *b = s_head; b; b = b->next) {
        L_HEAP_CHECK(b, "alloc 扫描");
        if (b->used || b->size < n) continue;

        /* 剩余空间足够再放一个块头+有意义的数据区(32B)才切分,
         * 否则整块分配: 避免产生大量 <32B 的碎屑空闲块破坏合并 */
        if (b->size >= n + sizeof(HBlock) + 32) {
            HBlock *nb = (HBlock *)((uint8_t *)(b + 1) + n);
            nb->size = b->size - n - sizeof(HBlock);
            nb->used = 0;
            nb->magic = L_HEAP_MAGIC;
            nb->prev = b;
            nb->next = b->next;
            if (b->next) b->next->prev = nb;
            b->next = nb;
            b->size = n;
            s_blocks++;
        }
        b->used = 1;
        s_used += b->size;
        return (void *)(b + 1);
    }
    return NULL;    /* 池耗尽: 由调用方 (gc.c) 触发回收后重试 */
}

void *l_heap_realloc(void *p, size_t n) {
    if (!p) return l_heap_alloc(n);
    if (!in_pool(p)) return NULL;
    if (n == 0) {
        l_heap_free(p);
        return NULL;
    }
    size_t old = ((HBlock *)p - 1)->size;
    if (align_up(n) <= old) return p;   /* 原地满足, 不搬运 */
    void *np = l_heap_alloc(n);
    if (!np) return NULL;
    memcpy(np, p, old < n ? old : n);
    l_heap_free(p);
    return np;
}

void l_heap_free(void *p) {
    if (!p || !s_inited || !in_pool(p)) return;

    HBlock *b = (HBlock *)p - 1;
    L_HEAP_CHECK(b, "free");
    if (b->used == 0) {
        fprintf(stderr, "[l_heap] 重复释放: p=%p size=%zu\n", p, b->size);
        abort();
    }
    b->used = 0;
    s_used -= b->size;

    /* 与后继空闲块合并 */
    if (b->next) {
        L_HEAP_CHECK(b->next, "free 后继");
        if (!b->next->used) {
            b->size += sizeof(HBlock) + b->next->size;
            b->next = b->next->next;
            if (b->next) b->next->prev = b;
            s_blocks--;
        }
    }
    /* 与前驱空闲块合并 */
    if (b->prev) {
        L_HEAP_CHECK(b->prev, "free 前驱");
        if (!b->prev->used) {
            b->prev->size += sizeof(HBlock) + b->size;
            b->prev->next = b->next;
            if (b->next) b->next->prev = b->prev;
            s_blocks--;
        }
    }
}

int l_heap_owns(const void *p) {
    return s_inited && p && in_pool(p);
}

size_t l_heap_used_bytes(void) { return s_used; }
/* 可分配数据空间 = 池容量 - 已用数据 - 全部块头部开销 (头部随块数动态变化,
 * 不扣除会让 GC 水位线严重高估剩余空间: 小块多时头部可占池的 40%+) */
size_t l_heap_free_bytes(void) {
    size_t hdr = (size_t)s_blocks * sizeof(HBlock);
    size_t total = L_STATIC_HEAP_SIZE;
    return (total > s_used + hdr) ? (total - s_used - hdr) : 0;
}
size_t l_heap_capacity(void)   { return L_STATIC_HEAP_SIZE - sizeof(HBlock); }
const char *l_heap_mode(void)  { return "static-pool"; }

/* 调试: 校验链表实际占用与记账一致性, 漂移时报告 */
void l_heap_verify(void) {
    if (!s_inited) return;
    size_t sum_used = 0;
    int n = 0;
    for (HBlock *b = s_head; b; b = b->next) {
        n++;
        if (b->used) sum_used += b->size;
        if (n > 1000000) { fprintf(stderr, "[l_heap] 链表成环!\n"); abort(); }
    }
    if (sum_used != s_used) {
        fprintf(stderr, "[l_heap] 记账漂移: 实际 used=%zu 记账=%zu (差 %zu), 块数=%d\n",
                sum_used, s_used, sum_used - s_used, n);
    }
}

/* 调试: 转储空闲块分布 */
void l_heap_debug_dump(void) {
    if (!s_inited) { fprintf(stderr, "[l_heap] 未初始化\n"); return; }
    int n_free = 0, n_used = 0;
    size_t max_free = 0, sum_used = 0;
    FILE *f = fopen("l_heap_dump.txt", "w");
    fprintf(stderr, "[l_heap] 空闲块分布:\n");
    for (HBlock *b = s_head; b; b = b->next) {
        if (f) fprintf(f, "%u %zu %d\n", (unsigned)((uint8_t *)b - s_pool), b->size, b->used);
        if (b->used) { n_used++; sum_used += b->size; continue; }
        n_free++;
        if (b->size > max_free) max_free = b->size;
        if (n_free <= 40) {
            fprintf(stderr, "  free size=%zu @+%u\n", b->size, (unsigned)((uint8_t *)b - s_pool));
        }
    }
    if (f) fclose(f);
    fprintf(stderr, "  共 used=%d(实际和=%zu) free=%d max_free=%zu 记账used=%zu\n",
            n_used, sum_used, n_free, max_free, s_used);
}

#else /* ============ PC 宿主: libc 直通模式, 行为与旧版一致 ============ */

void l_heap_init(void) {}

void *l_heap_alloc(size_t n) {
    return malloc(n);
}

void *l_heap_realloc(void *p, size_t n) {
    if (n == 0) {
        free(p);
        return NULL;
    }
    return realloc(p, n);
}

void l_heap_free(void *p) {
    free(p);
}

int l_heap_owns(const void *p) {
    (void)p;
    return 0;
}

size_t l_heap_used_bytes(void) { return 0; }
size_t l_heap_free_bytes(void) { return 0; }
size_t l_heap_capacity(void)   { return 0; }
const char *l_heap_mode(void)  { return "libc"; }

void l_heap_debug_dump(void) {}
void l_heap_verify(void) {}

#endif /* L_STATIC_HEAP_SIZE > 0 */
