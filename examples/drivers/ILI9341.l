// ============================================================================
// ILI9341 2.4/2.8/3.2寸 彩色 SPI TFT LCD 屏幕驱动 (纯 L 语言开源驱动)
// 分辨率: 240x320 像素，65K 色 (RGB565 16位色彩)，SPI 接口
// 包含功能: 软硬件复位、四向旋转、画点/线/矩形/圆形、文本与 5x7 字体无级放大
// ============================================================================

// 常用 RGB565 16 位颜色预定义 (高5位红, 中6位绿, 低5位蓝)
const ILI9341_BLACK       = 0x0000;
const ILI9341_WHITE       = 0xFFFF;
const ILI9341_RED         = 0xF800;
const ILI9341_GREEN       = 0x07E0;
const ILI9341_BLUE        = 0x001F;
const ILI9341_YELLOW      = 0xFFE0;
const ILI9341_CYAN        = 0x07FF;
const ILI9341_MAGENTA     = 0xF81F;
const ILI9341_ORANGE      = 0xFD20;
const ILI9341_NAVY        = 0x000F;
const ILI9341_DARKGREEN   = 0x03E0;
const ILI9341_DARKCYAN    = 0x03EF;
const ILI9341_MAROON      = 0x7800;
const ILI9341_PURPLE      = 0x780F;
const ILI9341_OLIVE       = 0x7BE0;
const ILI9341_LIGHTGREY   = 0xC618;
const ILI9341_DARKGREY    = 0x7BEF;

// RGB888 (0~255) 转 16位 RGB565 辅助函数
fn ili9341_rgb(r, g, b) {
    let r5 = (r & 0xF8) << 8;
    let g6 = (g & 0xFC) << 3;
    let b5 = (b >> 3) & 0x1F;
    return r5 | g6 | b5;
}

// 5x7 ASCII 紧凑基础字模表 (常用字符: 空格, 标点, 0-9, A-Z, a-z)
const ILI9341_FONT5x7 = {
    " ": [0x00, 0x00, 0x00, 0x00, 0x00],
    "!": [0x00, 0x00, 0x5F, 0x00, 0x00],
    ":": [0x00, 0x36, 0x36, 0x00, 0x00],
    "-": [0x08, 0x08, 0x08, 0x08, 0x08],
    "+": [0x08, 0x08, 0x3E, 0x08, 0x08],
    ".": [0x00, 0x60, 0x60, 0x00, 0x00],
    "/": [0x20, 0x10, 0x08, 0x04, 0x02],
    "=": [0x14, 0x14, 0x14, 0x14, 0x14],
    "%": [0x63, 0x66, 0x08, 0x33, 0x63],
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
    "J": [0x20, 0x40, 0x41, 0x3F, 0x01],
    "K": [0x7F, 0x08, 0x14, 0x22, 0x41],
    "L": [0x7F, 0x40, 0x40, 0x40, 0x40],
    "M": [0x7F, 0x02, 0x0C, 0x02, 0x7F],
    "N": [0x7F, 0x04, 0x08, 0x10, 0x7F],
    "O": [0x3E, 0x41, 0x41, 0x41, 0x3E],
    "P": [0x7F, 0x09, 0x09, 0x09, 0x06],
    "Q": [0x3E, 0x41, 0x51, 0x21, 0x5E],
    "R": [0x7F, 0x09, 0x19, 0x29, 0x46],
    "S": [0x46, 0x49, 0x49, 0x49, 0x31],
    "T": [0x01, 0x01, 0x7F, 0x01, 0x01],
    "U": [0x3F, 0x40, 0x40, 0x40, 0x3F],
    "V": [0x1F, 0x20, 0x40, 0x20, 0x1F],
    "W": [0x7F, 0x20, 0x18, 0x20, 0x7F],
    "X": [0x63, 0x14, 0x08, 0x14, 0x63],
    "Y": [0x07, 0x08, 0x70, 0x08, 0x07],
    "Z": [0x61, 0x51, 0x49, 0x45, 0x43]
};

