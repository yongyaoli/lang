// ==============================================================================
// L 语言 ESP32-S3 / ESP32 开发板全功能硬件体检与状态自检诊断脚本
//
// 功能特性:
// 1. 芯片基础信息: 型号、架构、主频、核心数、SRAM、PSRAM、Flash、MAC、IDF 版本
// 2. 存储与固件状态: VFS 文件系统空间、文件列表、OTA l_script 分区有效性与脚本长度
// 3. 网络与无线通信: WiFi 连接状态、本机 IP、信号强度、周围热点自动扫描
// 4. GPIO 引脚全面体检: 芯片引脚能力速查、受限引脚警示、安全 GPIO 当前输入电平扫描
// 5. 外设总线智能探测: 自动扫描 I2C 总线，基于常见芯片地址指纹自动识别传感器与屏幕
// ==============================================================================

import "fs";
import "ota";

// ------------------------------------------------------------------------------
// 辅助格式化函数
// ------------------------------------------------------------------------------

let HEX_CHARS = "0123456789ABCDEF";

// 数值转 2 位十六进制字符串 (如 60 -> "0x3C")
fn to_hex2(val) {
    let v = val;
    if v < 0 { v = 0; }
    let h1 = (v / 16) % 16;
    let h0 = v % 16;
    return "0x" + HEX_CHARS[h1] + HEX_CHARS[h0];
}

// 格式化运行时间 (毫秒 -> 时:分:秒)
fn format_uptime(ms) {
    let total_sec = ms / 1000;
    let sec = total_sec % 60;
    let total_min = total_sec / 60;
    let min = total_min % 60;
    let hours = total_min / 60;

    let sec_str = str(sec);
    if len(sec_str) == 1 { sec_str = "0" + sec_str; }
    let min_str = str(min);
    if len(min_str) == 1 { min_str = "0" + min_str; }

    return str(hours) + "小时 " + min_str + "分 " + sec_str + "秒 (" + str(ms) + " ms)";
}

// 格式化字节大小 (B / KB / MB)
fn format_bytes(bytes_count) {
    let b = floor(bytes_count);
    if b < 1024 {
        return str(b) + " B";
    }
    if b < 1024 * 1024 {
        let kb = floor(b / 1024);
        return str(kb) + " KB";
    }
    let mb = floor(b / (1024 * 1024));
    let rem = floor((b % (1024 * 1024)) * 10 / (1024 * 1024));
    return str(mb) + "." + str(rem) + " MB";
}

print("================================================================================");
print("            L 语言 ESP32-S3 开发板硬件体检与全方位诊断报告");
print("================================================================================");

// ==============================================================================
// 1. 基础信息与芯片规格
// ==============================================================================
print("\n[一、芯片与系统核心规格]");

let info = ESP32.info();
// 若在 PC 离线环境演练，自动切为 ESP32-S3 规格；在真机上自动识别物理芯片
if not info.is_hardware {
    ESP32.set_chip("ESP32-S3");
    info = ESP32.info();
}
let chip_name = info.chip;
let arch_name = info.arch;
let cores = info.cores;
let freq = info.freq_mhz;
let sram_kb = info.sram_kb;
let flash_mb = info.flash_mb;
let psram_kb = info.psram_kb;
let free_heap = ESP32.free_heap();
let mac_addr = ESP32.mac();
let idf_ver = ESP32.version();
let uptime_ms = ESP32.ticks_ms();

print("  • 芯片型号   : " + chip_name + " (" + arch_name + ")");
print("  • CPU 核心数 : " + str(cores) + " 核心 @ " + str(freq) + " MHz");
print("  • 内部 SRAM  : " + str(sram_kb) + " KB");
if psram_kb > 0 {
    print("  • 外部 PSRAM : " + str(psram_kb / 1024) + " MB (" + str(psram_kb) + " KB)");
} else {
    print("  • 外部 PSRAM : 未挂载或未启用 (0 KB)");
}
print("  • 板载 Flash : " + str(flash_mb) + " MB SPI Flash");
print("  • 当前可用堆 : " + format_bytes(free_heap) + " (托管堆动态可用)");
print("  • 硬件 MAC   : " + mac_addr);
print("  • 系统固件   : L 语言 " + idf_ver);
print("  • 开机运行时 : " + format_uptime(uptime_ms));

