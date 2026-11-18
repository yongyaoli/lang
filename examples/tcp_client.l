// ==============================================================================
// 示例程序: TCP 客户端 Socket 通信 (长连接与超时重试)
// 适用硬件: ESP32 全系列 (真机运行需已连入局域网)
// 说明: 演示如何使用 Tcp 类建立双向 Socket 连接、发送遥测数据、接收服务端响应与超时控制
// ==============================================================================

print("========================================");
print("     L 语言 TCP 客户端通信演示");
print("========================================");

// 1. 检查网络连接状态
if (!wifi_is_connected()) {
    print("1. 正在初始化网络连接...");
    wifi_connect("MyHome_WiFi", "12345678", 10000);
}

// 2. 目标服务端配置 (可改为您局域网内的服务器 IP，如 "192.168.1.100")
let host = "127.0.0.1";
let port = 23075; // 若本地未运行回显服务，将演示优雅捕获连接异常

print("2. 正在尝试连接 TCP 服务端 -> " + host + ":" + str(port) + " (超时 3 秒)...");

try {
    let sock = Tcp(host, port, 3000);
    print("   TCP 连接已成功建立！");

    // 查看套接字信息
    let info = sock.info();
    print("   连接信息: " + info.host + ":" + str(info.port) + ", 连接有效:" + str(info.connected));

    // 3. 发送数据
    let ping_msg = "PING: Device " + ESP32.chip() + " Online\n";
    print("\n3. 发送数据帧:", ping_msg.trim());
    sock.send(ping_msg);

    // 4. 等待并接收服务端回显/响应 (超时 2 秒)
    print("4. 等待服务端回执 (超时 2000ms)...");
    let reply = sock.recv(1024, 2000);

    if (reply != nil) {
        print("   -> 收到服务端响应:", reply.trim());
    } else {
        print("   -> 等待超时，服务端未返回数据。");
    }

    // 5. 关闭套接字
    sock.close();
    print("5. 会话结束，套接字已主动关闭。");

} catch (e) {
    print("\n[连接提示] 无法接入目标服务器: " + host + ":" + str(port));
    print("   错误原因:", e.message);
    print("   排查建议: 确保目标主机已开启对应端口的 TCP 服务 (如使用 Netcat: nc -l " + str(port) + ")");
}

print("========================================");
print("TCP 客户端示例执行完毕。");
