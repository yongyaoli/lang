# 快速起步与环境搭建

本篇指南将协助您在 **1 分钟内完成电脑端与 ESP32 开发环境的配置**。

---

## 1. 软件安装 (Windows 11)

L 语言提供了全自动的 Windows 安装包，内置编译器、REPL、固件工具与 VSCode 语法插件。

### 方式 A：通过 Inno Setup 安装包（推荐）
1. 运行项目发布的 `L-Language-Setup-v0.1.14.exe`；
2. 按照向导点击下一步即可；
3. 安装向导会自动将 `C:\Program Files\L-Language` 加入系统的 `PATH` 环境变量，并自动在 VSCode 中注册 `.l` 文件图标与语法高亮。

### 方式 B：从源码手动编译 (GCC / MinGW)
本项目没有任何第三方依赖，只需标准 C 编译器即可在 2 秒内编译完成：
```powershell
# 在 langgit 根目录执行
gcc -O2 -Isrc/include src/core/*.c src/stdlib/*.c -lws2_32 -lwininet -o l.exe
```
编译成功后即可直接使用 `./l.exe`。

---

## 2. 检查安装与运行 Hello World

打开 PowerShell 或终端，检查 L 语言环境：
```powershell
l --version
```
输出类似：
```text
L 0.1.14.202611251708 (Windows x86_64)
```

### 运行第一个脚本
新建一个文件 `hello.l`，写入：
```l
// 打印问候与计算
print("你好，L 语言！");
let a = 10;
let b = 20;
print("10 + 20 =", a + b);

// 体验硬件仿真
print("当前仿真芯片:", ESP32.chip());
pin(2).on();
print("LED 状态:", pin(2).read());
```
在命令行执行：
```powershell
l hello.l
```

---

## 3. 准备 ESP32 开发板

L 语言原生支持全系列 ESP32 芯片：
* **ESP32** (经典双核 Xtensa LX6)
* **ESP32-S3** (双核 Xtensa LX7，支持 USB-JTAG)
* **ESP32-C3** (单核 RISC-V 160MHz)
* **ESP32-C6** (RISC-V Wi-Fi 6 + Zigbee/Thread)

### 硬件初次刷入底包
首次拿到一块全新的开发板时，只需要通过串口刷一次底包固件（此后无需再插线编译）：
```powershell
# 1. 查询当前串口端口
l -p

# 2. 刷入底包固件 (以 ESP32-C3 为例)
esptool --port COM3 --baud 460800 write-flash 0x0 l-esp32c3-merged.bin
```

---

## 4. 日常 0.2 秒极速开发流程

底包固件刷好后，日常开发**完全不再需要编译 C 代码**：
1. 在电脑上写好 `app.l`；
2. 执行一条命令推送到板子：
   ```powershell
   l -u COM3 app.l
   ```
   **0.2 秒完成烧录**，开发板立即开始执行新代码！断电重启永不丢失。
