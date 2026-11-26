# ESP32 硬件外设与 HAL API 手册

L 语言提供了一套统一、高性能的硬件抽象层（HAL），在真机上由 ESP-IDF 原生驱动实现，在电脑上由仿真内核接管。

---

## 1. 芯片与系统管理 (`ESP32`)

| 方法 / 属性 | 返回值 | 功能说明 |
| :--- | :--- | :--- |
| `ESP32.chip()` | `string` | 查询芯片型号（如 `"ESP32"`, `"ESP32-C3"`, `"ESP32-S3"`） |
| `ESP32.info()` | `map` | 芯片全景字典（架构、核心数、主频、Flash大小、MAC等） |
| `ESP32.free_heap()`| `int` | 当前芯片可用内部 SRAM 堆内存（字节） |
| `ESP32.mac()` | `string` | 芯片物理网卡 MAC 地址（如 `"20:E7:C8:9F:2C:DC"`） |
| `ESP32.restart()` | `void` | 软件重启开发板 |
| `ESP32.delay_ms(n)`| `void` | 毫秒级阻塞延时（自动让出调度并喂狗） |
| `ESP32.deepsleep(ms)`| `void`| 进入低功耗深度睡眠，时间到期后硬件复位唤醒 |

---

## 2. GPIO 数字输入输出 (`Pin` / `pin`)

```l
// 常量定义
GPIO.INPUT;      // 0: 输入模式
GPIO.OUTPUT;     // 1: 输出模式
GPIO.PULL_UP;    // 2: 上拉输入
GPIO.PULL_DOWN;  // 3: 下拉输入

// 实例化引脚
let p = Pin(4, GPIO.OUTPUT);
p.write(1);             // 写高电平
let val = p.read();     // 读电平 (0 或 1)
p.toggle();             // 翻转电平

// 硬件外部中断
p.mode(GPIO.INPUT);
p.irq(fn(pin_no, level) {
    print("触发中断! 引脚:", pin_no, "电平:", level);
}, "rise"); // "rise"(上升沿), "fall"(下降沿), "change"(跳变)
```

---

## 3. PWM 脉宽调制器 (`PWM`)

适用于控制舵机角度、LED 无级调光、无源蜂鸣器音调：
```l
// 声明 2 号引脚 PWM: 5000Hz, 占空比 512 (50%, 范围 0~1023)
let led_pwm = PWM(2, 5000, 512);

// 呼吸灯循环
for (let duty = 0; duty <= 1023; duty = duty + 10) {
    led_pwm.duty(duty);
    sleep(0.01);
}
```

---

## 4. ADC 模拟量采集 (`ADC`)

```l
// 声明 34 号引脚为模拟采集通道
let adc = ADC(34);
let raw = adc.read();      // 读取 12 位原始值 (0 ~ 4095)
let mv = adc.read_mv();    // 经过出厂校准后的毫伏值 (如 1850 mV)
print("当前检测电压:", mv / 1000.0, "V");
```

---

## 5. I2C 总线与多从机通信 (`I2C`)

```l
// 端口号 0, SDA=21, SCL=22, 频率 400kHz
let bus = I2C(0, 21, 22, 400000);

// 扫描总线挂载的所有从机设备地址
let addrs = bus.scan();
print("发现 I2C 设备地址:", addrs);

// 寄存器读写
bus.write_reg(0x68, 0x6B, bytes([0x00])); // 唤醒 MPU6050
let data = bus.read_reg(0x68, 0x3B, 6);   // 读取 6 字节加速度计数据
```
