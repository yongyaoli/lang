// ============================================
// 测试 24: MQTT 3.1.1 物联网客户端 (MqttClient 与 mqtt 模块)
// ============================================

fn assert(cond, msg) {
    if not cond {
        print("断言失败:", msg);
        exit(1);
    }
}

print("--- 测试 1. 模块导入与客户端构造 ---");
import "mqtt";
print("mqtt version:", mqtt.version);
assert(mqtt.version == "3.1.1", "MQTT 规范版本应为 3.1.1");

let c1 = mqtt.Client("dev_sensor_01");
print("c1 初始状态:", c1.is_connected());
assert(c1.is_connected() == false, "初始状态应为未连接");

// 全局构造函数 MqttClient
let c2 = MqttClient("dev_gateway_02");
assert(c2.is_connected() == false, "全局 MqttClient 初始状态应为未连接");

print("--- 测试 2. 鉴权与心跳保活配置 ---");
c1.set_auth("sensor_01&product_key", "sign_token_123456");
c1.set_keepalive(30);
c1.set_will("/sys/device/status", "{\"online\":false}", 1, false);
print("参数配置完成");

print("--- 测试 3. 未连接时的防御拦截 ---");
let caught_pub = false;
try {
    c1.publish("/test/topic", "hello");
} catch (e) {
    caught_pub = true;
    print("捕获未连接发布拦截:", e.message);
}
assert(caught_pub == true, "未连接发布应抛出错误");

let caught_sub = false;
try {
    c1.subscribe("/test/topic");
} catch (e) {
    caught_sub = true;
    print("捕获未连接订阅拦截:", e.message);
}
assert(caught_sub == true, "未连接订阅应抛出错误");

print("--- 测试 4. PC 离线模拟连接与状态流转 ---");
c1.sim_connect(true);
assert(c1.is_connected() == true, "sim_connect 后应处于连接状态");
print("模拟连接成功, is_connected:", c1.is_connected());

print("--- 测试 5. 消息订阅与下行回调处理 ---");
let received_topic = "";
let received_payload = "";
let call_count = 0;

c1.on_message(fn(topic, payload) {
    call_count = call_count + 1;
    received_topic = topic;
    received_payload = payload;
    print("回调接收消息:", topic, "内容:", payload);
});

// 模拟云端下发控制指令
c1.sim_incoming("/sys/dev_sensor_01/thing/service/property/set", "{\"params\":{\"LightSwitch\":1}}");
assert(call_count == 1, "回调应被触发 1 次");
assert(received_topic == "/sys/dev_sensor_01/thing/service/property/set", "主题应匹配");
assert(received_payload == "{\"params\":{\"LightSwitch\":1}}", "载荷应匹配");

// 模拟多包连续下发
c1.sim_incoming("/downlink/cmd", "REBOOT");
assert(call_count == 2, "回调应累计触发 2 次");
assert(received_topic == "/downlink/cmd", "主题应更新");
assert(received_payload == "REBOOT", "载荷应更新");

print("--- 测试 6. 注销回调 ---");
c1.on_message(nil);
c1.sim_incoming("/downlink/cmd", "IGNORED");
assert(call_count == 2, "注销后回调不应再触发");
print("注销回调验证成功");

print("--- 测试 7. 发布消息与断开连接 ---");
let pub_res = c1.publish("/sys/dev_sensor_01/thing/event/property/post", "{\"id\":\"123\",\"params\":{\"temp\":26.5}}", 1);
assert(pub_res == true, "发布应返回 true");

c1.disconnect();
assert(c1.is_connected() == false, "disconnect 后应断开连接");
print("断开连接成功, is_connected:", c1.is_connected());

print("=== MQTT 测试结束 ===");
