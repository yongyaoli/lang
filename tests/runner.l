// ==============================================================================
// L 语言 官方自动化回归测试运行器 (Test Runner)
// 纯 L 语言自实现: 自动管理 TCP 回显服务, 精确比对 tests/[01-26]_*.l 与 .out
// 运行方式: l tests/runner.l
// ==============================================================================

import "fs";

print("==================================================");
print("     L 语言 官方自动化回归测试套件 (Test Runner)");
print("==================================================");

// 1. 若运行 21_tcp_socket.l，在后台启动回显服务
print("1. 正在初始化测试上下文与辅助服务...");
sys_exec("start /B .\\l.exe scripts/echo_server.l > nul 2>&1");
sleep(0.3); // 等待回显服务监听 23075 端口

let tests = [
    {"name": "01_basic.l",             "desc": "核心基础语法与表达式求值"},
    {"name": "02_closure.l",           "desc": "闭包、作用域与高阶函数"},
    {"name": "03_collections.l",       "desc": "列表 Array 与哈希字典 Map 集合操作"},
    {"name": "04_help.l",              "desc": "内置 help() 文档与自省"},
    {"name": "05_stdlib_string.l",     "desc": "字符串标准库与切片查找"},
    {"name": "06_stdlib_math_time.l",  "desc": "数学库与系统时间函数"},
    {"name": "07_gc.l",                "desc": "Mark-Sweep 垃圾回收器与内存边界"},
    {"name": "08_ternary.l",           "desc": "三元运算符 a ? b : c"},
    {"name": "09_higher_order.l",      "desc": "函数式 map/filter/reduce 高阶算子"},
    {"name": "10_io_sys.l",            "desc": "系统属性与标准 I/O"},
    {"name": "11_utf8_sort_format.l",  "desc": "UTF-8 字符处理与排序格式化"},
    {"name": "12_import.l",            "desc": "模块导入机制 import 语法"},
    {"name": "13_io_sys_ext.l",        "desc": "扩展系统调用与环境变量"},
    {"name": "14_int_float_bitops.l",  "desc": "整型/浮点位运算类型安全保护"},
    {"name": "15_method_try_pin.l",    "desc": "链式调用与 try-catch 异常安全"},
    {"name": "16_esp32_m3.l",          "desc": "ESP32 芯片安全引脚护栏拦截"},
    {"name": "17_hal_flash_m4.l",      "desc": "裸 Flash 分区与 CRC32 数据帧"},
    {"name": "18_net_wifi_http.l",     "desc": "WiFi 驱动与网络连接诊断"},
    {"name": "19_vm_bytecode.l",       "desc": "字节码虚拟机编译与执行"},
    {"name": "20_m7_irq_timer.l",      "desc": "GPIO 中断与软件定时器"},
    {"name": "21_tcp_socket.l",        "desc": "TCP 客户端套接字与超时保护"},
    {"name": "22_tcp_server_and_ble.l","desc": "TcpServer 服务端与 BLE 蓝牙广播"},
    {"name": "23_json.l",              "desc": "流式高性能 JSON 编解码器"},
    {"name": "24_mqtt.l",              "desc": "MQTT 3.1.1 协议栈与物联网通信"},
    {"name": "25_fs.l",                "desc": "统一虚拟文件系统与流式大文件句柄"},
    {"name": "26_ota.l",               "desc": "OTA 空中升级与 Web OTA 内存热重载"},
    {"name": "27_vm_stress.l",         "desc": "栈式VM与预编译字节码深度压测"}
];

let total = len(tests);
let passed = 0;
let failed = 0;

print("2. 正在运行全量 " + str(total) + " 项回归测试 (精确比对预期输出)...\n");

for (let i = 0; i < total; i = i + 1) {
    let item = tests[i];
    let file = item.name;
    let desc = item.desc;
    let path = "tests/" + file;
    let base = substr(file, 0, len(file) - 2);
    let out_file = "tests/" + base + ".out";

    let idx_str = str(i + 1);
    if (len(idx_str) == 1) {
        idx_str = "0" + idx_str;
    }

    let prefix = "[" + idx_str + "/" + str(total) + "] " + file;
    while (len(prefix) < 32) {
        prefix = prefix + " ";
    }

    // 执行脚本并将标准输出重定向到临时文件
    let tmp_out = "build-test/test_out.tmp";
    sys_exec(".\\l.exe " + path + " > " + tmp_out + " 2> nul");

    let is_ok = false;

    // 检查是否存在预期 .out 文件
    if (FS.exists(out_file)) {
        let expected = trim(replace(FS.read(out_file), "\r\n", "\n"));
        let actual = "";
        if (FS.exists(tmp_out)) {
            actual = trim(replace(FS.read(tmp_out), "\r\n", "\n"));
        }
        if (expected == actual) {
            is_ok = true;
        }
    } else {
        let ret = sys_exec(".\\l.exe " + path + " > nul 2> nul");
        if (ret == 0) {
            is_ok = true;
        }
    }

    if (is_ok) {
        print(prefix + " -> √ PASS  (" + desc + ")");
        passed = passed + 1;
    } else {
        print(prefix + " -> × FAIL  (" + desc + ")");
        failed = failed + 1;
    }
}

// 清理临时文件
if (FS.exists("build-test/test_out.tmp")) {
    FS.remove("build-test/test_out.tmp");
}

print("\n==================================================");
print("                测试套件执行完成报告");
print("==================================================");
print("总测试数 :", total);
print("通过数量 :", passed);
print("失败数量 :", failed);
let rate = passed * 100 / total;
print("通过率   :", str(rate) + "%");

if (failed == 0) {
    print("结论     : ★★★ 全部测试 " + str(total) + "/" + str(total) + " 100% PASS！质量基线极其稳健！★★★");
} else {
    print("结论     : 存在未通过用例，请检查上述失败项！");
}
print("==================================================");
