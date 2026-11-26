# PC 离线硬件仿真系统

出差路上、没带开发板、手头没有特定芯片？L 语言内置了一套**强大的跨平台 PC 离线硬件仿真引擎**，同一份 `.l` 脚本不用改动一行代码，即可在 Windows、Linux、macOS 电脑上直接跑通全部外设交互！

---

## 1. 模拟芯片切换 (`ESP32.set_chip`)

您可以在脚本开头随时切换想要模拟的目标芯片：
```l
// 切换为 ESP32-C3 单核 RISC-V 芯片
ESP32.set_chip("ESP32-C3");

let info = ESP32.info();
print("芯片型号:", info.chip);   // ESP32-C3
print("芯片架构:", info.arch);   // RISC-V
print("主频:", info.freq_mhz);   // 160
```
一旦设置，底层会自动切换该芯片的引脚安全矩阵与限制。

---

## 2. GPIO 与电平注入 (`write_sim`)

在电脑端操作 GPIO 时，控制台会自动以可视化形式输出引脚变化：
```l
let led = pin(2, "out");
led.on();  // 控制台打印: [sim:gpio] pin 2 (out) -> 1
led.off(); // 控制台打印: [sim:gpio] pin 2 (out) -> 0

// 模拟外部传感器输入与中断触发
let btn = pin(4, "in");
btn.irq(fn(pin_no, level) {
    print("触发中断! 引脚", pin_no, "电平跳变至:", level);
}, "rise");

// 注入外部电平跳变，触发 ISR 中断入队
btn.write_sim(1);
```

---

## 3. 虚拟 I2C 设备仿真 (`sim_device`)

PC 端甚至支持注册虚拟 I2C 寄存器设备，模拟真实传感器的应答：
```l
let bus = I2C(0, 21, 22, 400000);

// 在 0x3C 地址注册一个虚拟设备
bus.sim_device(0x3C);

// 写入数据
bus.write_reg(0x3C, 0x00, bytes([0xAE, 0xD5]));

// 读取数据 (默认应答)
let data = bus.read_reg(0x3C, 0x00, 2);
print("PC 模拟读取到:", data.hex(" "));
```
