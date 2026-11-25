// ==============================================================================
// 示例程序: 局域网 Web OTA 控制台与无线热重载服务器
// 适用硬件: ESP32 全系列 (真实 WiFi) / PC 宿主环境 (自动演练仿真)
// 规范标准: TcpServer + HTTP REST 接口 + OTA.update_from_string 字符串热写入
// 使用方式:
//   1. 修改下方的 wifi_ssid 和 wifi_pass，烧录到 ESP32
//   2. ESP32 联网后自动启动 8080 端口 Web OTA 服务并输出 IP 地址
//   3. 手机或电脑浏览器打开 http://<板端IP>:8080/ 查看控制台
//   4. 在网页文本框中编写/粘贴 L 代码，点击「无线 OTA 烧录」，板端实时热重载生效！
// ==============================================================================

import "ota";

print("==================================================");
print("     L 语言 局域网 Web OTA 无线热重载服务器");
print("==================================================");

// ------------------------------------------------------------------------------
// WiFi 配置: 如果在真实 ESP32 硬件上运行，请在此配置您的路由器账号与密码
// ------------------------------------------------------------------------------
let wifi_ssid = "CMCC-307";         // 请替换为您的 2.4GHz WiFi 名称
let wifi_pass = "Ni_laomu88";     // 请替换为您的 WiFi 密码 (至少 8 位)

if (!wifi_is_connected()) {
    if (wifi_ssid != "YOUR_WIFI_SSID") {
        print("正在连接 WiFi:", wifi_ssid, "...");
        try {
            wifi_connect(wifi_ssid, wifi_pass, 15000);
            print("WiFi 连接成功! 本机 IP 地址:", wifi_ip());
        } catch (e) {
            print("WiFi 连接失败:", e);
            print("提示: 请检查 SSID 和密码，或靠近路由器重试。");
        }
    } else {
        print("提示: 当前 wifi_ssid 为默认占位符，若需真机联网请修改脚本开头的 wifi_ssid/wifi_pass。");
        if (wifi_ip() != nil) {
            print("检测到当前网络已就绪，本机 IP:", wifi_ip());
        }
    }
} else {
    print("WiFi 已就绪, 本机 IP 地址:", wifi_ip());
}

let port = 8080;
let srv = TcpServer(port);
print("1. Web OTA 服务器已启动, 正在监听端口:", port);
let current_ip = wifi_ip();
if (current_ip != nil) {
    print("   - 打开浏览器访问: http://" + current_ip + ":" + str(port) + "/");
} else {
    print("   - 打开浏览器访问: http://127.0.0.1:" + str(port) + "/ (PC模拟) 或 http://<ESP32-IP>:" + str(port) + "/");
}

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
"<textarea id='code'>// 在此输入需要无线推送到 ESP32 的 L 脚本\nprint(\"★ [Web-OTA] 收到无线推流更新, 正在执行业务任务!\");\nlet count = 100;\nprint(\"当前任务计数:\", count);\n</textarea>" +
"<br><br><button onclick='doOta()'>🚀 一键无线 OTA 烧录到 ESP32</button>" +
"<p id='result' class='status'></p>" +
"</div>" +
"<script>" +
"function doOta() {" +
"  const code = document.getElementById('code').value;" +
"  const resDiv = document.getElementById('result');" +
"  resDiv.style.color = '#38bdf8';" +
"  resDiv.innerText = '正在向 ESP32 发送无线烧录流...';" +
"  fetch('/ota', { method: 'POST', body: code })" +
"    .then(r => r.json())" +
"    .then(data => {" +
"      if (data.ok) {" +
"        resDiv.style.color = '#4ade80';" +
"        resDiv.innerText = '√ 烧录成功! 写入 ' + data.bytes + ' 字节, CRC32=' + data.crc32 + ' (板端已热重载)'; " +
"      } else {" +
"        resDiv.style.color = '#f87171';" +
"        resDiv.innerText = '× 脚本执行出错 (Web OTA 服务保持在线): ' + data.error;" +
"      }" +
"    })" +
"    .catch(e => {" +
"      resDiv.style.color = '#f87171';" +
"      resDiv.innerText = '× 网络请求中断: ' + e;" +
"    });" +
"}" +
"</script></body></html>";

