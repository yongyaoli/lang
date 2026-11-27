// =========================================================================
// 测试 27: 栈式虚拟机 VM (--vm) 与预编译字节码 (.lc) 深度压测与一致性回归套件
// 覆盖三大核心能力: MQTT 物联网协议栈、VFS 虚拟文件系统、OTA 无线空中升级
// 保证 AST 解释器模式、--vm 实时 VM 模式、.lc 预编译模式表现 100% 一致
// =========================================================================

fn assert(cond, msg) {
    if not cond {
        print("[断言失败]", msg);
        exit(1);
    }
}

print("==================================================");
print("  L 语言 VM 栈式虚拟机与核心库深度压测套件 (M16)");
print("==================================================");

// =========================================================================
// 第一部分: MQTT 3.1.1 物联网协议栈深度压测
// =========================================================================
print("\n>>> [Part 1/3] MQTT 协议栈与栈式 VM 深度一致性压测 <<<");

import "mqtt";
assert(mqtt.version == "3.1.1", "MQTT 规范版本应为 3.1.1");

// 1.1 多客户端并发构造与参数配置
let clients = [];
for let cid = 0; cid < 5; cid = cid + 1 {
    let client = mqtt.Client("stress_dev_" + str(cid));
    client.set_auth("user_" + str(cid), "token_secret_" + str(cid));
    client.set_keepalive(15 + cid * 5);
    client.set_will("/sys/devices/will/" + str(cid), "{\"online\":false}", 1, false);
    clients.push(client);
}
assert(len(clients) == 5, "应成功创建 5 个独立 MQTT 客户端");

// 1.2 异常防御拦截与异常对象完整性
let caught_unconnected = false;
try {
    clients[0].publish("/test/unconnected", "hello");
} catch (e) {
    caught_unconnected = true;
    assert(e.type == "RuntimeError", "异常类型应为 RuntimeError");
    assert(len(e.message) > 0, "异常消息不能为空");
}
assert(caught_unconnected == true, "未连接发布必须抛出异常");

// 1.3 批量模拟连接建立
for let cid = 0; cid < 5; cid = cid + 1 {
    clients[cid].sim_connect(true);
    assert(clients[cid].is_connected() == true, "模拟连接后客户端应处于在线状态");
}
print("5 个 MQTT 客户端模拟建立连接就绪");

// 1.4 高频下行消息分发与闭包 Upvalue 状态累加 (100 次循环分发)
let msg_counter = 0;
let last_received_topic = "";
let last_received_payload = "";
let topic_stats = {};

clients[0].on_message(fn(topic, payload) {
    // 深度闭包读写 Upvalue
    msg_counter = msg_counter + 1;
    last_received_topic = topic;
    last_received_payload = payload;

    if topic_stats[topic] == nil {
        topic_stats[topic] = 1;
    } else {
        topic_stats[topic] = topic_stats[topic] + 1;
    }
});

let total_packets = 100;
for let p = 0; p < total_packets; p = p + 1 {
    let target_topic = (p % 2 == 0) ? "/sensor/telemetry" : "/sensor/events";
    let json_payload = "{\"seq\":" + str(p) + ",\"temp\":" + str(20 + (p % 15)) + ",\"status\":\"OK\"}";
    clients[0].sim_incoming(target_topic, json_payload);
}

assert(msg_counter == total_packets, "回调触发次数应严格等于 100 次");
assert(topic_stats["/sensor/telemetry"] == 50, "/sensor/telemetry 计数应为 50");
assert(topic_stats["/sensor/events"] == 50, "/sensor/events 计数应为 50");
assert(len(last_received_payload) > 0, "最后一条消息载荷应有效");
print("高频 100 包下行消息分发与闭包 Upvalue 累加校验完全通过");

// 1.5 注销回调与断开连接
clients[0].on_message(nil);
clients[0].sim_incoming("/sensor/telemetry", "{\"ignored\":true}");
assert(msg_counter == total_packets, "注销回调后计数值不应增加");

for let cid = 0; cid < 5; cid = cid + 1 {
    clients[cid].disconnect();
    assert(clients[cid].is_connected() == false, "断开连接后应处于离线状态");
}
print("MQTT 深度一致性压测完成: 全部通过！");


