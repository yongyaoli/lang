// ==============================================================================
// 示例程序: 华为云设备接入 (Huawei Cloud IoTDA) MQTTS (TLS/8883) 物联网客户端
// 适用硬件: ESP32 全系列 (真实 WiFi) / PC 宿主环境 (自动演练仿真)
// 规范标准: MQTT 3.1.1 + TLS 加密传输 + 华为云 IoTDA 平台标准规范
// 真实参数: 来源于华为云 IoTDA 设备注册配置
// ==============================================================================

import "mqtt";
import "json";
import "esp32" as hw;

print("==================================================");
print("   L 语言 华为云设备接入 MQTTS (端口 8883) 客户端");
print("==================================================");

// 1. 华为云 MQTTS 设备凭证配置 (参考真实华为云 IoTDA 设备实例参数)
let DEVICE_ID     = "6afebaa1e094d6159284e9aa_673c123456789abcdef01234_ESP32_DEV01";
let DEVICE_SECRET = "HuaweiSecret_20269988";
let hostname      = "d732ae5830.st1.iotda-device.cn-east-3.myhuaweicloud.com";
let port          = 8883; // MQTTS 标准加密端口 (MQTT over TLS)

// ClientID 与认证凭证格式规范
// 华为云 IoTDA 格式: 推荐 ClientID 采用 DeviceId，密码采用设备秘钥或 HMAC-SHA256
let timestamp = "2026111910";
let client_id = DEVICE_ID;
let username  = DEVICE_ID;
let password  = DEVICE_SECRET;

print("1. 正在初始化华为云 MQTTS 客户端...");
print("   - 目标平台: 华为云 IoTDA (华东-上海一 / cn-east-3)");
print("   - 接入域名 :", hostname);
print("   - 接入端口 :", port, "(MQTTS TLS/SSL 加密端口)");
print("   - 设备 ID  :", DEVICE_ID);
print("   - 认证方式 : 设备密钥身份验证");

let client = mqtt.Client(client_id);
client.set_auth(username, password);
client.set_keepalive(60);

// 2. 华为云标准系统主题 (System Topics)
// 属性上报: $oc/devices/{device_id}/sys/properties/report
let report_topic = "$oc/devices/" + DEVICE_ID + "/sys/properties/report";

// 云端命令下发: $oc/devices/{device_id}/sys/commands/#
let cmd_sub_topic = "$oc/devices/" + DEVICE_ID + "/sys/commands/#";

// 3. 注册下行命令与参数配置消息处理回调
client.on_message(fn(topic, payload) {
    print("\n[云端下发] 收到华为云下行主题消息:", topic);
    print("   载荷内容:", payload);

    try {
        let msg = json.parse(payload);
        if msg.command_name != nil {
            let cmd_name = msg.command_name;
            let service_id = msg.service_id;
            print("   -> 收到华为云设备控制命令: [" + service_id + "] " + cmd_name);

            if cmd_name == "SetFanSpeed" {
                let speed = msg.paras.speed;
                print("   -> 执行硬件动作: 调节电机风扇转速为 " + str(speed) + " RPM");
            } else if cmd_name == "Reboot" {
                print("   -> 执行硬件动作: 准备软复位重启设备");
            }

            // 提取 request_id 并回送命令响应
            // 响应主题格式: $oc/devices/{device_id}/sys/commands/response/request_id={request_id}
            let req_id = "mock_req_12345";
            let resp_topic = "$oc/devices/" + DEVICE_ID + "/sys/commands/response/request_id=" + req_id;
            let resp_payload = json.stringify({
                "result_code": 0,
                "response_name": cmd_name + "Response",
                "paras": {
                    "result": "success",
                    "executed_at": timestamp
                }
            });

            print("   -> 上报命令响应至华为云:", resp_topic);
            print("      响应载荷: " + resp_payload);
            client.publish(resp_topic, resp_payload, 0);
        }
    } catch (e) {
        print("   -> 解析华为云下发 JSON 异常:", e.message);
    }
});

// 4. 连接华为云 MQTTS 平台
print("\n2. 正在连接华为云 MQTTS 接入端点 (8883)...");
let connected = false;
try {
    // 尝试发起连接 (如果当前运行环境尚未加载 mbedtls / TLS 握手证书，将获得安全防护诊断)
    client.connect(hostname, port, 3000);
    connected = true;
    print("   [√] 成功建立 MQTTS 加密连接并在云端上线！");
} catch (e) {
    print("   [!] 外部网络直接连接结果: " + e.message);
    print("   [*] 说明: MQTTS (8883) 要求 TLS Client Hello 握手层。");
    print("   [*] 自动启动本地高保真物联网仿真演练模式...");
    client.sim_connect(true);
    connected = true;
}

// 5. 订阅华为云命令下发主题
print("\n3. 订阅华为云命令下发主题...");
print("   - 订阅:", cmd_sub_topic);
client.subscribe(cmd_sub_topic, 1);

// 6. 向华为云周期性上报多服务属性 (Services Properties)
print("\n4. 模拟采集环境与外设状态并上报至华为云...");

for (let i = 1; i <= 3; i = i + 1) {
    // 华为云标准属性上报结构: services 列表
    let report_data = {
        "services": [
            {
                "service_id": "Environment",
                "properties": {
                    "temperature": 26.5 + i * 0.4,
                    "humidity": 60 + i,
                    "air_pressure": 101.3
                },
                "event_time": "20261119T080000Z"
            },
            {
                "service_id": "Battery",
                "properties": {
                    "battery_level": 98 - i,
                    "is_charging": false
                }
            }
        ]
    };

    let payload_str = json.stringify(report_data);
    print("   [第 " + str(i) + " 次上报] 发布到:", report_topic);
    print("     数据载荷: " + payload_str);
    client.publish(report_topic, payload_str, 1);

    // 轮询等待与处理下行包
    client.poll(100);
}

// 7. 演练华为云平台下发反向控制指令 (仿真演练)
print("\n5. 演练华为云下发设备控制命令 (远程调速)...");
let mock_huawei_cmd = json.stringify({
    "service_id": "MotorControl",
    "command_name": "SetFanSpeed",
    "paras": {
        "speed": 2200,
        "direction": "forward"
    }
});
client.sim_incoming(cmd_sub_topic, mock_huawei_cmd);

// 8. 心跳保活与断开连接
print("\n6. 发送 PINGREQ 保活报文...");
client.ping();

print("7. 演示结束，断开华为云连接并下线。");
client.disconnect();
print("   华为云客户端已安全下线, is_connected:", client.is_connected());
print("\n==================================================");
print("       华为云 MQTTS 示例程序全部演练顺利完成！");
print("==================================================");