let wake = ESP32.wake_reason();
if wake != nil {
    print("  • 最近复位因 : " + wake.reason + " (Code: " + str(wake.code) + ")");
}

// ==============================================================================
// 2. 存储介质与固件分区 (Flash & VFS)
// ==============================================================================
print("\n[二、存储系统与固件分区状态]");

// 2.1 VFS 虚拟文件系统
let fs_info = FS.info();
print("  • VFS 文件系统类型 : " + fs_info.fs_type);
print("  • VFS 总容量空间   : " + format_bytes(fs_info.total_bytes));
print("  • VFS 剩余可用空间 : " + format_bytes(fs_info.free_bytes));

let files = FS.list("/flash");
print("  • /flash 根目录文件 : " + str(len(files)) + " 个文件/目录");
for let fi = 0; fi < len(files); fi = fi + 1 {
    let fname = files[fi];
    let full_path = "/flash/" + fname;
    let is_d = FS.isdir(full_path);
    if is_d {
        print("    [目录] " + fname);
    } else {
        let fsz = FS.size(full_path);
        print("    [文件] " + fname + " (" + format_bytes(fsz) + ")");
    }
}

// 2.2 OTA l_script 裸脚本分区
let ota_st = OTA.status();
print("  • OTA 脚本分区名称 : " + ota_st.partition + " (容量: " + format_bytes(ota_st.partition_size) + ")");
print("  • 当前分区状态     : " + ota_st.status);
print("  • 已刷入脚本长度   : " + str(ota_st.script_length) + " 字节");
print("  • 启动校验合法性   : " + (ota_st.valid ? "√ 有效 (Valid)" : "× 空或未初始化"));


// ==============================================================================
// 3. 网络与无线通信检测 (WiFi & Web OTA)
// ==============================================================================
print("\n[三、WiFi 无线网络与通信诊断]");

let is_wifi = wifi_is_connected();
if is_wifi {
    let cur_ssid = wifi_ssid();
    let cur_ip = wifi_ip();
    print("  • WiFi 状态 : √ 已连接至局域网");
    print("  • 连接热点  : " + str(cur_ssid));
    print("  • 本机 IP   : " + str(cur_ip));
    print("  • Web OTA   : 访问 http://" + str(cur_ip) + ":8080/ 进行无线更新");
} else {
    print("  • WiFi 状态 : × 当前未连接网络");
    print("  • 提示      : 可在脚本中执行 wifi_connect(\"热点名\", \"密码\") 连接");
    print("  • 正在执行周围 2.4GHz WiFi 热点环境扫描 (请稍候)...");

    let ap_list = wifi_scan();
    if ap_list == nil or len(ap_list) == 0 {
        print("    (未扫描到周围热点或天线未就绪)");
    } else {
        print("    发现 " + str(len(ap_list)) + " 个周围可用 WiFi 热点:");
        let show_count = len(ap_list);
        if show_count > 6 { show_count = 6; } // 最多列出前 6 个
        for let ai = 0; ai < show_count; ai = ai + 1 {
            let ap = ap_list[ai];
            print("    [" + str(ai + 1) + "] SSID: " + ap.ssid + " | 信号: " + str(ap.rssi) + " dBm | 信道: " + str(ap.channel));
        }
        if len(ap_list) > show_count {
            print("    ... 另有 " + str(len(ap_list) - show_count) + " 个弱信号热点已省略");
        }
    }
}


// ==============================================================================
// 4. GPIO 引脚能力与安全护栏
// ==============================================================================
print("\n[四、GPIO 引脚布局与当前输入电平检测]");