// =========================================================================
// 第二部分: VFS 虚拟文件系统与流式 IO 深度压测
// =========================================================================
print("\n>>> [Part 2/3] VFS 虚拟文件系统与流式 IO 深度压测 <<<");

import "fs";
assert(fs.FS != nil, "VFS 模块应当已加载");

// 2.1 递归目录深度构建
let base_dir = "/flash/stress_test";
let deep_dir = "/flash/stress_test/level1/level2/level3";
FS.mkdir(deep_dir);
assert(FS.exists(deep_dir) == true, "深层嵌套目录应创建成功");
assert(FS.isdir(deep_dir) == true, "FS.isdir 应当准确识别深层目录");

// 2.2 批量循环写入与追加 (50 次循环读写压测)
let data_file = deep_dir + "/bulk_records.csv";
if FS.exists(data_file) {
    FS.remove(data_file);
}

let header = "id,timestamp,voltage,current\n";
FS.write(data_file, header);

let rows_count = 50;
for let r = 0; r < rows_count; r = r + 1 {
    let row_str = str(r) + ",1732694400," + str(3.3 + (r % 5) * 0.1) + "," + str(100 + r) + "\n";
    FS.append(data_file, row_str);
}
assert(FS.exists(data_file) == true, "文件写入后应当存在");
let f_size = FS.size(data_file);
assert(f_size > len(header), "文件大小应当显著大于表头长度");
print("批量 50 次循环追加写入完成, 文件大小:", f_size, "字节");

// 2.3 流式 File 句柄深度操作 (逐行读取、定位、EOF 探测)
let reader = File.open(data_file, "r");
let read_lines = [];
while true {
    let line = reader.readline();
    if line == nil { break; }
    read_lines.push(line);
}
assert(len(read_lines) == rows_count + 1, "流式读取行数应为 51 行 (表头 + 50 数据行)");
assert(read_lines[0] == header, "首行表头内容严格匹配");
assert(reader.eof() == true, "读取到底后 eof() 必须返回 true");

// 测试 seek 与 tell 重新定位
reader.seek(0, 0); // 回到文件开头
assert(reader.tell() == 0, "seek(0, 0) 后文件指针应为 0");
let re_read_header = reader.readline();
assert(re_read_header == header, "回到开头后应能再次读取表头");
reader.close();
print("流式大文件句柄 seek / tell / readline / eof 校验通过");

// 2.4 文件全生命周期: 复制、重命名与删除
let copy_path = base_dir + "/backup_bulk.csv";
assert(FS.copy(data_file, copy_path) == true, "文件复制应返回 true");
assert(FS.exists(copy_path) == true, "副本文件应存在");
assert(FS.size(copy_path) == f_size, "副本文件大小应与原文件严格一致");

let rename_path = base_dir + "/renamed_bulk.csv";
assert(FS.rename(copy_path, rename_path) == true, "文件重命名应返回 true");
assert(FS.exists(copy_path) == false, "原副本路径应已不存在");
assert(FS.exists(rename_path) == true, "重命名后新路径应存在");
assert(FS.remove(rename_path) == true, "删除重命名文件应返回 true");
assert(FS.exists(rename_path) == false, "删除后文件应不存在");

// 2.5 动态模块 VFS 写入与 import 加载执行 (代码热生成导入)
let dynamic_module_code = "fn calc_fib(n) { if n < 2 { return n; } return calc_fib(n - 1) + calc_fib(n - 2); } fn get_tag() { return \"VFS_DYNAMIC_OK\"; }";
let module_path = "/flash/dynamic_worker.l";
FS.write(module_path, dynamic_module_code);
assert(FS.exists(module_path) == true, "动态模块文件应成功写入 VFS");

import "/flash/dynamic_worker.l" as worker;
assert(worker.get_tag() == "VFS_DYNAMIC_OK", "动态导入模块函数应当执行正常");
assert(worker.calc_fib(10) == 55, "动态模块计算 fib(10) 应当精确等于 55");
print("动态模块生成并跨 VFS import 导入执行通过: worker.calc_fib(10) = 55");

// 2.6 异常拦截测试
let caught_fs_err = false;
try {
    FS.read("/flash/non_existent_stress_file.txt");
} catch (e) {
    caught_fs_err = true;
    assert(e.type == "RuntimeError", "文件不存在时异常类型应为 RuntimeError");
}
assert(caught_fs_err == true, "读取不存在文件必须被 try...catch 捕获");
print("VFS 虚拟文件系统深度压测完成: 全部通过！");


