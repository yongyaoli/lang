// ==============================================================================
// HC-SR04 超声波测距传感器驱动库 (纯 L 语言实现)
// 适用硬件: ESP32 全系列 (真实 Trig/Echo 引脚) / PC 宿主环境 (自动仿真模式)
// 测量原理:
//   1. 向 Trig 引脚发出至少 10 微秒的高电平触发脉冲
//   2. 模块自动发射 8 个 40kHz 超声波方波并检测回波
//   3. Echo 引脚输出高电平，高电平持续时间即为超声波往返时间
//   4. 距离计算公式: 距离(厘米) = 脉宽时间(微秒) * 0.0343 / 2 = 脉宽时间 / 58.0
// ==============================================================================

fn HC_SR04(trig_pin, echo_pin) {
    let trig = pin(trig_pin, "out");
    let echo = pin(echo_pin, "in");
    trig.low();

    let dev = {
        "trig": trig,
        "echo": echo,
        "trig_pin": trig_pin,
        "echo_pin": echo_pin,
        "temperature": 20.0, // 环境温度补偿，默认 20℃ (声速 343.5m/s)

        // 设置当前环境温度(摄氏度)，动态提高声速测距精度
        // 空气声速公式: V = 331.3 + 0.606 * T (米/秒)
        "set_temperature": fn(t_celsius) {
            self.temperature = t_celsius;
            return self;
        },

        // 单次原始脉宽测量 (返回高电平持续微秒数, 超时或异常返回 -1)
        "measure_pulse_us": fn(timeout_us) {
            if timeout_us == nil { timeout_us = 30000; } // 30ms 对应约 5 米极限测距

            // 1. 确保 Trig 处于低电平基线
            self.trig.low();
            if !ESP32.info().is_hardware {
                // PC 模拟模式直接返回 1160 微秒 (约合 20.0 厘米)
                return 1160;
            }

            // 2. 发出 12 微秒高电平触发脉冲
            self.trig.high();
            let t_start = ESP32.ticks_us();
            while (ESP32.ticks_us() - t_start < 12) {
                // 极简微秒自旋
            }
            self.trig.low();

            // 3. 测量 Echo 引脚高电平宽度
            let duration = self.echo.pulse_in(1, timeout_us);
            return duration;
        },

        // 读取距离 (厘米 cm, 保留 2 位小数)
        // 测量失败或超出范围 (2cm ~ 400cm) 返回 -1.0
        "read_cm": fn() {
            let duration = self.measure_pulse_us(30000);
            if duration <= 0 {
                return -1.0;
            }

            // 根据环境温度精确计算声速: cm/us = (331.3 + 0.606 * T) * 100 / 1000000
            let speed_cm_us = (331.3 + 0.606 * self.temperature) * 0.0001;
            let dist = (duration * speed_cm_us) / 2.0;

            // 超声波盲区与极限距离截断 (2cm ~ 450cm)
            if dist < 2.0 or dist > 450.0 {
                return -1.0;
            }
            return dist;
        },

        // 读取距离 (毫米 mm, 整数)
        "read_mm": fn() {
            let cm = self.read_cm();
            if cm < 0.0 { return -1; }
            return floor(cm * 10.0);
        },

        // 滤波平滑测距 (多次采样去除跳变噪点，默认采样 5 次取中位数)
        "read_smooth_cm": fn(samples) {
            if samples == nil or samples < 3 { samples = 5; }
            let valid_list = [];
            for (let i = 0; i < samples; i = i + 1) {
                let d = self.read_cm();
                if d > 0.0 {
                    push(valid_list, d);
                }
                ESP32.delay_ms(15); // 避开超声波残余回波反射干扰
            }

            if len(valid_list) == 0 {
                return -1.0;
            }
            // 排序后取中位数
            let sorted = sort(valid_list);
            let mid = len(sorted) / 2;
            return sorted[mid];
        }
    };

    // 默认居中
    return dev;
}

// 导出创建工厂函数，支持 import "drivers/HC_SR04.l" 后直接 HC_SR04.create(...) 或 HC_SR04(...) 调用
let create = HC_SR04;
