# TCP 客户端与服务端编程

L 语言提供了非阻塞、带超时保护的原生 `Tcp` 客户端与 `TcpServer` 服务端对象，全面兼容 ESP32 与 PC 宿主环境。

---

## 1. TCP 客户端 (`Tcp`)

用于与局域网或公网 TCP 服务器（例如 Socket 调试助手、上位机、数据网关）建立通信：

```l
// 创建并立即连接 TCP 服务端: Tcp(主机名或IP, 端口, [连接超时毫秒=5000])
try {
    let client = Tcp("192.168.10.5", 9000, 3000);
    print("已成功连接到 TCP 服务器!");

    // 发送字符串数据
    client.send("Hello from L-Language ESP32!\r\n");

    // 接收回显数据: 参数 (最大字节数, 超时毫秒)
    let reply = client.recv(1024, 2000);
    if (reply != nil) {
        print("收到服务端响应:", reply);
    }

    client.close();
} catch (e) {
    print("TCP 连接异常:", e);
}
```

---

## 2. TCP 服务端 (`TcpServer`)

让 ESP32 自身成为一个 Socket 服务器，支持多客户端轮询接入与 HTTP 响应：

```l
// 创建并监听端口: TcpServer(端口, [监听地址="0.0.0.0"], [backlog=5])
let port = 8080;
let srv = TcpServer(port);
print("服务端已在端口", port, "启动监听...");

while true {
    // 监听等待客户端连接，单次超时 200ms (超时返回 nil，便于主循环与心跳共存)
    let client = srv.accept(200);
    if (client != nil) {
        print("检测到新客户端接入!");
        let req = client.recv(1024, 1000);
        if (req != nil) {
            print("收到请求报文:\n", req);
            // 回送 HTTP 响应
            let resp = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nHello World!";
            client.send(resp);
        }
        client.close();
    }

    // 必须休眠片刻出让时间片，自动喂看门狗
    sleep(0.01);
}
```
