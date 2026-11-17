// L 语言示例: 轻量嵌入式 HTTP Web 控制后台
// 运行在 PC 或 ESP32 芯片上，提供浏览器直接访问的 Web 状态面板与控制接口。

let port = 8080;
let server = TcpServer(port);
print("[HTTP] Web 控制后台服务已启动: http://127.0.0.1:" + str(port) + "/");
print("[HTTP] 按 Ctrl+C 或等待客户端请求...");

let request_count = 0;
let led_state = false;

// 运行循环 (示例中处理若干次请求或演示超时非阻塞事件轮询)
for (let i = 0; i < 5; i = i + 1) {
    // accept 带 1000ms 超时，未收到连接返回 nil，便于主循环处理心跳任务与外设轮询
    let client = server.accept(1000);
    if (client != nil) {
        let req = client.recv(2048, 2000);
        if (req != nil) {
            request_count = request_count + 1;
            print("[HTTP] 收到客户端请求:\n" + req);

            // 简易路由解析
            let body = "";
            if (req.find("GET /toggle") >= 0) {
                led_state = !led_state;
                body = "{\"status\": \"ok\", \"led\": " + str(led_state) + "}";
            } else {
                body = "<html><head><meta charset='utf-8'><title>L-Lang Web 控制台</title></head>" +
                       "<body style='font-family:sans-serif;padding:20px;'>" +
                       "<h1>🚀 L 语言嵌入式轻量 Web 控制台</h1>" +
                       "<p>服务器状态: <b>正常运行</b></p>" +
                       "<p>累计处理请求数: " + str(request_count) + "</p>" +
                       "<p>虚拟板载 LED 状态: <b>" + (led_state ? "亮 (ON)" : "灭 (OFF)") + "</b></p>" +
                       "<p><a href='/toggle'><button style='padding:10px 20px;font-size:16px;'>切换 LED 开关</button></a></p>" +
                       "</body></html>";
            }

            let resp = "HTTP/1.1 200 OK\r\n" +
                       "Content-Type: text/html; charset=utf-8\r\n" +
                       "Content-Length: " + str(len(body)) + "\r\n" +
                       "Connection: close\r\n\r\n" + body;

            client.send(resp);
        }
        client.close();
    } else {
        // 心跳打印
        // print("[心跳] 正在监听端口 " + str(port) + "...");
    }
}

server.close();
print("[HTTP] 服务端已优雅关闭。");