// 处理单次 HTTP 请求
fn handle_http_request(client) {
    print("-> 收到客户端连接，正在读取 HTTP 请求数据...");
    // 浏览器 HTTP GET 请求头通常在 500~1000 字节，此处读取 1460 字节 (单个 MTU)，超时 1000ms
    let req_text = client.recv(1460, 1000);
    if req_text == nil or len(req_text) == 0 {
        print("   [提示] 未收到有效数据或读取超时，关闭连接");
        client.close();
        return nil;
    }

    print("   [HTTP] 收到请求: " + substr(req_text, 0, min(50, len(req_text))) + "...");

    // 区分 GET 与 POST
    if contains(req_text, "GET /") {
        let resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " +
                   str(len(HTML_PAGE)) + "\r\n\r\n" + HTML_PAGE;
        client.send(resp);
        print("   [HTTP] 已成功向浏览器返回 Web OTA 控制台页面 (HTML " + str(len(HTML_PAGE)) + " 字节)");
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

        // 核心步骤: 使用 try-catch 安全沙箱保护！即使新脚本语法报错，网页服务绝不崩溃！
        let success = true;
        let err_msg = "";
        let bytes_written = 0;
        let crc_str = "0x00000000";

        try {
            // 参数: (code_str, expected_crc, auto_exec)
            let res = OTA.update_from_string(body, 0, true);
            bytes_written = res.bytes;
            crc_str = res.crc32;
            print("   [Web-OTA] √ 业务脚本更新并热执行成功 (写入 " + str(bytes_written) + " 字节, CRC=" + crc_str + ")");
        } catch (e) {
            success = false;
            err_msg = str(e);
            print("   [Web-OTA] × 业务脚本运行异常 (已安全隔离，Web 服务不受影响):", err_msg);
        }

        // 回送结构化 JSON 响应给浏览器控制台
        let json_resp = "";
        if success {
            json_resp = "{\"ok\":true,\"bytes\":" + str(bytes_written) + ",\"crc32\":\"" + crc_str + "\"}";
        } else {
            // 转义引号防止破坏 JSON
            json_resp = "{\"ok\":false,\"error\":\"" + replace(replace(err_msg, "\\", "\\\\"), "\"", "'") + "\"}";
        }

        let http_resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\nContent-Length: " +
                        str(len(json_resp)) + "\r\n\r\n" + json_resp;
        client.send(http_resp);
    } else {
        let not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nNot Found";
        client.send(not_found);
    }
    client.close();
    print("<- 客户端请求处理完毕，连接已关闭\n");
}

// ==============================================================================
// 启动常驻 Web OTA HTTP 服务
// ==============================================================================
print("\n2. Web OTA 服务器已启动并处于监听状态，请保持连接。");
print("   - 打开电脑或手机浏览器访问: http://" + str(current_ip) + ":" + str(port) + "/");
print("   - 随时可在网页上编写代码并点击「一键无线 OTA 烧录」");
print("   - (按 Ctrl+C 可停止当前服务)\n");

let count = 0;
while true {
    // 监听等待客户端连接，单次超时 200ms
    let client = srv.accept(200);
    if client != nil {
        handle_http_request(client);
    }
    count = count + 1;
    if count % 25 == 0 {
        // 每约 5 秒输出一次心跳日志
        print("... [Web OTA 心跳] 正在监听 8080 端口, 客户端待命中 (已轮询 " + str(count) + " 次)"+wifi_ip());
    }
    // 每次轮询暂停 10 毫秒，保证 CPU 调度与 WiFi 协议栈响应
    sleep(0.01);
}
