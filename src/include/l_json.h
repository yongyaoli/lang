/*
 * L 语言流式 JSON 编解码器头文件
 *
 * 专注于 ESP32 / 嵌入式低内存场景的零外部依赖流式 JSON 编解码器。
 * 纯 C99 实现，严格遵循 RFC 8259 规范，具备最大嵌套深度防御，
 * 直接在 L 托管堆分配原生 Map/Array，避免中间二次拷贝。
 */

#ifndef L_JSON_H
#define L_JSON_H

#include "interp.h"
#include "value.h"

/* 将 JSON 字符串反序列化为 L 原生 Value (Map/Array/String/Int/Float/Bool/Nil) */
Value l_json_parse(Interp *in, const Node *at, const char *json_str, size_t len);

/* 将 L 原生 Value 序列化为 JSON 格式的字符串对象 (pretty 非 0 时输出 2 空格缩进) */
Value l_json_stringify(Interp *in, const Node *at, Value val, int pretty);

/* 在解释器全局环境中注册 JSON / json 对象与便捷内置函数 */
void json_install_globals(Interp *in);

/* 构建内置 json 模块 Map (供 import "json" 使用) */
Value json_build_module(Interp *in);

#endif /* L_JSON_H */
