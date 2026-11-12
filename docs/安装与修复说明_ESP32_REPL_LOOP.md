# ESP32 串口终端一直输出 esp32>>> 的原因分析与修复记录

## 1. 故障现象
刷入固件后，在 PC 端执行 `connect("COM3")` 连接 ESP32，终端持续且飞速打印 `esp32>>> esp32>>> esp32>>>`，导致控制台被大量提示符淹没，键盘无法正常交互。

---

## 2. 根本原因定位

### 原因 1：ESP32 端使用了非阻塞的 `fgets()`
在 ESP32（ESP-IDF + newlib）中，标准输入 `stdin` 设置为了 `_IONBF`（无缓冲）。当串口没有接收到完整一行（带换行符）时，`fgets(line, sizeof line, stdin)` 会**非阻塞并立即返回 `NULL`**（判定为暂无数据 / EOF）。
固件原先的主循环为：
```c
for (;;) {
    printf("esp32>>> ");
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin)) {
        l_hal_delay_ms(10);
        continue; // 每次 NULL 延时 10ms 后立即 continue 回到开头！
    }
    ...
}
```
因为平时没有按键，`fgets` 每一轮都返回 `NULL`，每隔 10 毫秒就循环一次并打印一个 `esp32>>> `，**导致 1 秒钟连续打印 100 次 `esp32>>> `**！

### 原因 2：PC 端终端与板载 REPL 回显冲突
原先 PC 端的 `connect` 监控器在本地缓存并自行回显按键，如果 ESP32 也回显，会导致输入一个字符显示两次（双重回显）。

---

## 3. 修复方案与技术改进

1. **实现阻塞式行读取器 `esp32_readline()`**：
   - ESP32 端不再使用容易立即返回 NULL 的 `fgets()`，改用专用的按键级 `esp32_readline()`。
   - 在未收到任何字符时，仅在后台让出 CPU（`l_hal_delay_ms(10)`），**绝对不返回、绝不重复打印 `esp32>>> `**。
   - 支持实时回显用户输入的每个字符，并支持退格键（Backspace / DEL 擦除光标字符）与 `Ctrl+C` 取消当前输入。
   - **仅在用户敲下回车键执行完一条命令后，才打印下一次的 `esp32>>> ` 提示符**。
2. **PC 端终端纯透传改造**：
   - PC 端的 `connect("COM3")` 改为标准串口终端透传模式：敲击任何按键直接发送给 ESP32，回显完全由 ESP32 控制，彻底消除双重回显。
   - 输入 `.exit` 或按 `Ctrl+]` 可退出终端。

---

## 4. 固件与安装包更新状态

1. **本地安装包**：已重新打包并自动静默更新安装至 `%LOCALAPPDATA%\Programs\lang\l.exe`。
2. **云端固件构建**：修复代码已推送至 GitHub，Tag `v0.1.0-m6` 正在自动触发 GitHub Actions 编译生成 ESP32 / ESP32-S3 / ESP32-C3 的最新固件 bin 文件。流水线完成后可在 GitHub Release 页面下载最新固件重新烧录。
