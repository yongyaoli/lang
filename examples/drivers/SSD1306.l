// ============================================================================
// SSD1306 0.96寸 I2C OLED 显示屏驱动 (纯 L 语言实现)
// 适配分辨率: 128x64 / 128x32, 默认 I2C 地址 0x3C
// ============================================================================

// 5x7 ASCII 紧凑基础字模表 (常用字符: 空格, 符号, 0-9, A-Z, a-z)
// 每个字符占 5 个字节列扫描数据
const FONT5x7 = {
    " ": [0x00, 0x00, 0x00, 0x00, 0x00],
    "!": [0x00, 0x00, 0x5F, 0x00, 0x00],
    ":": [0x00, 0x36, 0x36, 0x00, 0x00],
    "-": [0x08, 0x08, 0x08, 0x08, 0x08],
    ".": [0x00, 0x60, 0x60, 0x00, 0x00],
    "/": [0x20, 0x10, 0x08, 0x04, 0x02],
    "=": [0x14, 0x14, 0x14, 0x14, 0x14],
    "0": [0x3E, 0x51, 0x49, 0x45, 0x3E],
    "1": [0x00, 0x42, 0x7F, 0x40, 0x00],
    "2": [0x42, 0x61, 0x51, 0x49, 0x46],
    "3": [0x21, 0x41, 0x45, 0x4B, 0x31],
    "4": [0x18, 0x14, 0x12, 0x7F, 0x10],
    "5": [0x27, 0x45, 0x45, 0x45, 0x39],
    "6": [0x3C, 0x4A, 0x49, 0x49, 0x30],
    "7": [0x01, 0x71, 0x09, 0x05, 0x03],
    "8": [0x36, 0x49, 0x49, 0x49, 0x36],
    "9": [0x06, 0x49, 0x49, 0x29, 0x1E],
    "A": [0x7C, 0x12, 0x11, 0x12, 0x7C],
    "B": [0x7F, 0x49, 0x49, 0x49, 0x36],
    "C": [0x3E, 0x41, 0x41, 0x41, 0x22],
    "D": [0x7F, 0x41, 0x41, 0x22, 0x1C],
    "E": [0x7F, 0x49, 0x49, 0x49, 0x41],
    "F": [0x7F, 0x09, 0x09, 0x09, 0x01],
    "G": [0x3E, 0x41, 0x49, 0x49, 0x7A],
    "H": [0x7F, 0x08, 0x08, 0x08, 0x7F],
    "I": [0x00, 0x41, 0x7F, 0x41, 0x00],
    "L": [0x7F, 0x40, 0x40, 0x40, 0x40],
    "O": [0x3E, 0x41, 0x41, 0x41, 0x3E],
    "P": [0x7F, 0x09, 0x09, 0x09, 0x06],
    "R": [0x7F, 0x09, 0x19, 0x29, 0x46],
    "S": [0x46, 0x49, 0x49, 0x49, 0x31],
    "T": [0x01, 0x01, 0x7F, 0x01, 0x01],
    "C": [0x3E, 0x41, 0x41, 0x41, 0x22]
};