let pin_db = ESP32.pins();
let usable_pins = pin_db.usable_pins;
let flash_pins = pin_db.flash_pins;
let strapping_pins = pin_db.strapping_pins;
let default_i2c = pin_db.default_i2c;
let default_spi = pin_db.default_spi;
let default_uart = pin_db.default_uart;

print("  • 默认总线映射:");
print("    - 默认 I2C  : SDA = GPIO " + str(default_i2c.sda) + ", SCL = GPIO " + str(default_i2c.scl));
print("    - 默认 SPI  : SCK = GPIO " + str(default_spi.sck) + ", MOSI = GPIO " + str(default_spi.mosi) + ", MISO = GPIO " + str(default_spi.miso));
print("    - 默认 UART : TX = GPIO " + str(default_uart.tx) + ", RX = GPIO " + str(default_uart.rx));

print("  • 硬件安全防护护栏 (禁止占用):");
print("    - 内部 Flash/PSRAM : " + str(flash_pins) + " (硬件严禁读写)");
print("    - 启动 Strapping  : " + str(strapping_pins) + " (上电时外部电平决定启动模式)");

if chip_name == "ESP32-S3" {
    print("    - 原生 USB/JTAG   : GPIO 19 (D-), GPIO 20 (D+) (双 Type-C 板原生 USB 使用)");
}

// 快速抽检几个最常见且安全的通用 GPIO 当前输入电平
print("  • 常见测试 GPIO 输入电平扫描:");
let probe_pins = [];
if chip_name == "ESP32-S3" {
    // S3 常用的安全通用引脚
    probe_pins = [1, 2, 4, 5, 6, 7, 8, 9, 10, 14, 21, 38, 47, 48];
} else {
    // 经典 ESP32
    probe_pins = [2, 4, 5, 12, 13, 14, 15, 18, 19, 21, 22, 23];
}

let level_report = "    ";
let count_probed = 0;
for let pi = 0; pi < len(probe_pins); pi = pi + 1 {
    let p_num = probe_pins[pi];
    try {
        let p_obj = Pin(p_num, "in");
        let val = p_obj.value();
        level_report = level_report + "IO" + str(p_num) + ":" + (val ? "H" : "L") + " ";
        count_probed = count_probed + 1;
        if count_probed % 7 == 0 {
            level_report = level_report + "\n    ";
        }
    } catch (e) {
        // 某些引脚若被系统独占则静默跳过
    }
}
print(level_report);
print("    (说明: H=高电平 3.3V, L=低电平 0V)");


// ==============================================================================
// 5. 外设总线智能探测 (I2C 传感器与显示屏识别)
// ==============================================================================
print("\n[五、外设总线扫描与设备指纹智能识别]");

// 建立常见 I2C 外设地址数据库
let I2C_KNOWN_DEVICES = {
    "0x23": "BH1750 环境光照强敏度传感器",
    "0x27": "PCF8574 (LCD1602 / I2C 16键扩展芯片)",
    "0x38": "AHT10 / AHT20 / AHT21 温湿度传感器",
    "0x39": "APDS-9960 手势与接近光传感器",
    "0x3C": "SSD1306 / SH1106 0.96寸 OLED 显示屏 (128x64)",
    "0x3D": "SSD1306 OLED 显示屏 (副地址)",
    "0x3F": "PCF8574A (LCD1602 背板)",
    "0x40": "INA219 直流电压/电流/功率高精度采样计",
    "0x44": "SHT30 / SHT31 高精度工业级温湿度传感器",
    "0x45": "SHT30 / SHT31 (副地址) 温湿度传感器",
    "0x48": "ADS1115 / PCF8591 16位高精度模数转换器 (ADC)",
    "0x50": "AT24C02 / AT24C04 / AT24C32 EEPROM 存储器",
    "0x57": "AT24C32 (RTC 模块集成 EEPROM)",
    "0x68": "MPU6050 六轴陀螺仪加速度计 或 DS3231/DS1307 实时时钟 RTC",
    "0x76": "BMP280 / BME280 / MS5611 气压高度温湿度计 (默认地址)",
    "0x77": "BMP280 / BME280 (副地址 0x77)"
};

