// ==============================================================================
// 示例程序: BLE 低功耗蓝牙广播信标 (iBeacon / 通用外设广播)
// 适用硬件: ESP32 / ESP32-S3 / ESP32-C3 / ESP32-C6 (支持 BLE 4.2 / 5.0)
// 说明: 初始化低功耗蓝牙协议栈，设置广播设备名称并开启无线广播。
//      可在手机上使用「nRF Connect」或「BLE 调试宝」App 扫描发现该设备。
// ==============================================================================

print("========================================");
print("     L 语言 BLE 蓝牙广播信标");
print("========================================");

// 1. 初始化 BLE 协议栈，设置设备对外可见的蓝牙名称
let device_name = "L-ESP32-Beacon";
print("1. 正在初始化 BLE 蓝牙控制器，广播名称:", device_name, "...");

let init_ok = BLE.init(device_name);
if (!init_ok) {
    print("BLE 初始化失败！硬件可能不支持或已被占用。");
} else {
    print("BLE 初始化成功！");
}

// 2. 查看当前蓝牙协议栈状态看板
let st = BLE.status();
print("\n2. 当前蓝牙状态信息:");
print("   - 初始化状态:", st.initialized ? "已就绪" : "未就绪");
print("   - 广播状态  :", st.advertising ? "广播中" : "已停止");
print("   - 客户端连接:", st.connected ? "已连接" : "无连接");

// 3. 开启蓝牙广播 (广播间隔推荐 100ms，兼顾功耗与发现速度)
let adv_interval_ms = 100;
print("\n3. 启动 BLE 广播 (广播间隔 " + str(adv_interval_ms) + " ms)...");
BLE.adv_start(adv_interval_ms);

print("   广播已开启！");
print("   提示: 请打开手机蓝牙与 BLE 调试 App (如 nRF Connect)，扫描名为 \"" + device_name + "\" 的设备。");

// 4. 广播心跳循环与连接监听 (持续广播 5 秒演示)
print("\n4. 正在持续广播信号中 (等待客户端发现与连接)...");

for (let i = 0; i < 5; i = i + 1) {
    ESP32.delay_ms(1000);
    let conn = BLE.is_connected();
    print("   [心跳 " + str(i + 1) + "s] 广播活跃中, 连接状态:", conn ? "【已连接】" : "等待连接...");
    if (conn) {
        print("   -> 检测到外部客户端已成功连接！");
        break;
    }
}

// 5. 停止广播
print("\n5. 停止 BLE 广播...");
BLE.adv_stop();
print("广播已停止，当前状态:", BLE.status().advertising ? "广播中" : "已停止");

print("========================================");
print("BLE 广播信标演示完毕。");
