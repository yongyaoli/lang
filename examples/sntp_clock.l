// ==============================================================================
// 示例程序: SNTP 网络自动对时与物联网数字时钟
// 适用硬件: ESP32 全系列 (真机联网需先连接 WiFi)
// 说明: 通过 NTP 服务器（如 pool.ntp.org / ntp.aliyun.com）同步国际标准时间，
//      并计算时区偏移，实现无需外置 RTC 芯片的精准网络日历时钟。
// ==============================================================================

print("========================================");
print("     L 语言 SNTP 网络对时与时钟");
print("========================================");

// 1. 确保连网
if (!wifi_is_connected()) {
    print("正在连接网络以获取精准时间...");
    wifi_connect("MyHome_WiFi", "12345678", 10000);
}

// 2. 发起 SNTP 网络对时
// 默认服务器: "pool.ntp.org"，国内用户可传 "ntp.aliyun.com" 或 "cn.pool.ntp.org"
let ntp_server = "ntp.aliyun.com";
print("正在通过 NTP 服务器同步现实时间:", ntp_server, "...");

let sync_ok = sntp_sync(ntp_server, 8000);
if (sync_ok) {
    print("对时成功！当前系统时间已与国家授时中心对齐。");
} else {
    print("对时超时或失败，使用当前系统时钟继续。");
}

// 3. 读取当前时间戳并格式化
let now_ts = time();
print("\n当前 UTC 时间戳 (秒):", now_ts);

// 东八区 (北京时间 UTC+8) 偏移 8 * 3600 秒
let beijing_ts = now_ts + (8 * 3600);

// 将时间戳拆解为简易时分秒 (L 语言两个整数相除为 C 语义整除)
let total_seconds = beijing_ts % 86400;
let hours = total_seconds / 3600;
let minutes = (total_seconds % 3600) / 60;
let seconds = total_seconds % 60;

// 补零格式化
fn pad2(num) {
    let s = str(num);
    if (len(s) < 2) {
        return "0" + s;
    }
    return s;
}

let time_str = pad2(hours) + ":" + pad2(minutes) + ":" + pad2(seconds);

print("----------------------------------------");
print("      北京时间 (CST / UTC+8)");
print("           " + time_str);
print("----------------------------------------");

// 4. 模拟时钟走针心跳 (循环走动 3 秒)
print("\n启动时钟实时走针心跳 (每秒刷新一次):");

for (let tick = 0; tick < 3; tick = tick + 1) {
    ESP32.delay_ms(1000);
    seconds = seconds + 1;
    if (seconds >= 60) {
        seconds = 0;
        minutes = minutes + 1;
        if (minutes >= 24) {
            minutes = 0;
            hours = (hours + 1) % 24;
        }
    }
    print("  [TICK #" + str(tick + 1) + "] " + pad2(hours) + ":" + pad2(minutes) + ":" + pad2(seconds));
}

print("\n时钟走针正常，演示结束。");
