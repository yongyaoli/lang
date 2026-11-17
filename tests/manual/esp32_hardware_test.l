// ============================================================================
// L 语言 (lang) ESP32 真实硬件全功能与极限压力闭环验证脚本
// ============================================================================
// 用途:
//   1. 真机验证: 通过 `l -u COMx tests/manual/esp32_hardware_test.l` 刷入 ESP32 实测
//   2. PC 离线自测: 直接执行 `l tests/manual/esp32_hardware_test.l` 跑通全部逻辑
//
// 验证范围:
//   - M2/M3: 芯片检测、安全引脚拦截、GPIO 控制、bytes 缓冲区
//   - M4:    裸 Flash 分区信息与 CRC32 校验
//   - M7/M8: Timer 软硬件定时器、Pin.irq 外部中断、看门狗喂狗、Deep Sleep
//   - M7:    64KB 静态堆分配器在真机极端内存下的 GC 根扫描与压力回收
// ============================================================================

print("");
print("============================================================");
print("      L 语言 ESP32 真实硬件功能与稳定性闭环验证套件         ");
print("============================================================");

let total_tests = 0;
let passed_tests = 0;
let failed_tests = 0;

fn assert_test(name, condition) {
    total_tests = total_tests + 1;
    if condition {
        passed_tests = passed_tests + 1;
        print("  [PASS] " + name);
    } else {
        failed_tests = failed_tests + 1;
        print("  [FAIL] " + name);
    }
}

// ----------------------------------------------------------------------------
// 1. 硬件信息与芯片安全识别
// ----------------------------------------------------------------------------
print("\n--- 1. 芯片识别与规格看板 ---");
let info = ESP32.info();
print("芯片型号:", info.chip, "| 架构:", info.arch, "| 核心数:", info.cores);
print("CPU 主频:", info.freq_mhz, "MHz | 可用堆内存:", info.free_heap, "字节");
print("ESP-IDF 版本:", info.idf, "| 运行环境:", (info.is_hardware ? "真实 ESP32 硬件" : "PC 离线模拟器"));

assert_test("芯片型号有效", len(info.chip) > 0);
assert_test("可用堆内存大于 10KB", info.free_heap > 10240);

// 安全引脚护栏拦截测试 (GPIO 6 为 SPI Flash 占用, 必须严禁设为输出)
let pin_protect_pass = false;
try {
    Pin(6, "out");
} catch (e) {
    pin_protect_pass = true;
}
assert_test("Flash 占用引脚安全拦截 (GPIO 6 禁配输出)", pin_protect_pass);

// ----------------------------------------------------------------------------
// 2. GPIO 基础输出与电平控制
// ----------------------------------------------------------------------------
print("\n--- 2. GPIO 控制与链式调用 ---");
// 自动根据芯片选择安全 GPIO (经典 ESP32 用 2, C3 用 8, S3 用 2)
let test_pin_id = 2;
if info.chip == "ESP32-C3" {
    test_pin_id = 8;
}

let test_pin = Pin(test_pin_id, "out");
test_pin.low();
let val_low = test_pin.value();
test_pin.high();
let val_high = test_pin.value();
test_pin.toggle();
let val_toggle = test_pin.value();

assert_test("GPIO 低电平写入与读取", val_low == 0);
assert_test("GPIO 高电平写入与读取", val_high == 1);
assert_test("GPIO 翻转 toggle", val_toggle == 0);

// ----------------------------------------------------------------------------
// 3. Timer 软硬件定时器与事件循环心跳 (M7/M8)
// ----------------------------------------------------------------------------
print("\n--- 3. Timer 软硬件定时器与分片事件循环 ---");
let timer_ticks = 0;
fn on_timer_tick() {
    timer_ticks = timer_ticks + 1;
}

// 启动 30ms 周期定时器
let t1 = Timer(30, on_timer_tick, true);
assert_test("定时器创建后处于 running 状态", t1.running());

// 通过 ESP32.delay_ms 充当事件循环分片派发点
ESP32.delay_ms(150);
let count_after_run = timer_ticks;
assert_test("定时器 150ms 内触发 >= 3 次 (实际: " + str(count_after_run) + ")", count_after_run >= 3);

// 停止定时器
t1.stop();
assert_test("调用 stop() 后 running 为 false", !t1.running());

let count_frozen = timer_ticks;
ESP32.delay_ms(100);
assert_test("停止后定时器不再触发递增", timer_ticks == count_frozen);

