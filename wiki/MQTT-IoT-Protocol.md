# MQTT 物联网协议与华为云 IoT 实战

L 语言内置纯 C 原生实现的 MQTT 客户端引擎，支持标准 **MQTT v3.1.1 协议**，支持 QoS 0/1/2 消息收发、心跳保持与订阅回调，完美对接主流物联网云平台（阿里云 IoT、腾讯云 IoT、华为云 IoT、EMQX 等）。

---

## 1. 核心 API 快速入门

```l
import "mqtt";

// 1. 创建 MQTT 客户端实例
let client = MQTT("broker.emqx.io", 1883, "ESP32_Device_001");

// 2. 连接服务器 (可选用户名与密码)
client.connect("user", "password");

// 3. 订阅主题并注册回调函数
client.subscribe("sensor/temperature", fn(topic, payload) {
    print("收到消息推送! 主题:", topic, "内容:", payload);
});

// 4. 发布遥测数据 (主题, 正文, QoS=0/1/2)
client.publish("sensor/temperature", "26.8℃", 0);

// 5. 在主循环中保活
while true {
    client.loop(100); // 处理网络消息包与心跳 Ping
    sleep(0.1);
}
```

---

## 2. 华为云 IoTDA 工业物联网实战

华为云 IoTDA 等主流平台对客户端 ID、用户名与密码签名算法有严格规范。详细代码可直接参考项目自带的完整用例：[`examples/mqtt_huaweicloud_iot.l`](file:///d:/github/langgit/examples/mqtt_huaweicloud_iot.l)。

### 接入流程与鉴权生成：
1. **设备接入地址**：在华为云控制台获取（如 `xxxx.iot-mqtts.cn-north-4.myhuaweicloud.com`，端口 1883 或 8883）；
2. **Client ID**：格式为 `<设备ID>_0_0_<时间戳>`；
3. **用户名**：为 `<设备ID>`；
4. **密码**：使用 HMAC-SHA256 算法，以设备密钥对时间戳计算哈希。

### 上报属性格式 (JSON)：
```l
import "json";

let payload = {
    "services": [
        {
            "service_id": "Environmental",
            "properties": {
                "temperature": 27.4,
                "humidity": 68.2
            }
        }
    ]
};

let json_str = JSON.stringify(payload);
let pub_topic = "$oc/devices/" + device_id + "/sys/properties/report";
client.publish(pub_topic, json_str, 0);
print("遥测数据已成功上报至华为云物模型！");
```
