// ==============================================================================
// 示例程序: ILI9341 2.8寸 彩色 TFT LCD 屏幕综合演示
// 适用硬件: ESP32 全系列 (SPI 接口，分辨率 240x320，RGB565 16位色彩)
// 接线参考 (ESP32 默认 VSPI / HSPI 引脚):
//   - SCK  -> GPIO 18 (SCLK)
//   - MOSI -> GPIO 23 (SDI)
//   - MISO -> GPIO 19 (可选)
//   - CS   -> GPIO 5  (片选)
//   - DC   -> GPIO 4  (数据/指令选择)
//   - RST  -> GPIO 22 (复位，可选)
//   - BL   -> GPIO 21 (背光使能，可选)
// ==============================================================================

import "./drivers/ILI9341.l" as lcd_lib;

print("========================================");
print("     L 语言 ILI9341 彩色 TFT 屏演示");
print("========================================");

// 1. 初始化 SPI 总线 (总线 1, 频率 40MHz 极速刷新)
let sck_pin  = 18;
let mosi_pin = 23;
let miso_pin = 19;
let cs_pin   = 5;
let dc_pin   = 4;
let rst_pin  = 22;
let bl_pin   = 21;

print("1. 正在初始化 SPI 总线与引脚配置...");
let spi = SPI(1, sck_pin, mosi_pin, miso_pin, 40000000);

// 2. 构造 ILI9341 驱动对象并初始化
print("2. 正在初始化 ILI9341 屏幕寄存器与显存...");
let tft = lcd_lib.ILI9341(spi, cs_pin, dc_pin, rst_pin, bl_pin);

print("   - 初始分辨率:", tft.width, "x", tft.height);
print("   - 旋转方向  : 竖屏模式 (Rotation = 0)");

// 3. 演示全屏纯色填充 (RGB 色彩测试)
print("\n3. 演示色彩填充 (清屏 -> 蓝底)...");
tft.clear(lcd_lib.ILI9341_NAVY);

// 4. 演示几何图形绘制: 边框、填充矩形与彩虹色块
print("4. 绘制多彩卡片与状态看板...");
// 顶栏状态条
tft.fill_rect(0, 0, 240, 28, lcd_lib.ILI9341_DARKCYAN);
tft.text("L-LANG IOT MONITOR", 16, 8, lcd_lib.ILI9341_WHITE, lcd_lib.ILI9341_DARKCYAN, 2);

// 绘制主仪表卡片
tft.rect(10, 40, 220, 100, lcd_lib.ILI9341_CYAN);
tft.fill_rect(12, 42, 216, 96, lcd_lib.ILI9341_DARKGREY);

tft.text("CHIP : " + ESP32.chip(), 20, 52, lcd_lib.ILI9341_GREEN, nil, 2);
tft.text("HEAP : " + str(ESP32.free_heap()) + " B", 20, 74, lcd_lib.ILI9341_YELLOW, nil, 2);
tft.text("STATUS: RUNNING OK", 20, 96, lcd_lib.ILI9341_WHITE, nil, 2);

// 5. 绘制圆形与指示灯 (实心圆与同心圆)
print("5. 绘制状态指示灯与同心圆...");
tft.fill_circle(40, 175, 18, lcd_lib.ILI9341_GREEN);
tft.circle(40, 175, 22, lcd_lib.ILI9341_WHITE);
tft.text("WIFI", 70, 168, lcd_lib.ILI9341_WHITE, nil, 2);

tft.fill_circle(140, 175, 18, lcd_lib.ILI9341_BLUE);
tft.circle(140, 175, 22, lcd_lib.ILI9341_WHITE);
tft.text("BLE", 170, 168, lcd_lib.ILI9341_WHITE, nil, 2);

// 6. 演示动态色轮条与渐变线条 (Bresenham 算法)
print("6. 绘制动态色条与交叉线条...");
let colors = [
    lcd_lib.ILI9341_RED,
    lcd_lib.ILI9341_ORANGE,
    lcd_lib.ILI9341_YELLOW,
    lcd_lib.ILI9341_GREEN,
    lcd_lib.ILI9341_CYAN,
    lcd_lib.ILI9341_BLUE,
    lcd_lib.ILI9341_MAGENTA
];

for (let i = 0; i < len(colors); i = i + 1) {
    let bar_x = 15 + (i * 30);
    tft.fill_rect(bar_x, 220, 24, 40, colors[i]);
}

// 底部线条与版本号
tft.line(10, 275, 230, 275, lcd_lib.ILI9341_LIGHTGREY);
tft.text("L Language v0.3.2 TFT UI", 25, 290, lcd_lib.ILI9341_LIGHTGREY, nil, 1);

print("========================================");
print("ILI9341 显示屏演示绘制完毕！");
print("提示: 接入真实 ILI9341 开发板即可直接看到高帧率彩屏 UI。");