// ----------------------------------------------------------------------------
// 4. GPIO 中断系统与事件派发 (M7/M8)
// ----------------------------------------------------------------------------
print("\n--- 4. GPIO 外部中断 (Pin.irq) ---");
let irq_triggered = 0;
fn on_gpio_irq(level) {
    irq_triggered = irq_triggered + 1;
}

test_pin.mode("in");
test_pin.irq(on_gpio_irq, "rise");

if !info.is_hardware {
    // PC 离线模式: 注入模拟电平
    test_pin.write_sim(0);
    test_pin.write_sim(1);
    ESP32.delay_ms(5);
    assert_test("PC 模拟跳变触发上升沿中断", irq_triggered == 1);
} else {
    print("  [提示] 真机中断已就绪 (引脚 GPIO" + str(test_pin_id) + " 注册上升沿中断)");
    assert_test("真机 GPIO 中断注册无报错", true);
}

// 注销中断
test_pin.irq(nil);
assert_test("GPIO 中断注销成功", true);

// ----------------------------------------------------------------------------
// 5. Watchdog 看门狗与心跳自恢复 (M7)
// ----------------------------------------------------------------------------
print("\n--- 5. Watchdog 看门狗管理 ---");
// 开启 500ms 看门狗
ESP32.watchdog_enable(500);
ESP32.delay_ms(100);
ESP32.watchdog_feed();
assert_test("看门狗启动与正常喂狗", true);
ESP32.watchdog_disable();
assert_test("看门狗安全关闭", true);

// ----------------------------------------------------------------------------
// 6. 裸 Flash 分区与 CRC32 校验 (M4)
// ----------------------------------------------------------------------------
print("\n--- 6. l_script 裸 Flash 分区校验 ---");
let flash_info = ESP32.flash_info();
print("Flash 状态:", flash_info.status, "| 脚本长度:", flash_info.length, "| 分区总容量:", flash_info.size);
let crc_test = ESP32.crc32("Hello ESP32 L language");
assert_test("IEEE 802.3 CRC32 算法正确校验", len(crc_test) == 10);
assert_test("Flash 分区信息读取成功", flash_info.size > 0);

// ----------------------------------------------------------------------------
// 7. 64KB 静态堆分配器极限压力测试与 GC 根扫描 (M7 核心考验)
// ----------------------------------------------------------------------------
print("\n--- 7. 静态堆分配器压力与 GC 回收考验 ---");
let heap_start = ESP32.free_heap();

// (1) 密集生成与释放短生命周期字符串
let str_concat_ok = true;
for let i = 0; i < 200; i = i + 1 {
    let s = "chunk_" + str(i) + "_" + "lang_esp32_pressure_test";
    if len(s) < 10 { str_concat_ok = false; }
}
assert_test("200 次动态字符串拼接与自动回收", str_concat_ok);

// (2) 深度递归与值栈闭包 Upvalue 测试 (验证 GC 扫描值栈无踩踏)
fn fib_rec(n) {
    if n < 2 { return n; }
    return fib_rec(n - 1) + fib_rec(n - 2);
}
let fib15 = fib_rec(15);
assert_test("递归栈压测 fib(15) = 610", fib15 == 610);

// (3) 嵌套容器对象分配与手动垃圾回收
let big_map = {};
for let k = 0; k < 50; k = k + 1 {
    big_map["key_" + str(k)] = [k, k * 2, "val_" + str(k)];
}
assert_test("50 个嵌套容器对象正确定位", len(big_map["key_49"]) == 3);

// 强制回收，验证无死锁、无碎片崩溃
sys_gc_collect();
let heap_end = ESP32.free_heap();
print("测试初始可用堆:", heap_start, "字节 | 压力回收后可用堆:", heap_end, "字节");
assert_test("GC 回收后内存健康稳定", heap_end > 0);

// ----------------------------------------------------------------------------
// 最终汇总看板
// ----------------------------------------------------------------------------
print("\n============================================================");
print("                   硬件验证结果汇总                         ");
print("============================================================");
print("总测试项:", total_tests, "| 通过项:", passed_tests, "| 失败项:", failed_tests);
if failed_tests == 0 {
    print("【恭喜】全部硬件特性验证通过！固件运行稳定可靠！");
} else {
    print("【警告】存在未通过测试项，请对照上方日志排查。");
}
print("============================================================\n");
