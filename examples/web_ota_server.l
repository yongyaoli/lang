// ==============================================================================
// 示例程序: 局域网 Web OTA 控制台与无线热重载服务器
// 适用硬件: ESP32 全系列 (真实 WiFi) / PC 宿主环境 (自动演练仿真)
// 规范标准: TcpServer + HTTP REST 接口 + OTA.update_from_string 字符串热写入
// 使用方式:
//   1. ESP32 联网后自动启动 8080 端口 Web OTA 服务
//   2. 手机或电脑浏览器打开 http://<板端IP>:8080/ 查看控制台
//   3. 在网页文本框中编写/粘贴 L 代码，点击「无线 OTA 烧录」，板端实时热重载生效！
// ==============================================================================

import "ota";

print("==================================================");
print("     L 语言 局域网 Web OTA 无线热重载服务器");
print("==================================================");

let port = 8080;
let srv = TcpServer(port);
print("1. Web OTA 服务器已启动, 正在监听端口:", port);
print("   - 打开浏览器访问: http://127.0.0.1:8080/ (PC模拟) 或 http://<ESP32-IP>:8080/");

// HTML 网页控制台界面模板
let HTML_PAGE = "<!DOCTYPE html><html><head><meta charset='utf-8'>" +
"<title>L 语言 ESP32 Web OTA 控制台</title>" +
"<style>" +
"body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; max-width: 800px; margin: 30px auto; padding: 0 20px; background: #0f172a; color: #f8fafc; }" +
"h1 { color: #38bdf8; font-size: 24px; border-bottom: 1px solid #334155; padding-bottom: 12px; }" +
".card { background: #1e293b; border-radius: 8px; padding: 20px; margin-bottom: 20px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.1); }" +
"textarea { width: 100%; height: 220px; background: #090d16; color: #38bdf8; font-family: 'Consolas', monospace; font-size: 14px; border: 1px solid #475569; border-radius: 6px; padding: 12px; box-sizing: border-box; }" +
"button { background: #0284c7; color: white; border: none; padding: 10px 24px; font-size: 15px; font-weight: 600; border-radius: 6px; cursor: pointer; transition: background 0.2s; }" +
"button:hover { background: #0369a1; }" +
".status { color: #94a3b8; font-size: 13px; margin-top: 10px; }" +
"</style></head><body>" +
"<h1>⚡ L 语言 ESP32 局域网 Web OTA 控制台</h1>" +
"<div class='card'>" +
"<h3>当前硬件 Flash 状态</h3>" +
"<p class='status'>存储分区: l_script (裸 Flash 零 SRAM 映射) | 端口: 8080</p>" +
"</div>" +
"<div class='card'>" +
"<h3>编写或粘贴最新 L 脚本:</h3>" +
"<textarea id='code'>// 在此输入需要无线推送到 ESP32 的 L 脚本\nprint('★ [Web-OTA] 收到无线推流更新, 正在执行业务任务!');\nlet count = 100;\nprint('当前任务计数:', count);\n</textarea>" +
"<br><br><button onclick='doOta()'>🚀 一键无线 OTA 烧录到 ESP32</button>" +
"<p id='result' class='status'></p>" +
"</div>" +
"<script>" +
"function doOta() {" +
"  const code = document.getElementById('code').value;" +
"  document.getElementById('result').innerText = '正在向 ESP32 发送无线烧录流...';" +
"  fetch('/ota', { method: 'POST', body: code })" +
"    .then(r => r.json())" +
"    .then(data => { document.getElementById('result').innerText = '√ 烧录成功! 写入 ' + data.bytes + ' 字节, CRC32=' + data.crc32 + ' (板端已热重载)'; })" +
"    .catch(e => { document.getElementById('result').innerText = '× 烧录失败: ' + e; });" +
"}" +
"</script></body></html>";

// 处理单次 HTTP 请求
fn handle_http_request(client) {
    let req_text = client.recv(4096, 500);
    if req_text == nil or len(req_text) == 0 {
        client.close();
        return nil;
    }

    // 区分 GET 与 POST
    if contains(req_text, "GET / ") or contains(req_text, "GET /index.html") {
        let resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " +
                   str(len(HTML_PAGE)) + "\r\n\r\n" + HTML_PAGE;
        client.send(resp);
        print("   [HTTP] 已向客户端返回 Web OTA 网页控制台 (HTML)");
    } else if contains(req_text, "POST /ota") {
        // 提取 HTTP POST 正文 (以 \r\n\r\n 为界)
        let body_start = find(req_text, "\r\n\r\n");
        let body = "";
        if body_start >= 0 {
            body = substr(req_text, body_start + 4);
        } else {
            body = req_text;
        }

        print("   [Web-OTA] 收到浏览器推送的新脚本源码 (长度: " + str(len(body)) + " 字节)");

        // 核心步骤: 调用 OTA.update_from_string 写入 Flash 分区并热执行
        let res = OTA.update_from_string(body, 0, true);

        // 回送 JSON 响应
        let json_resp = "{\"ok\":true,\"bytes\":" + str(res.bytes) + ",\"crc32\":\"" + res.crc32 + "\"}";
        let http_resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                        str(len(json_resp)) + "\r\n\r\n" + json_resp;
        client.send(http_resp);
        print("   [Web-OTA] 响应完成: 已更新 Flash 分区并热重载");
    } else {
        let not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nNot Found";
        client.send(not_found);
    }
    client.close();
}

// 演练模式: 模拟一次 Web OTA 烧录流程
print("\n2. 演练 Web OTA 客户端请求...");
let mock_upload_code = "print(\"★ [Client-Mock] Web OTA 烧录测试成功！\");";

// 构造模拟 HTTP POST 报文
let mock_http_post = "POST /ota HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nContent-Length: " +
                     str(len(mock_upload_code)) + "\r\n\r\n" + mock_upload_code;

// 使用 Tcp 客户端连接自身端口进行高保真闭环演练
let cli = Tcp("127.0.0.1", port, 2000);
cli.send(mock_http_post);

// 服务端接受并处理
let conn = srv.accept(1000);
if conn != nil {
    handle_http_request(conn);
}

// 客户端读取响应
let reply = cli.recv(1024, 1000);
print("   [客户端接收响应]:", reply);
cli.close();

// 关闭服务端
srv.close();
print("\n3. Web OTA 控制台演练结束，服务器安全关闭。");
print("\n==================================================");
print("     局域网 Web OTA 服务器示例程序顺利完成！");
print("==================================================");
