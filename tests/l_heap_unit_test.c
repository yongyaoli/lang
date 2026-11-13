/* l_heap 独立单元测试: 随机分配/释放/重分配, 校验数据完整性 */
#include "../src/include/l_heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_PTRS 512

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

int main(void) {
    l_heap_init();
    unsigned char *ptrs[MAX_PTRS] = {0};
    size_t sizes[MAX_PTRS] = {0};
    unsigned char seeds[MAX_PTRS] = {0};

    for (int iter = 0; iter < 200000; iter++) {
        int i = rnd() % MAX_PTRS;
        int op = rnd() % 100;
        if (ptrs[i] && op < 40) {
            /* free: 校验后释放 */
            for (size_t k = 0; k < sizes[i]; k++) {
                if (ptrs[i][k] != (unsigned char)(seeds[i] + k)) {
                    printf("CORRUPT iter=%d idx=%d off=%zu\n", iter, i, k);
                    return 1;
                }
            }
            l_heap_free(ptrs[i]);
            ptrs[i] = NULL;
        } else if (ptrs[i] && op < 70) {
            /* realloc: 保留旧数据, 扩展区填新种子 */
            size_t ns = rnd() % 4096;
            if (ns == 0) {
                l_heap_free(ptrs[i]);
                ptrs[i] = NULL;
                continue;
            }
            unsigned char *q = (unsigned char *)l_heap_realloc(ptrs[i], ns);
            if (!q) continue;
            size_t keep = sizes[i] < ns ? sizes[i] : ns;
            /* 校验 realloc 保留了旧数据前缀 [0, keep) (种子不变) */
            for (size_t k = 0; k < keep; k++) {
                if (q[k] != (unsigned char)(seeds[i] + k)) {
                    printf("RCORRUPT iter=%d idx=%d off=%zu\n", iter, i, k);
                    return 1;
                }
            }
            /* 扩展区间续填 (种子不变, 保证全缓冲仍满足 seed+k) */
            ptrs[i] = q; sizes[i] = ns;
            for (size_t k = keep; k < ns; k++) q[k] = (unsigned char)(seeds[i] + k);
        } else if (!ptrs[i]) {
            /* alloc */
            size_t ns = rnd() % 4096;
            if (ns == 0) ns = 1;
            unsigned char *q = (unsigned char *)l_heap_alloc(ns);
            if (!q) continue;
            ptrs[i] = q; sizes[i] = ns; seeds[i] = (unsigned char)rnd();
            for (size_t k = 0; k < ns; k++) q[k] = (unsigned char)(seeds[i] + k);
        }
    }
    /* 收尾校验 */
    for (int i = 0; i < MAX_PTRS; i++) {
        if (!ptrs[i]) continue;
        for (size_t k = 0; k < sizes[i]; k++) {
            if (ptrs[i][k] != (unsigned char)(seeds[i] + k)) {
                printf("FINAL-CORRUPT idx=%d off=%zu\n", i, k);
                return 1;
            }
        }
    }
    printf("HEAP-OK used=%zu free=%zu cap=%zu\n", l_heap_used_bytes(), l_heap_free_bytes(), l_heap_capacity());
    return 0;
}
