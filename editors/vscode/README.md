# L Language Support for Visual Studio Code

L 语言（`lang`）官方 VS Code 语法高亮与智能提示插件。

L 是一门用纯 C99 从零手写实现的超轻量嵌入式（ESP32 系列）与通用脚本语言。

---

## 核心特性

- **语法高亮与颜色分层**：
  - 完美识别关键字 `let`、`const`、`fn`、`try`、`catch`、`throw`、`import` 等；
  - 硬件外设类与系统内置单例（`Pin`、`PWM`、`ADC`、`I2C`、`SPI`、`UART`、`Timer`、`ESP32`）青色高亮；
  - 字符串（单双引号）、数字字面量、十六进制 `0x` 与二进制 `0b`。
- **Hover 鼠标悬停提示**：
  - 鼠标移动到 `pin()`、`ESP32.status()`、`bytes` 等内置函数与类上时，自动显示函数签名与中文 API 用法文档；
  - 支持用户自定义函数与类的方法签名提示。
- **跳转到定义 (Go to Definition)**：
  - <kbd>Ctrl</kbd> + 鼠标左键点击自定义函数或变量，直接跳转到源码声明位置。
- **L 专属文件图标**：
  - 自动为 `.l` 脚本文件提供专属优雅图标。

---

## 快速上手

1. 在 VS Code 中打开任意 `.l` 脚本文件（例如 `main.l`）；
2. 语言模式将自动识别为 `L` 并启用全套高亮与智能提示；
3. 如需在终端运行或烧录：
   ```powershell
   l main.l             # PC 离线模拟运行
   l -p                 # 列出开发板串口
   l -u COM3 main.l     # 0.2 秒极速烧录到 ESP32
   ```

---

## 开源协议

MIT License · © 2026 Yongyao Li