fn create_ssd1306(i2c, addr, width, height) {
    if addr == nil { addr = 0x3C; }
    if width == nil { width = 128; }
    if height == nil { height = 64; }

    // PC 模拟模式下自动注册虚拟从机
    if !ESP32.info().is_hardware {
        i2c.sim_device(addr);
    }

    let pages = height / 8;
    let buffer_size = width * pages;
    let buffer = bytes(buffer_size);

    let dev = {
        "i2c": i2c,
        "addr": addr,
        "width": width,
        "height": height,
        "pages": pages,
        "buffer": buffer,

        // 发送单字节指令
        "cmd": fn(c) {
            i2c.write_reg(addr, 0x00, [c]);
        },

        // 发送多字节指令序列
        "cmds": fn(list) {
            i2c.write_reg(addr, 0x00, list);
        },

        // 初始化 OLED 屏幕寄存器
        "init": fn() {
            // 基础初始化序列
            self.cmds([
                0xAE,       // 关闭显示
                0xD5, 0x80, // 设置显示时钟分频
                0xA8, 0x3F, // 多路复用比率 (64)
                0xD3, 0x00, // 显示偏移为 0
                0x40,       // 起始行 0
                0x8D, 0x14, // 开启电荷泵 (Charge Pump Enable)
                0x20, 0x00, // 内存寻址模式: 水平寻址 (Horizontal Addressing)
                0xA1,       // 段重映射 (左右反转, 正常显示)
                0xC8,       // COM 扫描方向 (上下反转, 正常显示)
                0xDA, 0x12, // COM 引脚硬件配置
                0x81, 0xCF, // 对比度控制 (0~255)
                0xD9, 0xF1, // 预充电周期
                0xDB, 0x40, // VCOMH 稳压脱开电平
                0xA4,       // 全局显示开启, 恢复 RAM 模式
                0xA6,       // 正常显示模式 (非反显)
                0xAF        // 打开显示 (Display ON)
            ]);
            self.clear();
            self.show();
            return self;
        },

        // 清空显存
        "clear": fn() {
            buffer.fill(0x00);
            return self;
        },

        // 全亮填充显存
        "fill": fn(color) {
            let val = (color ? 0xFF : 0x00);
            buffer.fill(val);
            return self;
        },

        // 绘制单个像素点 (x: 0..127, y: 0..63)
        "pixel": fn(x, y, color) {
            if x < 0 or x >= width or y < 0 or y >= height { return self; }
            if color == nil { color = 1; }
            let page = y / 8;
            let bit_idx = y % 8;
            let idx = page * width + x;
            let cur = buffer[idx];
            if color {
                buffer[idx] = cur | (1 << bit_idx);
            } else {
                buffer[idx] = cur & (~(1 << bit_idx) & 0xFF);
            }
            return self;
        },

        // 绘制单个字符 (基于 5x7 字模)
        "char": fn(x, y, ch) {
            let cols = FONT5x7[ch];
            if cols == nil {
                // 未收录字符用方块或空格代替
                cols = [0x7F, 0x41, 0x41, 0x41, 0x7F];
            }
            for let col_idx = 0; col_idx < 5; col_idx = col_idx + 1 {
                let col_data = cols[col_idx];
                for let bit_idx = 0; bit_idx < 7; bit_idx = bit_idx + 1 {
                    if (col_data & (1 << bit_idx)) != 0 {
                        self.pixel(x + col_idx, y + bit_idx, 1);
                    }
                }
            }
            return self;
        },

        // 绘制英文字符串文本
        "text": fn(x, y, text_str) {
            let cur_x = x;
            let text_len = len(text_str);
            for let i = 0; i < text_len; i = i + 1 {
                let ch = text_str[i];
                self.char(cur_x, y, ch);
                cur_x = cur_x + 6; // 5 像素字符 + 1 像素间距
            }
            return self;
        },

        // 绘制水平横线
        "hline": fn(x, y, w) {
            for let i = 0; i < w; i = i + 1 {
                self.pixel(x + i, y, 1);
            }
            return self;
        },

        // 将显存数据完整写入 OLED 屏幕
        "show": fn() {
            // 设置列地址范围 0..127
            self.cmds([0x21, 0, width - 1]);
            // 设置页地址范围 0..pages-1
            self.cmds([0x22, 0, pages - 1]);

            // 分块写入显存数据 (每次 32 字节, 避免单包过长)
            for let offset = 0; offset < buffer_size; offset = offset + 32 {
                let chunk_len = 32;
                if offset + chunk_len > buffer_size {
                    chunk_len = buffer_size - offset;
                }
                let slice = buffer.slice(offset, offset + chunk_len);
                i2c.write_reg(addr, 0x40, slice);
            }
            return self;
        }
    };

    return dev;
}

// 导出构造函数
let create = create_ssd1306;
let new = create_ssd1306;
let SSD1306 = create_ssd1306;
