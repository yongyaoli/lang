// ============================================================================
// L 语言智能硬件工作站演示程序 (OLED + AHT20 温湿度 + WS2812 状态指示)
// ============================================================================
import "./SSD1306.l";
import "./AHT20.l";
import "./WS2812.l";

print("=================================================");
print("     L 语言硬件工作站演示 (OLED + AHT20 + WS2812)");
print("=================================================");

// 1. 初始化 I2C 总线 (ESP32 默认: SDA=21, SCL=22)
let i2c = I2C(0, 21, 22, 400000);
print("[I2C] 总线就绪, 扫描地址:", i2c.scan());

// 2. 初始化 AHT20 温湿度传感器 (地址 0x38)
let aht = AHT20.create(i2c, 0x38).init();
print("[AHT20] 传感器初始化完成");

// 3. 初始化 SSD1306 0.96寸 OLED 显示屏 (地址 0x3C, 128x64)
let oled = SSD1306.create(i2c, 0x3C, 128, 64).init();
print("[OLED]  显示屏初始化完成");

// 4. 初始化 WS2812 RGB 灯带 (GPIO 4, 8 颗灯珠)
let rgb = WS2812.create(4, 8).clear();
print("[WS2812] RGB 灯带初始化完成");

// 5. 读取温湿度数据
let data = aht.read();
print("[数据] 当前温度:", data.temp_c, "℃ | 湿度:", data.humidity, "%");

// 6. 将数据渲染到 OLED 屏幕
oled.clear();
oled.text(0, 0,  "=== L LANG IOT ===");
oled.text(0, 16, "TEMP: " + str(data.temp_c) + " C");
oled.text(0, 32, "HUMI: " + str(data.humidity) + " %");
oled.text(0, 48, "CHIP: " + ESP32.chip());
oled.show();
print("[OLED]  画面渲染刷新完毕");

// 7. 根据温度给 RGB 灯带设置指示颜色 (绿色舒适, 蓝色凉爽, 红色过热)
if data.temp_c > 30.0 {
    rgb.fill(255, 0, 0); // 红色
} else if data.temp_c < 18.0 {
    rgb.fill(0, 0, 255); // 蓝色
} else {
    rgb.fill(0, 255, 0); // 舒适绿色
}
rgb.show();
print("[RGB]   状态指示灯已同步刷新");

print("=================================================");
print("【演示成功】全部外设驱动协同工作正常！");
print("=================================================");
