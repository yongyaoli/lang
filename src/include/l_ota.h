/*
 * L 语言无线网络空中升级 (OTA) 标准库头文件 (M15)
 *
 * 专注于嵌入式与 PC 跨平台的统一 OTA 升级体系:
 * - 脚本级空中升级 (Script OTA): 从 HTTP(S) 端点拉取脚本源码，校验 CRC32，流式写入裸 Flash 分区 (l_script) 并热执行
 * - 局域网 Web OTA 支撑: 支持直接从字符串写入 Flash 分区并热重载，完美配合 Web OTA 服务器
 * - 内置 PC 宿主环境高保真模拟器 (sim_set_response)，支持自动化回归测试与边界异常演练
 * - 纯 C99 零第三方依赖，与 l_heap、l_hal 及异常定位机制深度整合
 */

#ifndef L_OTA_H
#define L_OTA_H

#include "interp.h"
#include "value.h"

/* 在解释器全局环境中注册 OTA 全局对象与帮助文档 */
void ota_install_globals(Interp *in);

/* 构建内置 ota 模块 Map (供 import "ota" 使用) */
Value ota_build_module(Interp *in);

#endif /* L_OTA_H */
