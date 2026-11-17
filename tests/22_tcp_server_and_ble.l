// ============================================
// 测试 22: TcpServer 服务端与 BLE 低功耗蓝牙
// ============================================

fn assert(cond, msg) {
    if not cond {
        print("断言失败:", msg);
        exit(1);
    }
}

print("--- 测试 1. TcpServer 参数校验 ---");
let caught1 = false;
try {
    TcpServer(99999);
} catch (e) {
    caught1 = true;
    print("捕获非法端口诊断:", e.message);
}
assert(caught1 == true, "非法端口应抛出明确错误");

print("--- 测试 2. TcpServer 监听与 accept 超时 ---");
let srv = TcpServer(23088);
assert(srv.is_listening() == true, "服务端应处于监听状态");
let s_info = srv.info();
assert(s_info.port == 23088, "端口应为 23088");
assert(s_info.listening == true, "info listening 应为 true");

let client_none = srv.accept(100);
assert(client_none == nil, "超时无客户端时应返回 nil");

print("--- 测试 3. TcpServer 环回通信 (服务端 accept 与双向数据交互) ---");
let client = Tcp("127.0.0.1", 23088, 1000);
let peer = srv.accept(1000);
assert(peer != nil, "服务端应成功接入客户端");
assert(peer.is_connected() == true, "对端连接应已就绪");

client.send("HelloServer");
let received_req = peer.recv(1024, 1000);
assert(received_req == "HelloServer", "服务端应完整接收请求数据");

peer.send("HelloClientOK");
let received_resp = client.recv(1024, 1000);
assert(received_resp == "HelloClientOK", "客户端应完整接收响应数据");

client.close();
peer.close();
srv.close();
assert(srv.is_listening() == false, "close 后服务端监听应关闭");

print("--- 测试 4. BLE 初始化与广播控制 ---");
let b_st0 = BLE.status();
assert(b_st0.connected == false, "初始状态未连接");

BLE.init("L-Test-Device");
let b_st1 = BLE.status();
assert(b_st1.initialized == true, "BLE 已完成初始化");
assert(b_st1.device_name == "L-Test-Device", "设备名称应匹配");

BLE.adv_start();
let b_st2 = BLE.status();
assert(b_st2.advertising == true, "应处于广播中");

BLE.adv_stop();
let b_st3 = BLE.status();
assert(b_st3.advertising == false, "广播应已停止");

print("--- 测试 5. BLE 连接、发送与接收仿真 ---");
BLE.sim_connect(true);
assert(BLE.is_connected() == true, "BLE 客户端应已连接");

BLE.send("ble_tx_packet");

BLE.feed_rx_sim("app_command_led_on");
let rx_data = BLE.recv(1024, 100);
assert(rx_data == "app_command_led_on", "BLE 应收到注入的模拟数据");

BLE.feed_rx_sim("hex_packet");
let rx_bytes = BLE.recv_bytes(1024, 100);
assert(type(rx_bytes) == "bytes", "recv_bytes 应返回 bytes 类型");
assert(rx_bytes.to_str() == "hex_packet", "bytes 内容应一致");

BLE.deinit();
let b_st_end = BLE.status();
assert(b_st_end.initialized == false, "deinit 后应为未初始化状态");

print("22_tcp_server_and_ble 全部通过!");