// 待扫描的 I2C 引脚候选组
let i2c_pin_candidates = [];
// 候选 1: 芯片默认 SDA, SCL
i2c_pin_candidates.push({"sda": default_i2c.sda, "scl": default_i2c.scl, "desc": "芯片默认引脚"});

// 若为 ESP32-S3，常有使用 (SDA=4, SCL=5) 或 (SDA=1, SCL=2) 的扩展板
if chip_name == "ESP32-S3" {
    if default_i2c.sda != 4 {
        i2c_pin_candidates.push({"sda": 4, "scl": 5, "desc": "备选通道 A"});
    }
    if default_i2c.sda != 1 {
        i2c_pin_candidates.push({"sda": 1, "scl": 2, "desc": "备选通道 B"});
    }
}

let total_devices_found = 0;

for let ci = 0; ci < len(i2c_pin_candidates); ci = ci + 1 {
    let cand = i2c_pin_candidates[ci];
    let sda_pin = cand.sda;
    let scl_pin = cand.scl;
    let desc = cand.desc;

    try {
        let i2c_bus = I2C(0, sda_pin, scl_pin);
        let dev_addrs = i2c_bus.scan();

        if len(dev_addrs) > 0 {
            print("  √ 在 I2C 总线 [SDA: GPIO " + str(sda_pin) + ", SCL: GPIO " + str(scl_pin) + "] (" + desc + ") 探测到设备:");
            for let di = 0; di < len(dev_addrs); di = di + 1 {
                let addr_int = dev_addrs[di];
                let hex_str = to_hex2(addr_int);
                let dev_name = I2C_KNOWN_DEVICES[hex_str];
                if dev_name == nil {
                    dev_name = "未知或专用 I2C 从机设备";
                }
                print("    - 地址: " + hex_str + " (十进制 " + str(addr_int) + ") --> " + dev_name);
                total_devices_found = total_devices_found + 1;
            }
        }
    } catch (e) {
        // 总线无法建立或引脚被其他模块占用时忽略
    }
}

if total_devices_found == 0 {
    print("  • I2C 扫描结果: 当前未探测到任何响应的 I2C 外设。");
    print("    [排查小贴士]");
    print("    1. 请确认外设的 VCC (3.3V) 与 GND 供电稳固；");
    print("    2. ESP32-S3 默认 I2C 为 SDA=GPIO " + str(default_i2c.sda) + ", SCL=GPIO " + str(default_i2c.scl) + "；");
    print("    3. 若外设连接到了其他 GPIO，可在代码中使用 I2C(0, 自定义SDA, 自定义SCL)；");
    print("    4. 确保 SDA 与 SCL 信号线上具有 4.7kΩ~10kΩ 上拉电阻。");
}


// ==============================================================================
// 6. 模数转换与板载模拟量检测 (ADC)
// ==============================================================================
print("\n[六、模拟量通道快速采样 (ADC)]");
let adc_pin = 1; // ESP32-S3 GPIO 1 为 ADC1 通道 0
try {
    let adc_obj = ADC(adc_pin);
    let raw_val = adc_obj.read();
    let mv_val = adc_obj.read_mv();
    print("  • GPIO " + str(adc_pin) + " ADC1 读数 : 原始值=" + str(raw_val) + " (12位 0~4095), 采样电压=" + str(mv_val) + " mV");
} catch (e) {
    print("  • ADC 状态 : 当前通道未激活或由数字功能占用");
}

print("\n================================================================================");
print("                  ★★★ 开发板硬件体检诊断完成 ★★★");
print("================================================================================");
