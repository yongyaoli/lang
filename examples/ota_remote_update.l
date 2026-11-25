// ==============================================================================
// 示例程序: 无线远程 OTA 脚本空中升级 (HTTP Remote OTA)
// 适用硬件: ESP32 全系列 (真实 WiFi) / PC 宿主环境 (自动演练仿真)
// 规范标准: HTTP(S) 客户端 + IEEE 802.3 CRC32 完整性防篡改校验 + 裸 Flash 分区热重载
// ==============================================================================

import "ota";

print("==================================================");
print("     L 语言 远程 HTTP OTA 脚本空中升级系统");
print("==================================================");

// ------------------------------------------------------------------------------
// WiFi 配置: 如果在真实 ESP32 硬件上运行，请在此配置您的路由器账号与密码
// ------------------------------------------------------------------------------
let wifi_ssid = "YOUR_WIFI_SSID";         // 请替换为您的 2.4GHz WiFi 名称
let wifi_pass = "YOUR_WIFI_PASSWORD";     // 请替换为您的 WiFi 密码 (至少 8 位)

if (!wifi_is_connected()) {
    if (wifi_ssid != "YOUR_WIFI_SSID") {
        print("正在连接 WiFi:", wifi_ssid, "...");
        try {
            wifi_connect(wifi_ssid, wifi_pass, 15000);
            print("WiFi 连接成功! 本机 IP 地址:", wifi_ip());
        } catch (e) {
            print("WiFi 连接失败:", e);
            print("提示: 请检查 SSID 和密码，或靠近路由器重试。");
        }
    } else {
        print("提示: 当前 wifi_ssid 为默认占位符，若需真机联网请修改脚本开头的 wifi_ssid/wifi_pass。");
    }
} else {
    print("WiFi 已就绪, 本机 IP 地址:", wifi_ip());
}

// 1. 查询当前设备 Flash 分区状态
let initial_status = OTA.status();
print("1. 当前设备 Flash 状态看板:");
print("   - 目标存储分区 :", initial_status.partition);
print("   - 分区总容量   :", initial_status.partition_size, "字节");
print("   - 现有脚本长度 :", initial_status.script_length, "字节");
print("   - 现有脚本 CRC :", initial_status.script_crc32);
print("   - 分区有效性   :", initial_status.valid);

// 2. 模拟配置远程云端发布的新版本应用脚本
// (实际生产中，该 URL 指向您自己的云端 Web 服务器或物联网 CDN，如 https://my-iot.com/firmware/v2/app.l)
let REMOTE_FIRMWARE_URL = "http://ota.lang-lang.org/release/v2.1.0/sensor_app.l";

// 准备新版本应用源码 (包含温度监控与上报逻辑)
let new_version_code = "// ==========================================\n" +
"// 远程 OTA 更新后的最新应用业务代码 (v2.1.0)\n" +
"// ==========================================\n" +
"print(\"★ [OTA-APP v2.1.0] 欢迎运行无线空中热升级后的新版本应用！\");\n" +
"let temp = 26.8;\n" +
"let hum = 65.2;\n" +
"print(\"★ [传感器遥测] 当前环境温度:\", temp, \"℃, 相对湿度:\", hum, \"%\");\n";

// 计算该源码的预期 CRC32 校验码
let expected_crc = OTA.crc32(new_version_code);

print("\n2. 准备执行空中升级...");
print("   - 远程固件地址 :", REMOTE_FIRMWARE_URL);
print("   - 预期 CRC32   :", "0x" + str(expected_crc));

// 若在离线测试或仿真环境下，注入模拟响应
OTA.sim_set_response(REMOTE_FIRMWARE_URL, new_version_code);

// 3. 执行空中拉取、校验与热执行
print("\n3. 正在通过 WiFi 从远程服务器拉取脚本并校验 CRC32...");
try {
    // 参数说明: OTA.update_script(url, expected_crc, auto_exec)
    // - expected_crc: 若非 0 则校验 CRC，若云端传输损坏将自动拒绝写入
    // - auto_exec: true 表示写入 Flash 成功后立即热重载执行
    let result = OTA.update_script(REMOTE_FIRMWARE_URL, expected_crc, true);

    print("\n4. 升级完成！结果明细:");
    print("   [√] 升级状态   : 成功 (ok =", result.ok, ")");
    print("   [√] 写入字节数 :", result.bytes, "字节");
    print("   [√] 实际 CRC32 :", result.crc32);
    print("   [√] 自动热重载 :", result.auto_exec);
} catch (e) {
    print("\n[!] OTA 升级失败:", e.message);
}

// 4. 查看升级后最新的系统状态
let updated_status = OTA.status();
print("\n5. 升级后 Flash 分区最终状态:");
print("   - 最新代码长度 :", updated_status.script_length, "字节");
print("   - 最新代码 CRC :", updated_status.script_crc32);
print("   - 当前可用堆   :", updated_status.allocated_bytes, "字节已分配");

print("\n==================================================");
print("      远程 OTA 脚本空中升级演示全部顺利完成！");
print("==================================================");
