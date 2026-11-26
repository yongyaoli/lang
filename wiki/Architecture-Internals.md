# C99 解释器内核架构剖析

L 语言从底层词法、语法、抽象语法树到垃圾回收器，完全由纯 C99 从零手写实现，未引入任何外部库或生成器工具（如 Flex/Bison/LLVM）。

---

## 1. 核心目录与模块划分

```text
src/
├── core/                  # 语言核心解释器引擎
│   ├── lexer.c            # 词法分析器 (Token 流流式扫描)
│   ├── parser.c           # 递归下降语法分析器 (AST 构建)
│   ├── ast.c              # 抽象语法树节点内存管理与遍历
│   ├── interp.c           # AST / 字节码解释执行器
│   ├── value.c            # 紧凑动态值表示 (Value 结构体)
│   ├── gc.c               # 标记-清除 (Mark-Sweep) 垃圾回收器
│   ├── l_heap.c           # 静态 64KB/128KB 嵌入式内存分配池
│   ├── builtins.c         # 核心内置函数 (print, len, type 等)
│   └── compiler.c / vm.c  # 实验性紧凑字节码编译器与虚拟机
├── stdlib/                # 跨平台标准库与驱动
│   ├── hal.c              # 硬件抽象层统一适配 (PC 仿真 / ESP-IDF)
│   ├── hw.c               # 芯片管理与引脚安全检查
│   ├── net.c              # Socket 网络、TCP 客户端与服务端
│   ├── mqtt.c             # MQTT v3.1.1 协议栈客户端
│   ├── ota.c              # 固件与脚本无线空中升级库
│   ├── json.c             # 极速紧凑 JSON 解析器
│   └── time.c / fs.c      # 时间与虚拟文件系统
└── include/               # 公共 API 与结构体头文件
```

---

## 2. 内存模型与静态堆 (`l_heap.c`)

在单片机中，频繁调用标准库 `malloc` / `free` 极易引发内存碎片化导致崩溃变砖。
* L 语言实现了**独立的内存隔离池机制**；
* 可以在编译期收敛到固定的 64KB 或 128KB 静态数组；
* 配合 **Mark-Sweep（标记-清除）垃圾回收机制**，周期性回收无用字符串与闭包对象，确保长期运行（7×24小时）内存水位平稳。

---

## 3. 硬件抽象层 (HAL) 桥接机制

通过 [`src/include/l_hal.h`](file:///d:/github/langgit/src/include/l_hal.h)，上层解释器完全与芯片底层驱动解耦：
* 编译 PC 版本时，引入 `src/stdlib/tcp_win.c` 或 `tcp_posix.c`，硬件操作映射到内存模拟器；
* 编译 ESP-IDF 固件时，引入 ESP-IDF 原生的 `driver/gpio.h`、`driver/ledc.h`、`esp_wifi.h` 与 `lwip/sockets.h`，无缝发挥 ESP32 芯片的极致硬件性能。
