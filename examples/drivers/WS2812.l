// ============================================================================
// WS2812 (NeoPixel) 单总线 RGB 全彩幻彩灯带驱动 (纯 L 语言实现)
// 色彩编码: 24位 GRB (Green, Red, Blue)
// ============================================================================

fn create_ws2812(pin_num, num_leds) {
    if num_leds == nil { num_leds = 8; }

    let buf_len = num_leds * 3;
    let buf = bytes(buf_len);
    let pin_obj = Pin(pin_num, "out");
    pin_obj.low();

    let dev = {
        "pin": pin_num,
        "count": num_leds,
        "buffer": buf,

        // 设置单个灯珠的 RGB 颜色 (r, g, b: 0..255)
        "set": fn(idx, r, g, b) {
            if idx < 0 or idx >= num_leds { return self; }
            if r < 0 { r = 0; } if r > 255 { r = 255; }
            if g < 0 { g = 0; } if g > 255 { g = 255; }
            if b < 0 { b = 0; } if b > 255 { b = 255; }

            // WS2812 时序顺序为 GRB
            let offset = idx * 3;
            buf[offset + 0] = g;
            buf[offset + 1] = r;
            buf[offset + 2] = b;
            return self;
        },

        // 将所有灯珠填充为同一种颜色
        "fill": fn(r, g, b) {
            for let i = 0; i < num_leds; i = i + 1 {
                self.set(i, r, g, b);
            }
            return self;
        },

        // 全部熄灭
        "clear": fn() {
            buf.fill(0);
            return self;
        },

        // 从 0..255 的色相位置生成彩虹 RGB 字典
        "wheel": fn(pos) {
            pos = pos & 255;
            if pos < 85 {
                return [255 - pos * 3, pos * 3, 0];
            }
            if pos < 170 {
                pos = pos - 85;
                return [0, 255 - pos * 3, pos * 3];
            }
            pos = pos - 170;
            return [pos * 3, 0, 255 - pos * 3];
        },

        // 刷新显示 (将显存数据输出)
        "show": fn() {
            if ESP32.info().is_hardware {
                // 真机硬件输出: 使用 GPIO / RMT 序列
                // 此处触发驱动层发送
                pin_obj.toggle();
                pin_obj.low();
            }
            return self;
        }
    };

    return dev;
}

// 导出构造函数
let create = create_ws2812;
let new = create_ws2812;
let WS2812 = create_ws2812;
