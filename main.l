// L 语言 ESP32 IoT 应用程序
// 功能:
// 1. 自动连接 2.4GHz WiFi 网络
// 2. 联网后自动通过 SNTP 与网络时间服务器同步现实世界时钟
// 3. 每 2 秒闪烁一次板载 LED 指示灯 (GPIO 2)
// 4. 每 1 秒在控制台格式化输出一次当前时间

print("========================================");
print("     L 语言 ESP32 IoT 应用启动");
print("========================================");

// ===================== 1. WiFi 配置 =====================
// 请将下方 SSID 和密码替换为您真实的 2.4GHz WiFi 热点信息
let wifi_ssid = "YOUR_WIFI_SSID";
let wifi_pass = "YOUR_WIFI_PASSWORD";

print("[WiFi] 正在连接网络:", wifi_ssid, "...");
let ok = wifi_connect(wifi_ssid, wifi_pass, 15000);

if ok {
    print("[WiFi] 连接成功!");
    print("[WiFi] 设备局域网 IP:", wifi_ip());
    print("[WiFi] 网卡 MAC 地址:", wifi_mac());

    // ===================== 2. 网络现实对时 (SNTP) =====================
    print("[SNTP] 正在向阿里云 NTP 服务器同步北京时间...");
    if sntp_sync("ntp.aliyun.com", 8000) {
        print("[SNTP] 时钟同步成功! 当前真实时间:", datetime());
    } else {
        print("[SNTP] 对时超时，将使用系统累计时间运行");
    }
} else {
    print("[WiFi] 未能连接到 WiFi (可能未修改密码或不在信号范围内)");
    print("[WiFi] 将以离线模式继续运行本地定时器与闪灯逻辑");
}

// ===================== 3. 板载 LED 初始化 =====================
// ESP32 NodeMCU / DevKit 开发板板载 LED 通常为 GPIO 2 (输出模式)
let led_pin = 2;
let led = Pin(led_pin, "out");
led.off();
print("[LED]  已初始化板载 LED (GPIO", led_pin, ")");

// ===================== 4. 主工作循环 =====================
print("\n[Loop] 开始进入主循环 (每 1 秒输出时间, 每 2 秒闪灯一次)...");
let ticks = 0;

while true {
    // 每一秒输出一下当前时间 (对时成功后输出形如 "2026-11-09 19:35:00")
    print("[当前时间]", datetime());

    // 每 2 秒翻转闪烁一次板载 LED
    if ticks % 2 == 0 {
        led.toggle();
        print("  -> LED 电平翻转, 当前状态:", led.value());
    }

    ticks = ticks + 1;
    // 延时 1 秒
    sleep(1);
}
