// ==============================================================================
// 示例程序: 物联网传感器数据采集与 JSON 序列化上报
// 适用硬件: ESP32 全系列 (支持外接 I2C 温湿度传感器 AHT20 或内部模拟)
// 说明: 演示如何将传感器物理量读数（温度、湿度、电压、设备状态）结构化组织，
//      利用高效流式 JSON 库进行紧凑序列化（上报云端/MQTT）与格式化美化。
// ==============================================================================

print("========================================");
print("     L 语言 物联网传感器 JSON 数据上报");
print("========================================");

// 1. 获取设备基础信息
let chip_model = ESP32.chip();
let dev_mac    = wifi_mac();
let heap_free  = ESP32.free_heap();

print("1. 设备就绪:", chip_model, "(MAC:", dev_mac + ")");

// 2. 模拟采集多通道环境传感器数据 (带小幅动态波动)
let base_temp = 25.4;
let base_humi = 58.2;
let base_volt = 3.28;

print("\n2. 开始采集环境传感器数据 (模拟 3 个数据周期)...");

for (let cycle = 1; cycle <= 3; cycle = cycle + 1) {
    // 模拟小幅数值波动
    let cur_temp = base_temp + (cycle * 0.3);
    let cur_humi = base_humi - (cycle * 0.5);
    let cur_volt = base_volt;

    // 3. 构建符合工业物联网标准的遥测数据字典 (Telemetry Map)
    let telemetry = {
        version: "1.0",
        timestamp: time(),
        device: {
            id: "esp32_" + dev_mac.replace(":", "").substr(6, 6),
            model: chip_model,
            heap_free: heap_free
        },
        sensors: {
            temperature: cur_temp,
            humidity: cur_humi,
            battery_voltage: cur_volt,
            sensor_type: "AHT20+ADC"
        },
        alarm: {
            overheat: (cur_temp > 30.0),
            low_battery: (cur_volt < 3.0)
        }
    };

    // 4. 紧凑序列化 (专为 MQTT / HTTP 上报优化，零多余空白，最省流量与内存)
    let compact_json = JSON.stringify(telemetry);
    print("\n--- [采集周期 #" + str(cycle) + "] 紧凑 JSON 报文 (" + str(len(compact_json)) + " 字节) ---");
    print(compact_json);

    // 5. 反向解析验证 (验证数据可逆完整性)
    let parsed = JSON.parse(compact_json);
    print("   -> 校验解包: 设备ID=" + parsed.device.id + 
          ", 温度=" + str(parsed.sensors.temperature) + "℃" +
          ", 超温告警=" + str(parsed.alarm.overheat));

    ESP32.delay_ms(500);
}

// 6. 演示 2 空格美化格式输出 (供开发者本地调试查看结构)
print("\n3. 美化格式化展示 (JSON.stringify(val, true)):");
let sample = {
    service: "MQTT_TELEMETRY",
    qos: 1,
    topic: "devices/esp32/telemetry",
    payload: {
        temp: 26.3,
        humi: 57.0
    }
};

print(JSON.stringify(sample, true));

print("\n========================================");
print("物联网传感器 JSON 序列化示例演示完毕。");
