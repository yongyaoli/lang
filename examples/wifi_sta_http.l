// ==============================================================================
// 示例程序: WiFi 联网与 HTTP/HTTPS API 通信
// 适用硬件: ESP32 全系列 (真机连网需替换为实际 WiFi 名称与密码)
// 说明: 演示热点连接、网络状态查询、HTTP GET 数据拉取与 HTTP POST 遥测上报
// ==============================================================================

print("========================================");
print("     L 语言 WiFi 联网与 HTTP 通信");
print("========================================");

// 1. 配置 WiFi 热点参数 (请按需修改为你家里的路由器名称和密码)
let target_ssid = "MyHome_WiFi";
let target_pwd  = "12345678";

print("1. 正在连接 WiFi:", target_ssid, "...");

// 连接 WiFi (超时时间设为 15 秒)
let ok = wifi_connect(target_ssid, target_pwd, 15000);

if (!ok) {
    print("WiFi 连接失败！请检查热点名称与密码是否正确。");
    // 若在 PC 模拟器下或离线运行，提示模拟信息并继续演示
    print("(当前处于模拟环境或网络未就绪，继续执行离线流程)");
} else {
    print("WiFi 连接成功！");
}

// 2. 查看当前网络状态与分配的 IP
print("\n2. 网络连接状态:");
print("   - 连接状态:", wifi_is_connected() ? "已就绪 (ONLINE)" : "离线 (OFFLINE)");
print("   - 热点名称:", wifi_ssid());
print("   - 局域网 IP:", wifi_ip());
print("   - 网卡 MAC :", wifi_mac());

// 3. 发起 HTTP GET 请求 (拉取网络数据)
print("\n3. 发起 HTTP GET 请求 (拉取远程服务器数据)...");
let get_url = "http://httpbin.org/get";
let res_get = http_get(get_url, 8000);

print("   - HTTP 状态码:", res_get.status);
if (res_get.ok) {
    print("   - 请求成功！响应体长度:", len(res_get.body), "字节");
    // 截取前 120 字符预览
    let preview = res_get.body;
    if (len(preview) > 120) {
        preview = preview.substr(0, 120) + "...";
    }
    print("   - 响应内容预览:\n" + preview);
} else {
    print("   - 请求失败:", res_get.error);
}

// 4. 发起 HTTP POST 请求 (上传传感器数据)
print("\n4. 发起 HTTP POST 请求 (上报数据)...");
let post_url = "http://httpbin.org/post";
// 构造上报数据载荷 (使用刚刚落地的 JSON 库格式化)
let payload = JSON.stringify({
    device: "esp32_iot_node",
    temp: 26.8,
    humidity: 58,
    status: "normal"
});

let headers = "Content-Type: application/json\r\n";
let res_post = http_post(post_url, payload, headers, 8000);

print("   - HTTP 状态码:", res_post.status);
if (res_post.ok) {
    print("   - POST 数据上报成功！");
} else {
    print("   - POST 上报失败:", res_post.error);
}

print("\n========================================");
print("通信流程执行完毕，5 秒后断开网络。");
ESP32.delay_ms(1000);

// 5. 断开连接释放网卡
wifi_disconnect();
print("WiFi 已安全断开，当前连网状态:", wifi_is_connected());
