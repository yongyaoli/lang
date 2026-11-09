// 测试 18: M5 阶段 - 网络通信与 IoT 核心支持 (WiFi / HTTP / SNTP)

fn assert(cond, msg) {
    if not cond {
        print("断言失败:", msg);
        exit(1);
    }
}

print("--- 测试 1. WiFi 状态与网络属性 ---");
assert(wifi_is_connected() == true, "PC 宿主默认处于连网状态");
let ip = wifi_ip();
print("获取到当前局域网 IP 状态:", ip != nil);
assert(ip != nil and len(ip) > 6, "IP 地址格式有效");

let mac = wifi_mac();
print("获取到 MAC 地址格式有效:", len(mac) == 17);
assert(len(mac) == 17, "MAC 地址长度应为 17 (含冒号)");

print("--- 测试 2. WiFi 热点扫描 ---");
let aps = wifi_scan();
assert(type(aps) == "array", "wifi_scan 返回数组");
assert(len(aps) >= 1, "应至少扫描到 1 个热点");
print("扫描到热点有效:", len(aps) >= 1);
for let i = 0; i < len(aps); i = i + 1 {
    let ap = aps[i];
    assert(type(ap) == "map", "热点为 map 类型");
    assert(ap.ssid != nil, "热点包含 ssid");
    assert(ap.rssi < 0, "RSSI 信号强度为负数");
    assert(ap.auth != nil, "热点包含加密类型");
}

print("--- 测试 3. SoftAP 热点模式开启 ---");
let ap_res = wifi_ap("L_DEVICE_AP", "12345678");
assert(ap_res == true, "开启 SoftAP 成功");
print("开启 SoftAP 成功:", ap_res);

print("--- 测试 4. WiFi 断开与重连 ---");
let dis_res = wifi_disconnect();
assert(dis_res == true, "断开 WiFi 成功");
assert(wifi_is_connected() == false, "断开后处于未连接状态");
assert(wifi_ip() == nil, "未连接时 wifi_ip() 应返回 nil");
print("断开 WiFi 成功:", dis_res);

let conn_res = wifi_connect("MyTestWiFi", "password888");
assert(conn_res == true, "重新连接 WiFi 成功");
assert(wifi_is_connected() == true, "连接后状态恢复");
assert(wifi_ip() != nil, "连接后获得 IP");
print("重新连接 WiFi 成功:", conn_res);

print("--- 测试 5. SNTP 网络对时 ---");
let sntp_res = sntp_sync("pool.ntp.org", 5000);
assert(sntp_res == true, "SNTP 对时成功");
print("SNTP 对时成功:", sntp_res);

print("--- 测试 6. HTTP 客户端基础交互 ---");
// 测试无效 URL 容错
let bad_resp = http_get("http://invalid.url.that.does.not.exist.9999");
assert(type(bad_resp) == "map", "http_get 返回响应字典");
assert(bad_resp.status != nil, "响应包含 status 字段");
assert(bad_resp.body != nil, "响应包含 body 字段");
print("HTTP 客户端响应结构正常:", type(bad_resp) == "map");

print("M5 网络测试全部通过！");
