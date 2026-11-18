// ==============================================================================
// 示例程序: ESP32 SoftAP 热点模式与本地配网控制台
// 适用硬件: ESP32 全系列
// 说明: 开启 ESP32 自身为无线路由器(AP 模式)，并启动轻量 Web 服务端。
//      手机或电脑搜索并连接热点后，浏览器访问 http://192.168.4.1 即可打开配置后台。
// ==============================================================================

print("========================================");
print("     L 语言 SoftAP 热点与配网后台");
print("========================================");

// 1. 设置热点名称与密码
let ap_ssid = "L-Lang-AP";
let ap_pwd  = "88888888"; // 密码至少 8 位，留空 "" 则为开放无密码热点

print("1. 正在启动本地无线热点 (SoftAP)...");
let ap_ok = wifi_ap(ap_ssid, ap_pwd);

if (!ap_ok) {
    print("热点启动失败！");
} else {
    print("热点已就绪！");
    print("   - 热点名称:", ap_ssid);
    print("   - 连接密码:", ap_pwd);
    print("   - 默认网关:", wifi_ip());
}

// 2. 启动轻量级 Web 服务器监听 80 端口
let server_port = 80;
let server = TcpServer(server_port);
print("\n2. 启动 Web 配置控制台, 监听端口:", server_port);
print("   请用手机连接热点后，打开浏览器访问: http://" + wifi_ip());

// 模拟状态数据
let boot_time = ESP32.ticks_ms();
let req_count = 0;

print("\n3. 进入 Web 请求轮询监听中 (按 Ctrl+C 退出)...");

// 在示例中循环轮询 3 次或等待外部连接 (演示流程)
for (let cycle = 0; cycle < 3; cycle = cycle + 1) {
    // accept 超时时间为 1000ms，轮询友好，不卡死单片机心跳与定时器
    let client = server.accept(1000);

    if (client != nil) {
        req_count = req_count + 1;
        print("\n[收到接入请求 #" + str(req_count) + "]");

        // 读取客户端发送的 HTTP 请求头
        let req_text = client.recv(1024, 1000);
        if (req_text != nil) {
            let first_line = req_text.split("\r\n")[0];
            print("   客户端请求:", first_line);
        }

        // 构造 HTML 网页响应正文
        let uptime_sec = (ESP32.ticks_ms() - boot_time) / 1000;
        let html = "<html><head><meta charset='utf-8'><title>L-Lang AP Dashboard</title>" +
                   "<style>body{font-family:sans-serif;padding:20px;background:#f4f6f8;}" +
                   ".card{background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}" +
                   "h2{color:#0066cc;}table{width:100%;border-collapse:collapse;margin-top:10px;}" +
                   "td,th{padding:8px;border-bottom:1px solid #eee;text-align:left;}</style></head>" +
                   "<body><div class='card'>" +
                   "<h2>🌿 ESP32 设备配置后台</h2>" +
                   "<table>" +
                   "<tr><th>配置项</th><th>当前状态</th></tr>" +
                   "<tr><td>芯片型号</td><td>" + ESP32.chip() + "</td></tr>" +
                   "<tr><td>运行时间</td><td>" + str(uptime_sec) + " 秒</td></tr>" +
                   "<tr><td>剩余堆内存</td><td>" + str(ESP32.free_heap()) + " 字节</td></tr>" +
                   "<tr><td>热点名称</td><td>" + ap_ssid + "</td></tr>" +
                   "<tr><td>设备 IP</td><td>" + wifi_ip() + "</td></tr>" +
                   "</table>" +
                   "<p style='color:#666;font-size:12px;margin-top:16px;'>由 L 语言 TcpServer 原生驱动提供服务</p>" +
                   "</div></body></html>";

        // 构造 HTTP/1.1 标准响应报文
        let resp = "HTTP/1.1 200 OK\r\n" +
                   "Content-Type: text/html; charset=utf-8\r\n" +
                   "Content-Length: " + str(len(html)) + "\r\n" +
                   "Connection: close\r\n\r\n" +
                   html;

        // 发送数据并关闭连接
        client.send(resp);
        client.close();
        print("   网页分发完成，已关闭客户端连接。");
    } else {
        print("   [轮询] 等待客户端连入中... (" + str(cycle + 1) + "/3)");
    }
}

// 4. 清理并退出
server.close();
print("\n服务器已停止，演示结束。");
