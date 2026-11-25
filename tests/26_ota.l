// ============================================
// 测试 26: 无线网络空中升级系统 (OTA 标准库, M15)
// ============================================

fn assert(cond, msg) {
    if not cond {
        print("断言失败:", msg);
        exit(1);
    }
}

print("--- 测试 1. 全局单例与模块导入 ---");
assert(OTA != nil, "全局单例 OTA 应当已就绪");
assert(ota != nil, "全局单例 ota (小写) 应当已就绪");
import "ota";
assert(ota.OTA != nil, "模块导出应当包含 OTA");
print("OTA 全局对象与 ota 模块加载正常");

print("--- 测试 2. Flash 分区状态看板 (OTA.status) ---");
let st = OTA.status();
assert(st.partition == "l_script", "目标分区应当为裸脚本分区 l_script");
assert(st.partition_size > 0, "分区大小应当大于 0");
assert(st.status != nil, "应当包含状态字符串");
print("分区名称:", st.partition, "大小:", st.partition_size, "字节, 堆空间:", st.heap_capacity);

print("--- 测试 3. CRC32 算法校验 (OTA.crc32) ---");
let text = "Hello Lang OTA 2026";
let c1 = OTA.crc32(text);
assert(c1 != 0, "字符串 CRC32 应当非 0");
// 重复计算结果必须严格一致
let c2 = OTA.crc32(text);
assert(c1 == c2, "相同字符串计算的 CRC32 必须一致");

// bytes 字节数组 CRC32 计算
let b = bytes(4);
b[0] = 0xAA; b[1] = 0xBB; b[2] = 0xCC; b[3] = 0xDD;
let c_bytes = OTA.crc32(b);
assert(c_bytes != 0, "bytes 对象计算 CRC32 应当成功");
print("CRC32 算法检验通过:", c1);

print("--- 测试 4. 字符串直接 OTA 更新 (OTA.update_from_string) ---");
let new_script = "print(\"[OTA-HotReload] 远程新版本脚本热重载成功!\"); let ota_test_var = 8888;";
let calc_crc = OTA.crc32(new_script);

// auto_exec=false 仅写入不热执行
let res1 = OTA.update_from_string(new_script, calc_crc, false);
assert(res1.ok == true, "写入结果 ok 应当为 true");
assert(res1.bytes == len(new_script), "写入字节数应当等于脚本长度");
assert(res1.crc32_int == calc_crc, "记录的 CRC32 应当匹配");

let st2 = OTA.status();
assert(st2.valid == true, "写入后分区状态应当 valid");
assert(st2.script_length == len(new_script), "分区记录的代码长度匹配");
print("字符串写入 Flash 分区正常, 代码长度:", st2.script_length);

print("--- 测试 5. CRC32 防篡改校验拦截 (错误 CRC 抛出异常) ---");
let caught_crc_err = false;
try {
    // 传入一个故意的错误期望 CRC32: 0x12345678
    OTA.update_from_string(new_script, 0x12345678, false);
} catch (e) {
    caught_crc_err = true;
    assert(e.type != nil, "捕获的异常应当包含错误类型");
}
assert(caught_crc_err == true, "CRC32 错误时必须抛出异常拦截写入");
print("CRC32 错误完整性拦截正常");

print("--- 测试 6. 模拟 HTTP 远程 OTA 升级 (OTA.update_script) ---");
let firmware_url = "https://iot.lang.org/firmware/v2.0/app.l";
let v2_script = "print(\"== [OTA v2.0] 设备无线空中升级运行中 ==\");";
let v2_crc = OTA.crc32(v2_script);

// 注册模拟 HTTP 下载源
OTA.sim_set_response(firmware_url, v2_script);

// 执行无线远程脚本空中升级
let res_ota = OTA.update_script(firmware_url, v2_crc, false);
assert(res_ota.ok == true, "远程 OTA 升级应当成功");
assert(res_ota.bytes == len(v2_script), "OTA 升级字节数匹配");

let st3 = OTA.status();
assert(st3.script_length == len(v2_script), "升级后 Flash 分区代码长度匹配");
print("远程 OTA 脚本拉取、校验与刷入顺利完成");

print("--- 测试 7. 非法协议与异常 URL 拦截 ---");
let caught_http_err = false;
try {
    // 传入非法的协议格式 (毫秒级本地异常拦截，避免耗费网络 DNS 超时)
    OTA.update_script("ftp://invalid-host/test.l");
} catch (e) {
    caught_http_err = true;
}
assert(caught_http_err == true, "非法协议必须被正确捕获");
print("非法协议拦截正常");

print("--- 测试 8. 分区擦除格式化 (OTA.erase) ---");
assert(OTA.erase() == true, "擦除分区应当成功");
let st_erased = OTA.status();
assert(st_erased.status == "empty", "擦除后分区状态应当为 empty");
assert(st_erased.script_length == 0, "擦除后代码长度应当归零");
print("OTA 裸分区擦除成功");

print("\n============================================");
print("     OTA 无线空中升级回归测试全部通过！");
print("============================================");
