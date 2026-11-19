// ==============================================================================
// 示例程序: 阿里云物联网平台 (Aliyun IoT) 物模型属性上报与命令下发
// 适用硬件: ESP32 全系列 (真实 WiFi) / PC 宿主环境 (自动演练仿真)
// 规范标准: MQTT 3.1.1 + Alink 物模型协议
// ==============================================================================

import "mqtt";
import "json";
import "esp32" as hw;

print("==================================================");
print("     L 语言 阿里云物联网平台 (Aliyun IoT) 客户端");
print("==================================================");

// 1. 阿里云设备三元组配置 (开发者替换为您在阿里云控制台创建的真实三元组)
let PRODUCT_KEY   = "a1B2c3D4e5F";
let DEVICE_NAME   = "SmartMeter_01";
let DEVICE_SECRET = "8f3e2b1a0d9c8b7a"; // 生产环境中可通过鉴权算法生成 Token

// 阿里云 MQTT 接入端点与三元组拼装规则
let host = PRODUCT_KEY + ".iot-as-mqtt.cn-shanghai.aliyuncs.com";
let port = 1883;

// ClientID 格式规范: ${clientId}|securemode=3,signmethod=hmacsha1,timestamp=132323232|
let client_id = DEVICE_NAME + "|securemode=3,signmethod=plain,timestamp=20261119|";
let username  = DEVICE_NAME + "&" + PRODUCT_KEY;
let password  = DEVICE_SECRET; // 简化展示，亦可使用 HMAC-SHA1 签名

print("1. 正在初始化 MQTT 客户端...");
print("   - 目标平台: 阿里云 IoT (上海节点)");
print("   - 产品 Key :", PRODUCT_KEY);
print("   - 设备名称 :", DEVICE_NAME);
print("   - 接入域名 :", host, "端口:", port);

let client = mqtt.Client(client_id);
client.set_auth(username, password);
client.set_keepalive(60);

// 设置掉线遗嘱消息 (Will)
let will_topic = "/sys/" + PRODUCT_KEY + "/" + DEVICE_NAME + "/thing/status";
client.set_will(will_topic, json.stringify({"status": "offline"}), 1, false);

// 2. 注册下行消息接收与命令处理回调
let prop_set_topic = "/sys/" + PRODUCT_KEY + "/" + DEVICE_NAME + "/thing/service/property/set";
let prop_reply_topic = "/sys/" + PRODUCT_KEY + "/" + DEVICE_NAME + "/thing/service/property/set_reply";

client.on_message(fn(topic, payload) {
    print("\n[云端下发] 收到来自主题的消息:", topic);
    print("   载荷内容:", payload);

    try {
        let msg = json.parse(payload);
        if msg.params != nil {
            print("   -> 收到物模型属性设置指令!");
            if msg.params.LightSwitch != nil {
                let state = msg.params.LightSwitch;
                print("   -> 执行硬件动作: 设置照明开关为", (state == 1 ? "开启(ON)" : "关闭(OFF)"));
            }
            if msg.params.TargetTemp != nil {
                print("   -> 执行硬件动作: 调节目标温度为", msg.params.TargetTemp, "℃");
            }

            // 发送物模型属性设置回复 (set_reply)
            let reply = {
                "id": msg.id,
                "code": 200,
                "data": {},
                "message": "success"
            };
            let reply_str = json.stringify(reply);
            print("   -> 上报命令响应 (set_reply):", reply_str);
            client.publish(prop_reply_topic, reply_str, 0);
        }
    } catch (e) {
        print("   -> 解析下发 JSON 载荷异常:", e.message);
    }
});

// 3. 建立连接 (PC 环境若无法连外网则自动切入仿真演示)
print("\n2. 正在连接阿里云物联网平台...");
let connected = false;
try {
    client.connect(host, port, 3000);
    connected = true;
    print("   [√] 成功连接阿里云 IoT Broker 并在云端上线！");
} catch (e) {
    print("   [!] 外部网络直连跳过 (提示: " + e.message + ")");
    print("   [*] 自动启动本地高保真物联网仿真演练模式...");
    client.sim_connect(true);
    connected = true;
}

// 4. 订阅物模型下行属性设置与服务调用主题
print("\n3. 订阅 Alink 物模型下行主题...");
print("   - 订阅:", prop_set_topic);
client.subscribe(prop_set_topic, 1);

// 5. 循环上报传感器温湿度物模型属性 (Alink JSON 格式)
print("\n4. 模拟采集环境传感器并向阿里云上报物模型属性...");
let post_topic = "/sys/" + PRODUCT_KEY + "/" + DEVICE_NAME + "/thing/event/property/post";

for (let i = 1; i <= 3; i = i + 1) {
    // 构造物模型遥测数据
    let telemetry_data = {
        "id": str(1000 + i),
        "version": "1.0",
        "params": {
            "CurrentTemperature": 25.0 + i * 0.5,
            "CurrentHumidity": 60 + i * 2,
            "BatteryVoltage": 3.28,
            "WiFiRSSI": -55
        },
        "method": "thing.event.property.post"
    };

    let payload_str = json.stringify(telemetry_data);
    print("   [第 " + str(i) + " 次上报] 发布到:", post_topic);
    print("     数据载荷: " + payload_str);
    client.publish(post_topic, payload_str, 1);

    // 轮询处理下行消息与心跳
    client.poll(100);
}

// 6. 演示云端下发反向控制指令 (仿真演练)
print("\n5. 演练云端物模型反向控制 (远程控制开关与温度)...");
let mock_cloud_cmd = json.stringify({
    "id": "999888",
    "version": "1.0",
    "params": {
        "LightSwitch": 1,
        "TargetTemp": 24.5
    },
    "method": "thing.service.property.set"
});
client.sim_incoming(prop_set_topic, mock_cloud_cmd);

// 7. 发送心跳与断开连接
print("\n6. 发送 PINGREQ 心跳保活...");
client.ping();

print("7. 演示结束，断开连接并下线。");
client.disconnect();
print("   阿里云客户端已安全下线, is_connected:", client.is_connected());
print("\n==================================================");
print("        阿里云 IoT 示例程序全部演练顺利完成！");
print("==================================================");
