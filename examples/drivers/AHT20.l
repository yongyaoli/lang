// ============================================================================
// AHT20 高精度 I2C 温湿度传感器驱动 (纯 L 语言实现)
// I2C 默认从机地址: 0x38
// ============================================================================

fn create_aht20(i2c, addr) {
    if addr == nil { addr = 0x38; }

    // PC 模拟模式下注册虚拟设备
    if !ESP32.info().is_hardware {
        i2c.sim_device(addr);
    }

    let dev = {
        "i2c": i2c,
        "addr": addr,

        // 初始化 AHT20, 检查校准位
        "init": fn() {
            // 上电延时稳定
            ESP32.delay_ms(40);

            // 读取状态字
            let st = 0x18;
            try {
                let rx = i2c.read(addr, 1);
                if len(rx) > 0 { st = rx[0]; }
            } catch (e) {
                // PC 模拟或首次读取
                st = 0x18;
            }

            // bit 3 为 1 表示已校准，若未校准则发送初始化指令
            if (st & 0x08) == 0 {
                i2c.write(addr, [0xBE, 0x08, 0x00]);
                ESP32.delay_ms(10);
            }
            return self;
        },

        // 触发测量并读取温湿度
        // 返回字典: { "humidity": 相对湿度(0~100%), "temp_c": 摄氏度, "temp_f": 华氏度, "ok": true }
        "read": fn() {
            // 发送触发测量命令: 0xAC 0x33 0x00
            i2c.write(addr, [0xAC, 0x33, 0x00]);

            // 等待测量完成 (典型值 80ms)
            ESP32.delay_ms(80);

            // 读取 6 字节数据
            let data = bytes(6);
            if !ESP32.info().is_hardware {
                // PC 模拟数据: 湿度 55.2%, 温度 24.6℃
                // raw_h = 55.2 * 1048576 / 100 = 578813 (0x8D5FD)
                // raw_t = (24.6 + 50) * 1048576 / 200 = 391118 (0x05F7CE)
                data = bytes([0x18, 0x8D, 0x5F, 0xD5, 0xF7, 0xCE]);
            } else {
                data = i2c.read(addr, 6);
            }

            if len(data) < 6 {
                throw "AHT20 读取数据长度不足 6 字节";
            }

            // 检查状态字 bit 7 (Busy 标志, 0 为就绪)
            let status = data[0];
            if (status & 0x80) != 0 {
                throw "AHT20 传感器处于繁忙状态";
            }

            // 解析 20 位湿度数据: data[1], data[2], data[3] 高 4 位
            let h_raw = (data[1] << 12) | (data[2] << 4) | (data[3] >> 4);
            let humidity = (h_raw * 100.0) / 1048576.0;

            // 解析 20 位温度数据: data[3] 低 4 位, data[4], data[5]
            let t_raw = ((data[3] & 0x0F) << 16) | (data[4] << 8) | data[5];
            let temp_c = ((t_raw * 200.0) / 1048576.0) - 50.0;
            let temp_f = temp_c * 1.8 + 32.0;

            return {
                "humidity": round(humidity * 10.0) / 10.0,
                "temp_c": round(temp_c * 10.0) / 10.0,
                "temp_f": round(temp_f * 10.0) / 10.0,
                "ok": true
            };
        }
    };

    return dev;
}

// 导出构造函数
let create = create_aht20;
let new = create_aht20;
let AHT20 = create_aht20;
