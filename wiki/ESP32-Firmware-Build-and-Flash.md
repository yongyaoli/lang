# ESP32 固件编译与刷机指南

如果您需要定制 L 语言的底层底包（例如增加自定义 C 驱动、适配新型国产 RISC-V 芯片），或者为全新的空开发板刷机，请查阅本指南。

---

## 1. 固件底包一览

在项目的发布目录中，我们提供了各大主流芯片的预编译合并底包（Merged Binary，集成引导加载程序 bootloader、分区表与主程序）：

| 芯片型号 | 固件文件名 | 推荐烧录起始偏移 |
| :--- | :--- | :--- |
| **ESP32** (经典双核) | `l-esp32-merged.bin` | `0x0` |
| **ESP32-S3** (双 Type-C 款) | `l-esp32s3-merged.bin` | `0x0` |
| **ESP32-C3** (RISC-V 160M) | `l-esp32c3-merged.bin` | `0x0` |
| **ESP32-C6** (Wi-Fi 6) | `l-esp32c6-merged.bin` | `0x0` |

---

## 2. 使用 esptool 刷入底包

### 步骤 1：进入下载模式
* 按住板载的 **BOOT** 键不放；
* 短按一下 **RST / EN** 重启键；
* 松开 **BOOT** 键。

### 步骤 2：执行擦除与刷机
```powershell
# 1. 推荐先全盘擦除 (清除旧环境残留)
esptool --chip esp32 --port COM3 erase-flash

# 2. 刷入一体化固件到 0x0 起始地址
esptool --chip esp32 --port COM3 --baud 460800 write-flash 0x0 l-esp32-merged.bin
```
*(注：请将 `esp32` 换为您实际的芯片型号，如 `esp32s3` 或 `esp32c3`；`COM3` 替换为您电脑的实际端口)*。

---

## 3. 从源码全新编译底包 (ESP-IDF v5.3+)

如果您本地安装了 Espressif ESP-IDF 开发环境：

```powershell
# 1. 激活 ESP-IDF 环境变量
. $HOME/esp/esp-idf/export.ps1

# 2. 进入项目的 esp32 目录
cd d:\github\langgit\esp32

# 3. 设置目标芯片架构
idf.py set-target esp32c3  # 或 esp32 / esp32s3 / esp32c6

# 4. 编译固件
idf.py build

# 5. 一键烧录与串口监视
idf.py -p COM3 flash monitor
```
编译成功后，即可在串口看到熟悉的 L 语言启动欢迎头：
```text
================================================
  L 语言 ESP32 固件 0.1.14 (v5.3)
  输入 help() 查看帮助, 输入 ESP32.status() 查看系统与网络状态
================================================
```
