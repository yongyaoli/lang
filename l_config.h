/* L 语言全局编译与裁剪配置 (ESP32 / ESP-IDF 与 PC 宿主通用) */
#ifndef L_CONFIG_H
#define L_CONFIG_H

/* ================= 1. 目标平台自动识别 ================= */
#ifdef ESP_PLATFORM
#  define L_PLATFORM_ESP_IDF 1
#  define L_PLATFORM_HOST    0
#else
#  define L_PLATFORM_ESP_IDF 0
#  define L_PLATFORM_HOST    1
#endif

/* ================= 2. 标准库模块按需裁剪开关 =================
 * 可在编译命令行通过 -DL_WITH_STDLIB_IO=0 等参数覆盖默认值。
 * 在无文件系统的 ESP32 固件上, 默认关闭 io 与 sys 模块以节省 Flash/RAM。 */

#ifndef L_WITH_STDLIB_STRING
#  define L_WITH_STDLIB_STRING 1
#endif

#ifndef L_WITH_STDLIB_MATH
#  define L_WITH_STDLIB_MATH   1
#endif

#ifndef L_WITH_STDLIB_TIME
#  define L_WITH_STDLIB_TIME   1
#endif

#ifndef L_WITH_STDLIB_HW
#  define L_WITH_STDLIB_HW     1
#endif

#ifndef L_WITH_STDLIB_NET
#  define L_WITH_STDLIB_NET    1
#endif

#ifndef L_WITH_LITTLEFS
#  define L_WITH_LITTLEFS      0
#endif

#ifndef L_WITH_STDLIB_IO
#  if L_PLATFORM_ESP_IDF && !L_WITH_LITTLEFS
#    define L_WITH_STDLIB_IO   0
#  else
#    define L_WITH_STDLIB_IO   1
#  endif
#endif

#ifndef L_WITH_STDLIB_SYS
#  if L_PLATFORM_ESP_IDF
#    define L_WITH_STDLIB_SYS  0
#  else
#    define L_WITH_STDLIB_SYS  1
#  endif
#endif

/* 是否保留内置 help() 文档字符串 (极限裁剪 Flash 时可设为 0) */
#ifndef L_WITH_HELP_DOCS
#  define L_WITH_HELP_DOCS     1
#endif

/* ================= 3. 运行时与内存参数配置 ================= */

/* 初始 GC 触发阈值 (字节): ESP32 默认 32KB, PC 宿主默认 1MB */
#ifndef L_GC_INIT_THRESHOLD
#  if L_PLATFORM_ESP_IDF
#    define L_GC_INIT_THRESHOLD (32 * 1024)
#  else
#    define L_GC_INIT_THRESHOLD (1024 * 1024)
#  endif
#endif

/* 最大函数调用栈深度 (防止递归耗尽 FreeRTOS 任务栈) */
#ifndef L_MAX_CALL_DEPTH
#  if L_PLATFORM_ESP_IDF
#    define L_MAX_CALL_DEPTH 64
#  else
#    define L_MAX_CALL_DEPTH 256
#  endif
#endif

/* ================= 4. 裸 Flash 脚本分区 (l_script) 参数 ================= */

#ifndef L_FLASH_PART_NAME
#  define L_FLASH_PART_NAME    "l_script"
#endif

#ifndef L_FLASH_PART_SUBTYPE
#  define L_FLASH_PART_SUBTYPE 0x99
#endif

#ifndef L_FLASH_PART_SIZE
#  define L_FLASH_PART_SIZE    (64 * 1024)
#endif

/* "LSCR" (源码脚本) 与 "LBC1" (预编译字节码) 魔数 (小端序) */
#define L_SCRIPT_MAGIC_SRC 0x5243534Cu
#define L_SCRIPT_MAGIC_BC  0x3143424Cu

#endif /* L_CONFIG_H */
