// ==============================================================================
// 示例程序: BLE 蓝牙串口无线透传与远程控制 (BLE UART)
// 适用硬件: ESP32 全系列 (外接或板载 LED 默认 GPIO 2)
// 说明: 开启蓝牙串口透传服务，手机连接后可发送控制指令:
//      - 发送 "LED ON"  -> 点亮指示灯并回传确认
//      - 发送 "LED OFF" -> 熄灭指示灯并回传确认
//      - 发送 "STATUS"  -> 查询设备运行状态
// ==============================================================================

print("========================================");
print("     L 语言 BLE 蓝牙无线透传与遥控");
print("========================================");

// 1. 初始化板载 LED 引脚 (ESP32 板载 LED 常用 GPIO 2)
let led_pin = 2;
let led = Pin(led_pin, "out");
led.off();
print("1. 板载 LED 引脚初始化完成: GPIO", led_pin, "(默认熄灭)");

// 2. 初始化 BLE 蓝牙透传服务并广播
let ble_name = "L-Smart-Device";
BLE.init(ble_name);
BLE.adv_start(100);

print("2. 蓝牙串口透传服务已启动！设备名称:", ble_name);
print("   请打开手机蓝牙调试 App，连接本设备后进入数据收发通道。");
print("   支持指令: \"LED ON\" / \"LED OFF\" / \"STATUS\"\n");

// 在 PC 模拟模式下预置几条模拟客户端指令，方便离线演示
if (!ESP32.info().is_hardware) {
    print("(当前为 PC 离线模式，自动注入模拟指令演示交互)");
    BLE.sim_connect(true);
    BLE.feed_rx_sim("LED ON\n");
}

// 3. 循环轮询接收客户端数据
print("3. 开始监听蓝牙指令收发 (轮询处理中):");

let running = true;
let loop_count = 0;

while running {
    loop_count = loop_count + 1;
    // 非阻塞/短超时读取蓝牙数据 (最多读取 64 字节，超时 200ms)
    let msg = BLE.recv(64, 200);

    if (msg != nil) {
        let cmd = msg.trim();
        print("\n[收到手机蓝牙指令] -> \"" + cmd + "\"");

        if (cmd == "LED ON") {
            led.on();
            let ack = "OK: LED is turned ON\r\n";
            BLE.send(ack);
            print("   -> 已执行开灯，回传给手机:", ack.trim());
        } else if (cmd == "LED OFF") {
            led.off();
            let ack = "OK: LED is turned OFF\r\n";
            BLE.send(ack);
            print("   -> 已执行关灯，回传给手机:", ack.trim());
        } else if (cmd == "STATUS") {
            let status_msg = "CHIP:" + ESP32.chip() + ", HEAP:" + str(ESP32.free_heap()) + "B\r\n";
            BLE.send(status_msg);
            print("   -> 已回传设备状态看板:", status_msg.trim());
        } else {
            let err_reply = "UNKNOWN CMD: \"" + cmd + "\"\r\n";
            BLE.send(err_reply);
            print("   -> 未知指令，已回复帮助");
        }
    }

    // PC 演示环境下执行几轮后退出；真机环境可设为死循环永久运行
    if (!ESP32.info().is_hardware and loop_count >= 3) {
        running = false;
    }

    ESP32.delay_ms(300);
}

// 4. 清理退出
BLE.adv_stop();
print("\n蓝牙透传示例运行完成。");