// =========================================================================
// 第三部分: OTA 无线空中升级与热重载深度压测
// =========================================================================
print("\n>>> [Part 3/3] OTA 无线空中升级与热重载深度压测 <<<");

import "ota";
assert(OTA != nil, "OTA 模块应当已就绪");

// 3.1 高频批量 CRC32 算法一致性压测 (100 次循环计算)
let sample_str = "OTA_FIRMWARE_PACKET_2026_CRC32_CHECK_";
let prev_crc = 0;
for let c = 0; c < 100; c = c + 1 {
    let cur_crc = OTA.crc32(sample_str + str(c % 10));
    assert(cur_crc != 0, "计算出的 CRC32 应当非 0");
    if c > 0 and (c % 10 == 0) {
        assert(cur_crc == prev_crc, "相同字符串计算出的 CRC32 必须严格一致");
    }
    if c % 10 == 0 {
        prev_crc = cur_crc;
    }
}
print("100 次批量 CRC32 算法重复计算与幂等性验证通过");

// 3.2 擦除与格式化
assert(OTA.erase() == true, "OTA 分区擦除应返回 true");
let status_erased = OTA.status();
assert(status_erased.partition == "l_script", "目标分区应为 l_script");
assert(status_erased.status == "empty", "擦除后状态应为 empty");
assert(status_erased.script_length == 0, "擦除后脚本长度应为 0");

// 3.3 字符串更新与 Flash 写入 (auto_exec=false)
let v1_code = "print(\"[OTA-V1] 服务运行中\"); let ota_val_v1 = 12345;";
let v1_crc = OTA.crc32(v1_code);
let update_res = OTA.update_from_string(v1_code, v1_crc, false);
assert(update_res.ok == true, "update_from_string 应返回 ok=true");
assert(update_res.bytes == len(v1_code), "写入字节数应匹配源码长度");
assert(update_res.crc32_int == v1_crc, "记录的 CRC32 应完全匹配");

let status_v1 = OTA.status();
assert(status_v1.valid == true, "写入后分区状态必须 valid");
assert(status_v1.script_length == len(v1_code), "分区记录的脚本长度应一致");

// 3.4 CRC32 篡改与错误拦截 (防注入/防丢包)
let caught_crc_tamper = false;
try {
    // 传入错误 CRC 0xDEADBEEF
    OTA.update_from_string(v1_code, 0x3735928559, false);
} catch (e) {
    caught_crc_tamper = true;
    assert(e.type == "RuntimeError", "CRC 校验失败时应当抛出 RuntimeError");
}
assert(caught_crc_tamper == true, "CRC 错误时必须被严格拦截");

// 3.5 模拟远程 HTTP URL 固件升级流程
let remote_url = "https://ota.lang-iot.net/v2/app_payload.l";
let v2_remote_code = "print(\"[OTA-V2-REMOTE] 远程固件更新热重载成功\"); let ota_val_v2 = 99999;";
let v2_crc = OTA.crc32(v2_remote_code);
OTA.sim_set_response(remote_url, v2_remote_code);

let remote_res = OTA.update_script(remote_url, v2_crc, false);
assert(remote_res.ok == true, "远程 OTA 升级结果 ok 应为 true");
assert(remote_res.bytes == len(v2_remote_code), "远程 OTA 升级字节数应完全匹配");

let status_v2 = OTA.status();
assert(status_v2.script_length == len(v2_remote_code), "升级后脚本长度应更新");
print("远程 OTA 脚本拉取、校验与分区分层写入正常");

// 3.6 内存热重载执行校验 (auto_exec=true)
let hot_reload_code = "let __hot_reloaded_flag__ = 8888;";
let hot_crc = OTA.crc32(hot_reload_code);
let hot_res = OTA.update_from_string(hot_reload_code, hot_crc, true);
assert(hot_res.ok == true, "带 auto_exec=true 的 OTA 更新应执行成功");
assert(hot_res.auto_exec == true, "结果中 auto_exec 应为 true");

print("OTA 无线空中升级深度压测完成: 全部通过！");

print("\n==================================================");
print("  ★★★ 深度压测总结: MQTT / VFS / OTA 全部 100% 通过！★★★");
print("==================================================");
