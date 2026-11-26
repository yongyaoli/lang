# 面向对象与方法链式调用

L 语言为硬件开发量身打造了极简高效的面向对象机制，核心硬件类全面支持**流式链式调用（Fluent API）**。

---

## 1. 原生类与链式调用

在传统的嵌入式单片机开发中，配置一个引脚往往需要调用好几个独立的函数；而在 L 语言中，你可以一气呵成：

```l
// 声明 GPIO 2 为输出模式，点亮，并读取状态
let state = pin(2).mode(GPIO.OUTPUT).on().read();
print("LED 状态:", state); // 1

// 链式切换电平
pin(2).toggle().delay(100).toggle();
```

---

## 2. 常用链式硬件对象

### GPIO 引脚 (`Pin` / `pin`)
* `pin(pin_num, [mode])`：构造引脚对象
* `.on()` / `.high()`：拉高电平 (返回 self)
* `.off()` / `.low()`：拉低电平 (返回 self)
* `.toggle()`：反转电平 (返回 self)
* `.read()`：读取当前电平值 (0 或 1)
* `.mode(mode_val)`：动态重设模式 (输入/输出/上拉/下拉)
* `.irq(callback, trigger)`：注册硬件中断

### PWM 脉宽调制器 (`PWM`)
```l
// 声明 2 号引脚 PWM: 频率 5000Hz, 初始占空比 512 (50%)
let buzzer = PWM(2, 5000, 512);

// 链式改变频率与占空比
buzzer.freq(2000).duty(256);
```

### I2C 总线对象 (`I2C`)
```l
let oled = I2C(0, 21, 22, 400000); // 端口0, SDA=21, SCL=22, 400kHz
oled.write_reg(0x3C, 0x00, bytes([0xAE, 0xD5, 0x80]));
let data = oled.read_reg(0x3C, 0x00, 2);
```

---

## 3. 字典结构化对象

对于纯业务数据模型，L 语言使用 `map` 提供轻量级结构体能力，键名支持点号或中括号访问：
```l
let sensor = {
    "name": "BME280",
    "addr": 0x76,
    "calibrate": fn(raw) {
        return raw * 1.05 + 0.2;
    }
};

print("传感器型号:", sensor.name);
let val = sensor.calibrate(25.0);
print("校准后温度:", val);
```
