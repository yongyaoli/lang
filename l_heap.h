/* L 语言固定静态堆分配器 (M7)
 * 目标: 在 320KB SRAM 级 MCU (ESP32) 上把解释器全部托管内存收敛进一块
 * 编译期定长的静态池, 彻底消除运行期 malloc 碎片化与分配失败的不确定性。
 *
 * - ESP-IDF: 默认启用 64KB 静态池 (L_STATIC_HEAP_SIZE, 可编译期覆盖),
 *   采用"首次适配 + 空闲块合并"的紧凑分配器。
 * - PC 宿主: 默认 L_STATIC_HEAP_SIZE=0, 直接透传 libc malloc/realloc/free,
 *   行为与旧版完全一致, 且不受池大小限制。
 * 所有解释器长生命周期内存 (GC 对象及其内部缓冲) 统一经由本模块分配,
 * 短生命周期的宿主 IO 缓冲 (读文件等) 仍走 libc, 互不混用。 */
#ifndef L_HEAP_H
#define L_HEAP_H

#include <stddef.h>

/* 初始化静态池 (幂等; PC libc 模式为空操作) */
void l_heap_init(void);

/* 分配 n 字节, 8 字节对齐; 池耗尽返回 NULL (调用方应先触发 GC 重试) */
void *l_heap_alloc(size_t n);

/* 重分配; p 为 NULL 等价 alloc, n 为 0 等价 free */
void *l_heap_realloc(void *p, size_t n);

/* 释放; p 为 NULL 安全 */
void l_heap_free(void *p);

/* 指针是否由本模块管理 (PC libc 模式恒为 0) */
int l_heap_owns(const void *p);

/* 统计: 静态池模式返回池内数据; libc 模式 used/capacity 返回 0 */
size_t l_heap_used_bytes(void);
size_t l_heap_free_bytes(void);
size_t l_heap_capacity(void);

/* 当前模式: "static-pool" (静态池) 或 "libc" (宿主直通) */
const char *l_heap_mode(void);

/* 调试: 向 stderr 转储空闲块分布 */
void l_heap_debug_dump(void);

/* 调试: 校验记账一致性 */
void l_heap_verify(void);

#endif /* L_HEAP_H */
