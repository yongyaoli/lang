// 测试 16: ESP32 基础功能类 + M3 (const, bytes, 内置硬件模块与外设 PWM/ADC/I2C/SPI/UART)

print("=== 1. ESP32 基础功能类 (版本识别与引脚信息) ===");
import "esp32" as hw;

let info = ESP32.info();
print("chip:", ESP32.chip(), "arch:", info.arch, "cores:", info.cores, "idf:", info.idf);
print("version:", ESP32.version());
print("mac:", ESP32.mac(), "freq:", ESP32.freq(), "MHz");

// 查询特定引脚信息
let p2 = ESP32.pin_info(2);
print("GPIO2:", p2.name, "in:", p2.in, "out:", p2.out, "adc:", p2.adc, "strapping:", p2.strapping, "note:", p2.note);

let p6 = ESP32.pin_info(6);
print("GPIO6:", p6.name, "flash:", p6.flash, "usable:", p6.usable, "note:", p6.note);

let p34 = ESP32.pin_info(34);
print("GPIO34:", p34.name, "in:", p34.in, "out:", p34.out, "note:", p34.note);

// 硬件引脚合法性保护测试
try {
    Pin(6, "out");
} catch (e) {
    print("Caught Flash pin error:", e.message);
}

try {
    Pin(34, "out");
} catch (e) {
    print("Caught Input-only pin error:", e.message);
}

// 切换芯片型号查询 (如 ESP32-C3 / ESP32-S3)
ESP32.set_chip("ESP32-C3");
let c3 = ESP32.info();
print("Switched to:", c3.chip, "arch:", c3.arch, "cores:", c3.cores, "gpio_count:", c3.gpio_count);
let c3_p12 = ESP32.pin_info(12);
print("C3 GPIO12 flash:", c3_p12.flash, "usable:", c3_p12.usable, "note:", c3_p12.note);

// 切回默认 ESP32
ESP32.set_chip("ESP32");
let pins_summary = ESP32.pins();
print("ESP32 usable pins:", len(pins_summary.usable_pins), "flash pins:", pins_summary.flash_pins, "input_only:", pins_summary.input_only_pins);

print("=== 2. const 常量声明与保护 ===");
const LED_PIN = 2;
const I2C_ADDR = 0x3C;
print("LED_PIN:", LED_PIN, "I2C_ADDR:", I2C_ADDR);

try {
    LED_PIN = 4;
} catch (e) {
    print("Caught const reassign:", e.message, "at line", e.line);
}

print("=== 3. bytes 定长字节缓冲 ===");
let b1 = bytes(4);
b1.fill(0xAA);
b1[0] = 0x12;
b1[3] = 0x34;
print("b1 len:", len(b1), "hex:", b1.hex(" "));

b1.set_u16_be(0, 0xCAFE);
b1.set_u16_le(2, 0x1234);
print("b1 u16_be(0):", b1.u16_be(0) == 0xCAFE, "u16_le(2):", b1.u16_le(2) == 0x1234);
print("b1 hex:", b1.hex(":"));

let b2 = bytes([0x48, 0x65, 0x6C, 0x6C, 0x6F]);
print("b2 str:", b2.to_str(), "slice(1,4):", b2.slice(1, 4).to_str());

let b3 = bytes("ESP32");
print("b3 array:", b3.to_array());

print("=== 4. 外设类: PWM / ADC / I2C / SPI / UART ===");
// PWM
let pwm = PWM(LED_PIN, 5000, 512);
print("PWM freq:", pwm.freq(), "duty:", pwm.duty());
pwm.duty(256);
print("PWM new duty:", pwm.duty());
pwm.deinit();

// ADC
let adc = ADC(34);
adc.atten(11);
adc.write_sim(2048);
print("ADC raw:", adc.read(), "mV:", adc.read_mv());

// I2C (含虚拟从机与寄存器读写)
let i2c = hw.I2C(0, 21, 22, 400000);
i2c.sim_device(0x3C);
print("I2C scan:", i2c.scan());
i2c.write_reg(0x3C, 0x00, [0xAE, 0xD5]);
let reg_data = i2c.read_reg(0x3C, 0x00, 2);
print("I2C read_reg hex:", reg_data.hex(" "));

// SPI
let spi = SPI(1, 18, 23, 19, 1000000);
let rx = spi.transfer([0x9F, 0x40, 0x16]);
print("SPI transfer rx hex:", rx.hex(" "));

// UART
let uart = UART(1, 17, 16, 115200);
uart.write("AT+GMR\r\n");
uart.sim_input("OK");
print("UART any:", uart.any(), "read:", uart.read_str());
