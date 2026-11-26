# WiFi、SNTP 对时与网络诊断

L 语言内置了针对 ESP32 无线射频协议栈的高可靠网络通信驱动，支持 Station (STA) 模式连接路由器、周围热点扫描与 SNTP 国际授时对时。

---

## 1. 扫描周围 2.4GHz WiFi 热点 (`wifi_scan`)

```l
// 扫描周围可用无线网络
let ap_list = wifi_scan();
let count = len(ap_list);
print("发现", count, "个 WiFi 热点:\n");

for (let i = 0; i < count; i = i + 1) {
    let ap = ap_list[i];
    print(i + 1, "SSID:", ap.ssid, "信号(RSSI):", ap.rssi, "dBm, 加密:", ap.auth);
}
```

---

## 2. 连接 WiFi 路由器 (`wifi_connect`)

```l
let ssid = "MyHome_WiFi";
let pass = "12345678";

print("正在连接 WiFi:", ssid, "...");
try {
    // 连接 WiFi: 参数 (SSID, 密码, 超时毫秒)
    wifi_connect(ssid, pass, 15000);
    print("WiFi 连接成功!");
    print("本机内网 IP 地址:", wifi_ip());
    print("网卡物理 MAC 地址:", wifi_mac());
} catch (e) {
    print("WiFi 连接失败:", e);
}
```

### 网络诊断辅助 API：
* `wifi_is_connected()`：返回布尔值 `true` 或 `false`，可用于心跳检测断线重连；
* `wifi_ip()`：获取 DHCP 分配到的局域网 IPv4 地址；
* `wifi_mac()`：获取网卡物理 MAC；
* `wifi_disconnect()`：断开当前 WiFi。

---

## 3. SNTP 网络授时对时 (`sntp_sync`)

单片机断电后时间通常会丢失。连上 WiFi 后只需调用 `sntp_sync` 即可同步毫秒级国际时间：

```l
if (wifi_is_connected()) {
    print("正在通过 NTP 服务器同步国际北京时间...");
    try {
        // 同步时间: 参数 (时区偏移秒数, 默认 28800 即 UTC+8 东八区)
        sntp_sync(28800);
        print("当前系统标准时间:", datetime());
    } catch (e) {
        print("对时失败:", e);
    }
}
```
