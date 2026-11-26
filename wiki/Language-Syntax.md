# 基础语法与数据类型

L 语言采用了符合现代程序员直觉的紧凑类 C / JavaScript 语法，简单直观、表达力极强。

---

## 1. 变量与常量

```l
// 变量使用 let 声明
let x = 10;
let message = "你好，世界";
x = x + 5;

// 常量使用 const 声明 (不可二次赋值)
const PI = 3.14159265;
const MAX_RETRIES = 3;
```

---

## 2. 核心数据类型

L 语言内置以下核心数据类型：

| 类型 | 说明与字面量语法 | 示例 |
| :--- | :--- | :--- |
| **`nil`** | 空值 / 无类型 | `let a = nil;` |
| **`bool`** | 布尔型 (`true` / `false`) | `let ok = true;` |
| **`int`** | 64 位整型 | `let count = 42;` |
| **`num`** | 双精度浮点型 (float) | `let temp = 26.5;` |
| **`string`** | UTF-8 字符串 (严格双引号) | `let s = "ESP32-S3";` |
| **`array`** | 动态扩容数组 | `let arr = [1, "two", 3.0];` |
| **`map`** | 键值对哈希字典 | `let m = {"name": "L", "ver": 1};` |
| **`bytes`** | 定长字节缓冲区 (硬件通信专用) | `let b = bytes([0xAA, 0x55]);` |
| **`function`** | 头等公民函数 | `fn add(a, b) { return a + b; }` |
| **`object`** | 原生硬件/网络句柄类 | `Pin`, `PWM`, `I2C`, `Tcp` 等 |

---

## 3. 控制流

### if / else 条件分支
```l
let score = 85;
if (score >= 90) {
    print("优秀");
} else if (score >= 60) {
    print("及格");
} else {
    print("不及格");
}
```

### 循环语句 (while / for)
```l
// while 循环
let i = 0;
while (i < 5) {
    print("while 计数:", i);
    i = i + 1;
}

// 标准三段式 for 循环
for (let j = 0; j < 3; j = j + 1) {
    print("for 迭代:", j);
}
```

### 逻辑运算符
* 逻辑与：`and` 或 `&&`
* 逻辑或：`or` 或 `||`
* 逻辑非：`!`
* 按位运算：`&` (按位与), `|` (按位或), `^` (异或), `~` (取反), `<<` (左移), `>>` (右移)

---

## 4. 函数定义

函数在 L 语言中是一等公民，支持默认返回值、递归与闭包：
```l
// 标准具名函数
fn calc_pwm(duty_percent) {
    if (duty_percent < 0 or duty_percent > 100) {
        return nil;
    }
    return duty_percent * 1023 / 100;
}

// 匿名函数 (可作为中断或事件回调)
let callback = fn(pin_no, level) {
    print("引脚", pin_no, "电平跳变至:", level);
};
```
