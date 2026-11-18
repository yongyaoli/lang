// ==============================================================================
// 示例程序: 周围 WiFi 热点扫描与信号强度分析
// 适用硬件: ESP32 / ESP32-S3 / ESP32-C3 / ESP32-C6 (亦兼容 PC 模拟模式)
// 说明: 扫描设备周围 2.4GHz 无线热点，展示 SSID 名称、RSSI 信号强度与加密方式
// ==============================================================================

print("========================================");
print("     L 语言 WiFi 热点扫描助手");
print("========================================");

// 1. 获取当前网卡物理 MAC 地址
let mac = wifi_mac();
print("本机网卡 MAC:", mac);
print("正在扫描周围 2.4GHz 无线网络，请稍候...\n");

// 2. 发起周围热点扫描
// 在真机上调用底层 esp_wifi_scan_start，在 PC 上返回模拟的真实热点列表
let ap_list = wifi_scan();
let count = len(ap_list);

print("扫描完成！共发现", count, "个可用热点:\n");
print("序号 | 信号等级 | RSSI (dBm) | 加密类型  | 热点名称 (SSID)");
print("------------------------------------------------------------");

for (let i = 0; i < count; i = i + 1) {
    let ap = ap_list[i];
    let rssi = ap.rssi;
    let ssid = ap.ssid;
    let auth = ap.auth;

    // 根据 RSSI 评估信号强弱
    let signal_desc = "弱";
    if (rssi >= -60) {
        signal_desc = "强";
    } else if (rssi >= -75) {
        signal_desc = "中";
    }

    // 格式化输出 (保持对齐)
    let idx_str = str(i + 1);
    if (i + 1 < 10) {
        idx_str = " " + idx_str;
    }
    print(" " + idx_str + "  |   " + signal_desc + "   |   " + str(rssi) + " dBm  | " + auth + " | " + ssid);
}

print("------------------------------------------------------------");
print("提示: 可使用 wifi_connect(\"热点名称\", \"密码\") 连接上述任意网络。");
