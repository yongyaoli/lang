// 测试 17: M4 HAL 硬件抽象层 + l_config.h 模块配置 + 无文件系统裸 Flash 脚本分区 (l_script)

print("=== 1. HAL 与编译裁剪配置查询 (ESP32.config) ===");
let cfg = ESP32.config();
print("platform:", cfg.platform, "partition:", cfg.flash_partition, "flash_size:", cfg.flash_size);
print("modules hw:", cfg.modules.hw, "string:", cfg.modules.string, "math:", cfg.modules.math);

print("=== 2. IEEE 802.3 CRC32 校验测试 ===");
let c1 = ESP32.crc32("123456789");
let c2 = ESP32.crc32(bytes([0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39]));
print("CRC32('123456789'):", c1, "bytes match:", c1 == c2);

print("=== 3. 无文件系统裸 Flash 分区 (l_script) 擦写/校验/零拷贝执行 ===");
ESP32.flash_erase();
let info0 = ESP32.flash_info();
print("Initial flash status:", info0.status, "valid:", info0.valid, "read:", ESP32.flash_read());

// 烧录一段嵌入式控制脚本到 l_script 裸分区
let app_code = "const BLINK_PIN = 2;\n"
    + "let led = pin(BLINK_PIN).on();\n"
    + "fn calc_pwm(pct) { return (pct * 1023) / 100; }\n"
    + "print(\"Flash script running! LED value =\", led.value(), \"duty(50%) =\", calc_pwm(50));\n"
    + "calc_pwm(100);\n";

let written = ESP32.flash_write(app_code);
let info1 = ESP32.flash_info();
print("Written bytes:", written, "status:", info1.status, "valid:", info1.valid);
print("Header CRC32 matches:", info1.crc32 == ESP32.crc32(app_code));

// 直接从映射的 Flash 分区零拷贝执行脚本
let ret = ESP32.flash_exec();
print("flash_exec return value:", ret);

// 验证 Flash 脚本内抛出的异常也能精确定位到 <flash:l_script> 的行号与列号
ESP32.flash_write("let a = 10;\nlet b = 0;\nlet c = a / b;\n");
try {
    ESP32.flash_exec();
} catch (e) {
    print("Caught flash script error at", e.file, "line:", e.line, "col:", e.col, "msg:", e.message);
    print("Flash error source:", e.source);
}

// 擦除裸分区并验证状态恢复
ESP32.flash_erase();
print("After erase status:", ESP32.flash_info().status);

print("=== 4. REPL 内置 upload('sim', ...) 与 help('esp32') 测试 ===");
let up_res = upload("sim", "pin(2).off(); print(\"Uploaded via upload(sim)!\"); 42;");
print("upload('sim') result:", up_res, "GPIO2 level:", pin(2).value());
help(upload);
help("esp32");
