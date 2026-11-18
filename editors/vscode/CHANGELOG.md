# Change Log

## 0.3.2

- 修复 PowerShell 终端下由于引号包裹导致命令解析失败的问题（`"l" "path"` 报 `意外的标记` 错误），优化跨 Shell（PowerShell/cmd/bash/zsh）的命令行调用兼容性；
- 完善 Hover 悬停提示文档：增加 `JSON`、`json`、`json_parse` 与 `json_stringify` 说明。

## 0.3.1

- 新增编辑器/资源管理器右键上下文菜单与标题栏运行快捷按钮：支持一键运行、烧录到 ESP32 开发板、VM 模式运行、编译为 `.lc` 二进制字节码；
- 新增底部状态栏 ESP32 串口自动检测与快捷切换面板，支持即插即用智能提示；
- 新增内置串口监视器（Serial Monitor）终端支持，无需跳出编辑器即可实时查看板端打印日志与交互；
- 完善 Hover 悬停提示文档库：补充 `Tcp`、`TcpServer`、`BLE` 低功耗蓝牙与标准外设驱动库（`SSD1306`、`AHT20`、`WS2812`）说明。

## 0.3.0

- 完善语法高亮系统：增强硬件类 `Timer`、`Tcp`、`bytes` 等关键字着色；
- 支持内置外设与 ESP32 芯片 API 的 Hover 悬停提示文档；
- 支持自定义函数与局部变量的 Ctrl+Click 跳转定义；
- 添加 L 专属文件图标主题（`l-file-icons`）。
