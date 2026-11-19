/*
 * L 语言 MQTT 3.1.1 物联网客户端头文件
 *
 * 适用于 ESP32 嵌入式硬件与 PC 跨平台物联网通信。
 * 纯 C99 零外部依赖，严格遵循 MQTT 3.1.1 OASIS 规范。
 * 支持 CONNECT / CONNACK / PUBLISH(QoS 0/1) / PUBACK / SUBSCRIBE / SUBACK / PINGREQ / PINGRESP / DISCONNECT。
 * 与 l_heap、l_tcp 与 GC 紧密集成，原生支持阿里云 IoT、华为云 IoTDA 等主流物联网平台。
 */

#ifndef L_MQTT_H
#define L_MQTT_H

#include "interp.h"
#include "value.h"

/* 在解释器全局环境中注册 MqttClient 类与帮助信息 */
void mqtt_install_globals(Interp *in);

/* 构建内置 mqtt 模块 Map (供 import "mqtt" 使用) */
Value mqtt_build_module(Interp *in);

#endif /* L_MQTT_H */
