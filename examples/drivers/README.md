# L 语言标准外设与常用传感器驱动包

本目录提供纯 L 语言（`lang`）编写的标准硬件外设与传感器驱动库，兼具 **ESP32 真实硬件驱动** 与 **PC 宿主离线仿真** 能力。

---

## 包含驱动列表

| 驱动文件 | 硬件型号 / 外设 | 接口协议 | 核心能力说明 |
| :--- | :--- | :--- | :--- |
| [`SSD1306.l`](SSD1306.l) | 0.96 寸 OLED 显示屏 (128x64) | I2C (默认 `0x3C`) | 显存缓冲、点阵绘制、5x7 ASCII 英文/数字/符号字符绘制、`show()` 批量刷屏 |
| [`AHT20.l`](AHT20.l) | 高精度温湿度传感器 | I2C (默认 `0x38`) | 状态字检测、触发测量、20位高精度湿度与摄氏/华氏温度解析 |
| [`WS2812.l`](WS2812.l) | 单总线 RGB 全彩灯带 (NeoPixel) | GPIO / 单总线 | GRB 24 位色彩缓冲、单个/全局填充、彩虹色轮算法 (`wheel`) |
| [`demo_all.l`](demo_all.l) | 综合工作站演示程序 | I2C + GPIO | 协同读取温湿度传感器，并在 OLED 屏幕与 RGB 状态灯上实时显示 |

---

## 快速使用

### 1. 本机离线模拟运行 (无需硬件)

所有驱动均内置了离线仿真支持，在手头没有真实开发板时，直接在电脑终端执行即可提前跑通全部业务逻辑：

```powershell
l examples/drivers/demo_all.l
```

### 2. 烧录到真实 ESP32 运行

将 ESP32 开发板插入电脑 USB，运行：

```powershell
l -u COM3 examples/drivers/demo_all.l
```

不到 0.2 秒即可通过串口完成固件裸 Flash 分区烧录并立即上屏运行！

---

## 代码示例

```javascript
import "./SSD1306.l";
import "./AHT20.l";

// 1. 初始化 I2C
let i2c = I2C(0, 21, 22, 400000);

// 2. 初始化温湿度传感器与 OLED 屏幕
let aht = AHT20.create(i2c).init();
let oled = SSD1306.create(i2c).init();

// 3. 读取传感器并显示
let data = aht.read();
oled.clear();
oled.text(0, 0,  "ESP32 WEATHER");
oled.text(0, 20, "TEMP: " + str(data.temp_c) + " C");
oled.text(0, 40, "HUMI: " + str(data.humidity) + " %");
oled.show();
```
