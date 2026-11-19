/*
 * L 语言统一虚拟文件系统头文件 (FS / File 标准库, M13)
 *
 * 专注于嵌入式与 PC 跨平台的统一 VFS 抽象:
 * - PC 宿主环境: 透明映射本地磁盘与 ./flash 模拟分区
 * - ESP32 嵌入式环境: 映射 LittleFS 专用分区 (掉电安全、磨损均衡，单句柄 <512B)
 * - 提供全局快捷对象 FS 与大文件流式句柄 File
 * - 纯 C99 零第三方外部依赖，与 l_heap 托管堆深度整合
 */

#ifndef L_FS_H
#define L_FS_H

#include "interp.h"
#include "value.h"

/* 路径解析: 将虚拟路径 (如 "/flash/config.json") 解析映射为当前平台实际文件系统物理路径 */
const char *l_vfs_resolve_path(const char *path, char *out_buf, size_t cap);

/* 在解释器全局环境中注册 FS 与 File 原生对象/类 */
void fs_install_globals(Interp *in);

/* 构建内置 fs 模块 Map (供 import "fs" 使用) */
Value fs_build_module(Interp *in);

#endif /* L_FS_H */
