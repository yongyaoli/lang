// ==============================================================================
// 测试 23: 流式 JSON 编解码器全面验证 (RFC 8259 规范、美化缩进、深度防御与异常捕获)
// ==============================================================================

// 1. 基本字面量编解码
print("--- 1. 基本字面量 ---");
print("null:", JSON.parse("null"));
print("bool:", JSON.parse("true"), JSON.parse("false"));
print("int:", JSON.parse("0"), JSON.parse("-42"), JSON.parse("2147483647"));
print("float:", JSON.parse("3.14"), JSON.parse("-0.05"), JSON.parse("1e2"), JSON.parse("2.5e-1"));
print("string:", JSON.parse("\"hello\\nworld\\t\\\"quoted\\\"\""));
print("unicode:", JSON.parse("\"\\u4e2d\\u6587\\u0041\""));

// 2. 数组与对象嵌套
print("--- 2. 数组与对象 ---");
let empty_arr = JSON.parse("[]");
let empty_map = JSON.parse("{}");
print("empty arr len:", len(empty_arr));
print("empty map len:", len(empty_map));

let nested_json = "{\"device\":\"esp32_c6\",\"status\":{\"online\":true,\"rssi\":-68},\"pins\":[2,4,12]}";
let root = JSON.parse(nested_json);
print("device:", root.device);
print("online:", root.status.online);
print("rssi:", root.status.rssi);
print("pins:", root.pins[0], root.pins[1], root.pins[2]);

// 3. 序列化 (紧凑模式与美化模式)
print("--- 3. 序列化 ---");
let sensor_payload = {
    sensor: "AHT20",
    temp: 25.5,
    hum: 60,
    valid: true,
    note: nil,
    readings: [25.4, 25.5, 25.6]
};

let compact_str = JSON.stringify(sensor_payload);
print("compact length > 0:", len(compact_str) > 0);
// 反向验证序列化串能够被正确 parse 回相同的值
let roundtrip = JSON.parse(compact_str);
print("roundtrip check:", roundtrip.sensor, roundtrip.temp, roundtrip.readings[1]);

let pretty_str = JSON.stringify({ a: 1, b: [10, 20] }, true);
print("pretty lines count:", len(pretty_str.split("\n")));

// 4. bytes 缓冲区序列化
print("--- 4. bytes 缓冲区 ---");
let b = bytes([0xAA, 0x55, 0x01]);
print("bytes to json:", JSON.stringify(b));

// 5. 便捷别名与 import 模块导入
print("--- 5. 别名与模块 ---");
print("json lowercase:", json.stringify([1, 2]));
print("json_parse function:", json_parse("[42]")[0]);
print("json_stringify function:", json_stringify(true));

import "json" as jmod;
let from_mod = jmod.parse("{\"mod\": true}");
print("from import mod:", from_mod.mod);

// 6. 异常边界与防御性测试
print("--- 6. 异常捕获 ---");
fn test_parse_err(title, bad_str) {
    try {
        JSON.parse(bad_str);
        print("FAIL:", title);
    } catch (e) {
        print("PASS:", title, "->", e.message.split("] ")[1]);
    }
}

test_parse_err("空文本", "");
test_parse_err("数组尾随逗号", "[1, 2,]");
test_parse_err("对象尾随逗号", "{\"k\": 1,}");
test_parse_err("键名无引号", "{ key: 123 }");
test_parse_err("多余字符", "{\"a\": 1} extra");
test_parse_err("未闭合字符串", "\"abc");

// 7. 最大嵌套深度防御 (防止栈溢出)
print("--- 7. 嵌套深度防御 ---");
let deep_str = "[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]"; // 17 层
test_parse_err("深层数组", deep_str);

print("全部 JSON 测试完成!");
