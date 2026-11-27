// ==============================================================================
// 标准 180° / 360° 舵机平滑角度控制驱动库 (纯 L 语言实现)
// 适用硬件: ESP32 全系列 (真实 PWM/LEDC 输出) / PC 宿主环境 (自动仿真模式)
// 控制规范:
//   - 标准航模/机器人舵机控制信号周期为 20ms (50Hz PWM 频率)
//   - 0.5ms (500us) 对应 0 度最小行程 (占空比约 2.5%, 10位即 26/1023)
//   - 1.5ms (1500us) 对应 90 度中位行程 (占空比约 7.5%, 10位即 77/1023)
//   - 2.5ms (2500us) 对应 180 度最大行程 (占空比约 12.5%, 10位即 128/1023)
// ==============================================================================

fn Servo(pin_num) {
    let min_us = 500;   // 0度对应脉冲宽度(微秒)
    let max_us = 2500;  // 180度对应脉冲宽度(微秒)

    // 创建 50Hz (20ms) 的 PWM 控制对象
    let pwm = PWM(pin_num, 50, 0);

    let dev = {
        "pwm": pwm,
        "pin": pin_num,
        "min_us": min_us,
        "max_us": max_us,
        "current_angle": 0.0,
        "is_attached": true,

        // 将脉宽微秒转换为 10位 (0~1023) PWM 占空比
        // 50Hz 周期 = 20000 微秒
        "_us_to_duty": fn(us) {
            if us < self.min_us { us = self.min_us; }
            if us > self.max_us { us = self.max_us; }
            let duty = floor(us * 1023.0 / 20000.0);
            return duty;
        },

        // 直接写入微秒脉宽 (500us ~ 2500us)
        "write_us": fn(us) {
            let d = self._us_to_duty(us);
            self.pwm.duty(d);
            return self;
        },

        // 设置目标角度 (0.0 ~ 180.0 度)
        "write": fn(angle) {
            if angle < 0.0 { angle = 0.0; }
            if angle > 180.0 { angle = 180.0; }
            self.current_angle = angle;

            // 线性插值映射角度到微秒
            let us = self.min_us + (angle / 180.0) * (self.max_us - self.min_us);
            self.write_us(us);
            return self;
        },

        // 读取当前设定的角度
        "read": fn() {
            return self.current_angle;
        },

        // 平滑旋转过渡 (从当前角度平滑步进到目标角度，避免急冲过载)
        // target_angle: 目标角度 (0~180)
        // step_deg: 每次步进角度 (默认 2 度)
        // interval_ms: 步进间隔延时 (默认 15 毫秒)
        "sweep_to": fn(target_angle, step_deg, interval_ms) {
            if step_deg == nil or step_deg <= 0 { step_deg = 2.0; }
            if interval_ms == nil or interval_ms <= 0 { interval_ms = 15; }
            if target_angle < 0.0 { target_angle = 0.0; }
            if target_angle > 180.0 { target_angle = 180.0; }

            let cur = self.current_angle;
            if cur < target_angle {
                while cur < target_angle {
                    cur = cur + step_deg;
                    if cur > target_angle { cur = target_angle; }
                    self.write(cur);
                    ESP32.delay_ms(interval_ms);
                }
            } else if cur > target_angle {
                while cur > target_angle {
                    cur = cur - step_deg;
                    if cur < target_angle { cur = target_angle; }
                    self.write(cur);
                    ESP32.delay_ms(interval_ms);
                }
            }
            return self;
        },

        // 360° 连续旋转舵机速度控制
        // speed: -100(全速反转) ~ 0(停止) ~ 100(全速正转)
        "write_speed": fn(speed) {
            if speed < -100 { speed = -100; }
            if speed > 100 { speed = 100; }
            // 速度映射到 0~180 度 (0=反转, 90=静止, 180=正转)
            let angle = (speed + 100) * 180.0 / 200.0;
            self.write(angle);
            return self;
        },

        // 停止 PWM 输出并卸载舵机
        "detach": fn() {
            self.pwm.duty(0);
            self.pwm.stop();
            self.is_attached = false;
            return self;
        }
    };

    // 默认居中归位到 90 度
    dev.write(90.0);
    return dev;
}

// 导出创建工厂函数，支持 import "drivers/Servo.l" 后直接 Servo.create(...) 调用
let create = Servo;