// ============================================================================
// 驱动构造工厂函数: create_ili9341(spi, cs_pin, dc_pin, [rst_pin], [bl_pin])
// ============================================================================
fn create_ili9341(spi, cs_pin, dc_pin, rst_pin, bl_pin) {
    let cs = (cs_pin != nil) ? Pin(cs_pin, "out") : nil;
    let dc = (dc_pin != nil) ? Pin(dc_pin, "out") : nil;
    let rst = (rst_pin != nil) ? Pin(rst_pin, "out") : nil;
    let bl = (bl_pin != nil) ? Pin(bl_pin, "out") : nil;

    if cs != nil { cs.on(); }
    if dc != nil { dc.on(); }
    if bl != nil { bl.on(); }

    let dev = {
        "spi": spi,
        "cs": cs,
        "dc": dc,
        "rst": rst,
        "bl": bl,
        "width": 240,
        "height": 320,
        "rotation": 0,

        // 发送命令字节
        "cmd": fn(c) {
            if self.dc != nil { self.dc.off(); }
            if self.cs != nil { self.cs.off(); }
            self.spi.write([c & 0xFF]);
            if self.cs != nil { self.cs.on(); }
        },

        // 发送单字节数据
        "data1": fn(d) {
            if self.dc != nil { self.dc.on(); }
            if self.cs != nil { self.cs.off(); }
            self.spi.write([d & 0xFF]);
            if self.cs != nil { self.cs.on(); }
        },

        // 发送多字节数据
        "data": fn(arr) {
            if self.dc != nil { self.dc.on(); }
            if self.cs != nil { self.cs.off(); }
            self.spi.write(arr);
            if self.cs != nil { self.cs.on(); }
        },

        // 硬件复位屏幕
        "reset": fn() {
            if self.rst != nil {
                self.rst.off();
                ESP32.delay_ms(20);
                self.rst.on();
                ESP32.delay_ms(150);
            } else {
                self.cmd(0x01); // 软件复位 SWRESET
                ESP32.delay_ms(150);
            }
        },

        // 设置绘制窗口矩形 (X0, Y0) 到 (X1, Y1)
        "set_window": fn(x0, y0, x1, y1) {
            // 列地址设置 CASET (0x2A)
            self.cmd(0x2A);
            self.data([ (x0 >> 8) & 0xFF, x0 & 0xFF, (x1 >> 8) & 0xFF, x1 & 0xFF ]);

            // 行地址设置 PASET (0x2B)
            self.cmd(0x2B);
            self.data([ (y0 >> 8) & 0xFF, y0 & 0xFF, (y1 >> 8) & 0xFF, y1 & 0xFF ]);

            // 显存写入指令 RAMWR (0x2C)
            self.cmd(0x2C);
        },

        // 设置屏幕旋转方向:
        // 0 = 0度竖屏 (240x320), 1 = 90度横屏 (320x240)
        // 2 = 180度竖屏 (240x320), 3 = 270度横屏 (320x240)
        "set_rotation": fn(r) {
            self.rotation = r % 4;
            self.cmd(0x36); // MADCTL

            if self.rotation == 0 {
                self.data1(0x48); // MX | BGR
                self.width = 240;
                self.height = 320;
            } else if self.rotation == 1 {
                self.data1(0x28); // MV | BGR
                self.width = 320;
                self.height = 240;
            } else if self.rotation == 2 {
                self.data1(0x88); // MY | BGR
                self.width = 240;
                self.height = 320;
            } else {
                self.data1(0xE8); // MY | MX | MV | BGR
                self.width = 320;
                self.height = 240;
            }
        },

        // 初始化 ILI9341 控制器寄存器
        "init": fn() {
            self.reset();

            // 电源控制 A / B
            self.cmd(0xCB); self.data([0x39, 0x2C, 0x00, 0x34, 0x02]);
            self.cmd(0xCF); self.data([0x00, 0xC1, 0x30]);
            self.cmd(0xE8); self.data([0x85, 0x00, 0x78]);
            self.cmd(0xEA); self.data([0x00, 0x00]);
            self.cmd(0xED); self.data([0x64, 0x03, 0x12, 0x81]);
            self.cmd(0xF7); self.data([0x20]);

            // 电源控制 1 / 2
            self.cmd(0xC0); self.data1(0x23);
            self.cmd(0xC1); self.data1(0x10);

            // VCOM 控制 1 / 2
            self.cmd(0xC5); self.data([0x3E, 0x28]);
            self.cmd(0xC7); self.data1(0x86);

            // 内存访问控制 (默认竖屏)
            self.set_rotation(0);

            // 像素格式: 16 位/像素 (RGB565)
            self.cmd(0x3A); self.data1(0x55);

            // 帧率控制 (70Hz)
            self.cmd(0xB1); self.data([0x00, 0x18]);

            // 显示功能控制
            self.cmd(0xB6); self.data([0x08, 0x82, 0x27]);

            // 退出睡眠
            self.cmd(0x11);
            ESP32.delay_ms(120);

            // 开启显示
            self.cmd(0x29);
            ESP32.delay_ms(20);

            // 初始清屏为黑色
            self.clear(ILI9341_BLACK);
            return self;
        },

        // 在坐标 (x, y) 画单个像素点
        "pixel": fn(x, y, color) {
            if x < 0 or x >= self.width or y < 0 or y >= self.height { return; }
            self.set_window(x, y, x, y);
            let hi = (color >> 8) & 0xFF;
            let lo = color & 0xFF;
            self.data([hi, lo]);
        },

        // 快速填充矩形 (x, y, w, h)
        "fill_rect": fn(x, y, w, h, color) {
            if w <= 0 or h <= 0 { return; }
            if x < 0 { w = w + x; x = 0; }
            if y < 0 { h = h + y; y = 0; }
            if x + w > self.width { w = self.width - x; }
            if y + h > self.height { h = self.height - y; }
            if w <= 0 or h <= 0 { return; }

            self.set_window(x, y, x + w - 1, y + h - 1);

            let hi = (color >> 8) & 0xFF;
            let lo = color & 0xFF;
            let total_pixels = w * h;

            // 构造 32 像素 (64 字节) 高效块缓冲区循环输出
            let chunk_pixels = 32;
            if chunk_pixels > total_pixels {
                chunk_pixels = total_pixels;
            }
            let chunk_buf = bytes(chunk_pixels * 2);
            for (let i = 0; i < chunk_pixels; i = i + 1) {
                chunk_buf[i * 2]     = hi;
                chunk_buf[i * 2 + 1] = lo;
            }

            if self.dc != nil { self.dc.on(); }
            if self.cs != nil { self.cs.off(); }

            let remaining = total_pixels;
            while remaining >= chunk_pixels {
                self.spi.write(chunk_buf);
                remaining = remaining - chunk_pixels;
            }
            if remaining > 0 {
                self.spi.write(chunk_buf.slice(0, remaining * 2));
            }

            if self.cs != nil { self.cs.on(); }
        },

        // 全屏填充颜色
        "fill": fn(color) {
            self.fill_rect(0, 0, self.width, self.height, color);
        },

        // 清屏
        "clear": fn(color) {
            if color == nil { color = ILI9341_BLACK; }
            self.fill(color);
        },

        // 绘制空心矩形边框
        "rect": fn(x, y, w, h, color) {
            self.fill_rect(x, y, w, 1, color);             // 上边
            self.fill_rect(x, y + h - 1, w, 1, color);     // 下边
            self.fill_rect(x, y, 1, h, color);             // 左边
            self.fill_rect(x + w - 1, y, 1, h, color);     // 右边
        },

        // 绘制任意斜率线段 (Bresenham 算法)
        "line": fn(x0, y0, x1, y1, color) {
            let dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
            let sx = (x0 < x1) ? 1 : -1;
            let dy = (y1 > y0) ? (y0 - y1) : (y1 - y0);
            let sy = (y0 < y1) ? 1 : -1;
            let err = dx + dy;

            while true {
                self.pixel(x0, y0, color);
                if x0 == x1 and y0 == y1 { break; }
                let e2 = err * 2;
                if e2 >= dy {
                    err = err + dy;
                    x0 = x0 + sx;
                }
                if e2 <= dx {
                    err = err + dx;
                    y0 = y0 + sy;
                }
            }
        },

        // 绘制空心圆 (中点画圆法)
        "circle": fn(x0, y0, r, color) {
            let f = 1 - r;
            let ddF_x = 1;
            let ddF_y = -2 * r;
            let x = 0;
            let y = r;

            self.pixel(x0, y0 + r, color);
            self.pixel(x0, y0 - r, color);
            self.pixel(x0 + r, y0, color);
            self.pixel(x0 - r, y0, color);

            while x < y {
                if f >= 0 {
                    y = y - 1;
                    ddF_y = ddF_y + 2;
                    f = f + ddF_y;
                }
                x = x + 1;
                ddF_x = ddF_x + 2;
                f = f + ddF_x;

                self.pixel(x0 + x, y0 + y, color);
                self.pixel(x0 - x, y0 + y, color);
                self.pixel(x0 + x, y0 - y, color);
                self.pixel(x0 - x, y0 - y, color);
                self.pixel(x0 + y, y0 + x, color);
                self.pixel(x0 - y, y0 + x, color);
                self.pixel(x0 + y, y0 - x, color);
                self.pixel(x0 - y, y0 - x, color);
            }
        },

        // 绘制实心圆
        "fill_circle": fn(x0, y0, r, color) {
            self.fill_rect(x0, y0 - r, 1, 2 * r + 1, color);
            let f = 1 - r;
            let ddF_x = 1;
            let ddF_y = -2 * r;
            let x = 0;
            let y = r;

            while x < y {
                if f >= 0 {
                    y = y - 1;
                    ddF_y = ddF_y + 2;
                    f = f + ddF_y;
                }
                x = x + 1;
                ddF_x = ddF_x + 2;
                f = f + ddF_x;

                self.fill_rect(x0 + x, y0 - y, 1, 2 * y + 1, color);
                self.fill_rect(x0 - x, y0 - y, 1, 2 * y + 1, color);
                self.fill_rect(x0 + y, y0 - x, 1, 2 * x + 1, color);
                self.fill_rect(x0 - y, y0 - x, 1, 2 * x + 1, color);
            }
        },

        // 绘制单个字符
        "char": fn(ch, x, y, color, bg, scale) {
            if scale == nil or scale < 1 { scale = 1; }
            let glyph = ILI9341_FONT5x7[ch];
            if glyph == nil {
                glyph = ILI9341_FONT5x7[ch.upper()];
            }
            if glyph == nil {
                glyph = ILI9341_FONT5x7[" "];
            }

            for (let i = 0; i < 5; i = i + 1) {
                let line_byte = glyph[i];
                for (let j = 0; j < 8; j = j + 1) {
                    let has_dot = (line_byte & (1 << j)) != 0;
                    if has_dot {
                        if scale == 1 {
                            self.pixel(x + i, y + j, color);
                        } else {
                            self.fill_rect(x + i * scale, y + j * scale, scale, scale, color);
                        }
                    } else if bg != nil {
                        if scale == 1 {
                            self.pixel(x + i, y + j, bg);
                        } else {
                            self.fill_rect(x + i * scale, y + j * scale, scale, scale, bg);
                        }
                    }
                }
            }
        },

        // 绘制文本字符串 (支持字符放缩与自定义前/背景色)
        "text": fn(s, x, y, color, bg, scale) {
            if color == nil { color = ILI9341_WHITE; }
            if scale == nil { scale = 1; }
            let cur_x = x;
            let cur_y = y;
            let slen = len(s);

            for (let i = 0; i < slen; i = i + 1) {
                let c = s[i];
                if c == "\n" {
                    cur_x = x;
                    cur_y = cur_y + 8 * scale + 2;
                } else {
                    self.char(c, cur_x, cur_y, color, bg, scale);
                    cur_x = cur_x + 6 * scale; // 5 列点阵 + 1 列字符间距
                }
            }
        },

        // 背光控制 (开/关)
        "backlight": fn(state) {
            if self.bl != nil {
                if state { self.bl.on(); }
                else { self.bl.off(); }
            }
        }
    };

    return dev;
}

// 导出类风格构造器
fn ILI9341(spi, cs_pin, dc_pin, rst_pin, bl_pin) {
    let d = create_ili9341(spi, cs_pin, dc_pin, rst_pin, bl_pin);
    d.init();
    return d;
}
