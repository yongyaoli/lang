/* M7: GPIO 中断(Pin.irq) / Timer 定时器 / Watchdog 看门狗 / Deep Sleep 深度睡眠
 * 全部使用 PC 模拟事件(write_sim 注入电平), 无需真机即可离线验证事件循环 */

print("=== 1. GPIO 中断 (Pin.irq + write_sim) ===");
let irq_count = 0;
let irq_level = -1;
fn on_edge(level) {
    irq_count = irq_count + 1;
    irq_level = level;
}
let p = Pin(2, "in");
p.irq(on_edge, "rise");
p.write_sim(0);        // 0 -> 0 无跳变, 不触发
p.write_sim(1);        // 0 -> 1 上升沿, 触发
p.write_sim(1);        // 1 -> 1 无跳变, 不触发
ESP32.delay_ms(1);     // 派发点: 处理排队的中断事件
print("rise 触发次数:", irq_count, "电平:", irq_level);

p.irq(on_edge, "fall");
p.write_sim(0);        // 1 -> 0 下降沿, 触发
ESP32.delay_ms(1);
print("fall 触发次数:", irq_count, "电平:", irq_level);

p.irq(nil);            // 注销中断
p.write_sim(1);
ESP32.delay_ms(1);
print("注销后触发次数:", irq_count);

print("=== 2. Timer 定时器 ===");
let fires = 0;
fn tick() { fires = fires + 1; }
let one = Timer(30, tick, false);      // 单次定时器
print("创建后运行中:", one.running());
ESP32.delay_ms(100);
print("单次触发次数:", fires);
print("触发后运行中:", one.running());

let rep = Timer(25, tick, true);       // 周期定时器
ESP32.delay_ms(100);
print("周期触发总次数 >= 3:", fires >= 3);
rep.stop();
print("停止后运行中:", rep.running());
let before = fires;
ESP32.delay_ms(60);
print("停止后计数不变:", fires == before);

print("=== 3. Watchdog 看门狗 ===");
ESP32.watchdog_enable(50);
ESP32.watchdog_feed();
ESP32.delay_ms(30);
print("及时喂狗正常运行");
let wdt_caught = false;
try {
    ESP32.delay_ms(120);               // 派发点检测到超时, 抛出可捕获错误
} catch (e) {
    wdt_caught = true;
    print("捕获:", e.message);
}
print("看门狗超时可捕获:", wdt_caught);

print("=== 4. Deep Sleep 与唤醒原因 ===");
print("唤醒前:", ESP32.wake_reason().reason);
ESP32.deepsleep(100);                  // PC 模拟: 记录唤醒原因后返回; 真机复位重启
print("唤醒后:", ESP32.wake_reason().reason);

print("=== M7 测试结束 ===");
