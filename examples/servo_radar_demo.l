// ==============================================================================
// 示例程序: 超声波倒车雷达与舵机全景雷达扫描仪
// 适用硬件: ESP32 全系列 (真实 Trig/Echo/PWM 引脚) / PC 宿主环境 (自动演练仿真)
// 外设连接:
//   - HC-SR04 超声波模块: Trig 接 GPIO 5, Echo 接 GPIO 18
//   - 标准 180° 舵机: 信号线 接 GPIO 2
// 业务逻辑:
//   1. 舵机带动超声波探头从 30° 平滑平移至 150°
//   2. 实时采样当前方位距离，打印出雷达 ASCII 扫描波形
//   3. 距离小于 15cm 时触发近距避障预警！
// ==============================================================================

import "drivers/HC_SR04.l";
import "drivers/Servo.l";

print("==================================================");
print("     L 语言 超声波避障测距与舵机雷达扫描站");
print("==================================================");

// 1. 初始化超声波测距传感器 (Trig=5, Echo=18)
let radar = HC_SR04.create(5, 18);
radar.set_temperature(24.5); // 设置当前室温为 24.5℃ 开启声速高精补偿
print("1. HC-SR04 超声波测距传感器初始化完成 (Trig: 5, Echo: 18)");

// 2. 初始化 180° 舵机 (PWM 信号接 GPIO 2)
let servo = Servo.create(2);
print("2. 舵机平滑角度控制器初始化完成 (PWM Pin: 2, 频率: 50Hz)");

// 3. 单点静止测距测试
print("\n3. 执行单点静态测距采样:");
let dist_cm = radar.read_cm();
let dist_mm = radar.read_mm();
print("   - 测距距离 (厘米):", dist_cm, "cm");
print("   - 测距距离 (毫米):", dist_mm, "mm");

// 4. 多次滤波平滑测距
let smooth_d = radar.read_smooth_cm(5);
print("   - 5次滤波中位数距离:", smooth_d, "cm");

// 5. 舵机平滑旋转测试 (0° -> 90° -> 180°)
print("\n4. 演示舵机角度平滑控制与平移:");
print("   - 舵机归位至 0 度...");
servo.write(0.0);
ESP32.delay_ms(100);

print("   - 舵机平滑过渡至 90 度...");
servo.sweep_to(90.0, 5.0, 10);
print("   - 当前舵机角度:", servo.read(), "度");

// 6. 模拟全景雷达扫描 (从 30° 扫描至 150°)
print("\n5. 启动扇区全景雷达测距扫描 (30° ~ 150°):");
print("--------------------------------------------------");
print(" 角度(度) | 障碍物距离(cm) | 雷达可视化分布图");
print("--------------------------------------------------");

for (let deg = 30; deg <= 150; deg = deg + 30) {
    servo.write(deg);
    ESP32.delay_ms(50); // 等待探头转动稳定

    let d = radar.read_cm();

    // 构建可视化雷达条形图 (每 2cm 显示一个 '#')
    let bar_len = floor(d / 2.0);
    if (bar_len > 25) { bar_len = 25; }
    let bar = "";
    for (let b = 0; b < bar_len; b = b + 1) {
        bar = bar + "#";
    }

    let deg_str = str(deg);
    while len(deg_str) < 7 { deg_str = " " + deg_str; }

    let d_str = str(d);
    while len(d_str) < 14 { d_str = " " + d_str; }

    let warn = "";
    if (d < 15.0 and d > 0.0) {
        warn = " [⚠️ 碰撞警告!]";
    }

    print(deg_str + " | " + d_str + " | " + bar + warn);
}

// 7. 测试完毕释放舵机输出
servo.detach();
print("--------------------------------------------------");
print("\n6. 雷达测距扫描全部完成，舵机输出已安全释放 (PWM Detached)。");
print("==================================================");
