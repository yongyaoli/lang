// ==============================================================================
// 示例程序: 嵌入式统一虚拟文件系统 (FS / File 标准库, M13)
// 适用硬件: ESP32 全系列 (LittleFS 掉电安全分区) / PC 宿主环境 (透明映射本地磁盘)
// 功能演示:
// 1. 全局静态 API: 配置文件与参数快速写入、读取与存在性检查 (FS.write / FS.read)
// 2. 多级目录管理与文件清单扫描 (FS.mkdir / FS.list / FS.isdir)
// 3. 大文件流式句柄: 传感器长周期运行日志写入与逐行流式解析 (File.open / readline)
// 4. 存储看板信息看板查询 (FS.info)
// 5. 跨模块多文件动态 import 执行 (/flash/*.l)
// ==============================================================================

import "fs";
import "json";

print("==================================================");
print("     L 语言 统一虚拟文件系统 (FS / File) 演示");
print("==================================================");

// ------------------------------------------------------------------------------
// 1. 系统存储空间看板
// ------------------------------------------------------------------------------
print("1. 查询文件系统当前存储容量看板...");
let info = FS.info();
print("   - 文件系统后端类型:", info.fs_type);
print("   - 总存储空间容量  :", (info.total_bytes / 1024), "KB");
print("   - 可用剩余空间容量:", (info.free_bytes / 1024), "KB");

// ------------------------------------------------------------------------------
// 2. 设备配置文件快速读写 (配合流式 JSON 编解码器)
// ------------------------------------------------------------------------------
print("\n2. 创建并持久化设备配置 JSON 文件...");
let config_path = "/flash/etc/device_config.json";

let initial_config = {
    "device_name": "ESP32-Gateway-Node01",
    "wifi_ssid": "SmartHome-2.4G",
    "mqtt_broker": "192.168.1.100",
    "mqtt_port": 1883,
    "sample_rate_sec": 5,
    "features": {
        "ota_enabled": true,
        "deep_sleep_enabled": false
    }
};

let json_str = json.stringify(initial_config, true);
FS.write(config_path, json_str);
print("   [√] 成功写入配置文件:", config_path, "(大小: " + str(FS.size(config_path)) + " 字节)");

// 重新读取并解析
let read_back = FS.read(config_path);
let parsed_config = json.parse(read_back);
print("   [√] 成功读取并解析配置: 设备名 =", parsed_config.device_name, "采样率 =", parsed_config.sample_rate_sec);

// ------------------------------------------------------------------------------
// 3. 大文件流式句柄 (File.open): 连续记录传感器长日志
// ------------------------------------------------------------------------------
print("\n3. 使用 File 流式句柄记录环境传感器时序日志...");
let log_path = "/flash/logs/sensors_20261119.csv";

// 覆写打开 (清空或新建)
let log_file = File.open(log_path, "w");
log_file.write("timestamp,temp_c,humi_pct,pressure_hpa\n");

// 模拟 5 次传感器采集写入
for (let i = 1; i <= 5; i = i + 1) {
    let row = "2026-11-19T10:0" + str(i) + ":00," + str(25.0 + i * 0.4) + "," + str(55 + i) + ",101.32\n";
    log_file.write(row);
}
log_file.flush(); // 确保落盘同步
log_file.close();
print("   [√] 传感器 CSV 日志写入完成, 当前文件指针/大小:", FS.size(log_path), "字节");

// ------------------------------------------------------------------------------
// 4. 逐行流式读取与统计 (低 RAM 占用，单句柄 <512B)
// ------------------------------------------------------------------------------
print("\n4. 逐行流式读取 CSV 文件并分析数据...");
let reader = File.open(log_path, "r");
let header = reader.readline();
print("   -> 表头:", header.trim());

let line_no = 0;
while true {
    let line = reader.readline();
    if line == nil { break; } // 到达 EOF
    line_no = line_no + 1;
    print("   [行 " + str(line_no) + "] " + line.trim());
}
reader.close();
print("   [√] 逐行流式读取统计完毕，共解析数据行:", line_no);

// ------------------------------------------------------------------------------
// 5. 目录管理与文件清单扫描 (FS.list)
// ------------------------------------------------------------------------------
print("\n5. 递归扫描与列出 /flash/etc 目录文件列表...");
let etc_files = FS.list("/flash/etc");
for (let i = 0; i < len(etc_files); i = i + 1) {
    print("   - 条目 [" + str(i) + "]:", etc_files[i]);
}

// ------------------------------------------------------------------------------
// 6. 跨模块多文件动态 import 验证
// ------------------------------------------------------------------------------
print("\n6. 验证基于统一虚拟文件系统的动态模块 import...");
let dynamic_helper_path = "/flash/lib/math_helper.l";
FS.write(dynamic_helper_path, "fn square(x) { return x * x; }\nlet version = \"1.0.0\";\n");

import "/flash/lib/math_helper.l" as helper;
print("   -> 成功加载虚拟文件系统模块 /flash/lib/math_helper.l (版本: " + helper.version + ")");
print("   -> 执行动态模块函数 helper.square(12) =", helper.square(12));

print("\n==================================================");
print("     统一虚拟文件系统 (FS / File) 演示圆满完成！");
print("==================================================");
