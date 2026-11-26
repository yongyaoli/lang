# L 语言 (`lang`) 官方 Wiki

欢迎查阅 **L 语言（`lang`）** 官方维基文档库！

L 语言是一门用纯 C99 从零手写实现的超轻量级嵌入式（ESP32 系列）与通用脚本语言，兼具 **C 语言的高性能紧凑语法** 与 **MicroPython 的丝滑交互体验**。

---

## 📚 目录导航

### 1. 新手入门 (Getting Started)
* [项目简介与设计理念](Home.md)
* [快速起步与环境搭建](Getting-Started.md)
* [命令行与 REPL 交互指南](CLI-and-REPL.md)

### 2. 语言核心语法 (Language Specification)
* [基础语法与数据类型](Language-Syntax.md)
* [面向对象与方法链式调用](OOP-and-Chaining.md)
* [模块系统与异常安全](Modules-and-Error-Handling.md)

### 3. ESP32 硬件开发 (ESP32 Hardware & Drivers)
* [ESP32 硬件外设与 HAL API](ESP32-Hardware-API.md)
* [裸 Flash 分区与极速串口烧录原理](Flash-Partition-and-Upload.md)
* [ESP32 固件编译与刷机指南](ESP32-Firmware-Build-and-Flash.md)

### 4. 物联网与网络通信 (IoT & Networking)
* [WiFi、SNTP 与网络诊断](WiFi-and-Network.md)
* [TCP 客户端与服务端编程](TCP-Socket-Programming.md)
* [MQTT 物联网接入与华为云 IoT 实战](MQTT-IoT-Protocol.md)
* [OTA 无线空中升级与 Web OTA 控制台](Wireless-OTA-System.md)

### 5. 电脑端开发与工具链 (PC Tools & Ecosystem)
* [PC 离线硬件仿真系统](PC-Hardware-Simulation.md)
* [VSCode 插件与语言服务配置](VSCode-Extension.md)
* [Windows 独立安装包制作](Windows-Installer-Packaging.md)

### 6. 开发者与架构手册 (Architecture & Contributing)
* [C99 解释器内核架构剖析](Architecture-Internals.md)
* [标准库扩展开发指南](Stdlib-Extension-Guide.md)
* [常见问题排查手册 (FAQ)](FAQ-and-Troubleshooting.md)

---

## ⚡ 核心设计亮点
1. **纯 C99 零外部依赖**：不依赖任何第三方解析器或臃肿运行时，单编译器直接编译出百余 KB 极轻量体积。
2. **免文件系统裸 Flash 存储**：独创 `l_script` 裸分区（16B Header + IEEE 802.3 CRC32 + `esp_partition_mmap`），0.2 秒免插线极速烧写与断电自启。
3. **PC 离线全硬件仿真**：没带板子也能在电脑上模拟芯片引脚、PWM 呼吸灯、I2C 传感器、UART 报文与 GPIO 中断。
4. **工业级 Web OTA 防崩溃守护**：单板内置 Web 控制台，浏览器直接无线改代码，报错安全隔离绝不炸机。
