/* 标准库: ESP32 基础功能类与硬件外设控制 (ESP32 / Pin / PWM / ADC / I2C / SPI / UART / bytes)
 * 所有底层平台操作均通过 l_hal.h (stdlib/hal.c) 抽象层完成, 实现解释器核心与硬件驱动彻底解耦。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../interp.h"
#include "../l_hal.h"
#include "../version.h"

#if L_WITH_STDLIB_HW

/* 事件派发(M7): 把队列中的中断/定时器事件派发到脚本回调, 定义于本文件第 11 节 */
void hw_poll_events(Interp *in);

/* M7 新增方法的前置声明(实现见本文件第 11 节) */
static Value m_pin_irq(Interp *in, const Node *at, Value self, int argc, Value *argv);
static Value m_pin_write_sim(Interp *in, const Node *at, Value self, int argc, Value *argv);
static Value m_esp_deepsleep(Interp *in, const Node *at, Value self, int argc, Value *argv);
static Value m_esp_wake_reason(Interp *in, const Node *at, Value self, int argc, Value *argv);
static Value m_esp_wdt_enable(Interp *in, const Node *at, Value self, int argc, Value *argv);
static Value m_esp_wdt_feed(Interp *in, const Node *at, Value self, int argc, Value *argv);
static Value m_esp_wdt_disable(Interp *in, const Node *at, Value self, int argc, Value *argv);
static Value f_timer(Interp *in, const Node *at, int argc, Value *argv);

/* ================= 1. ESP32 芯片规格与引脚数据库 ================= */

typedef enum {
    CHIP_ESP32 = 0,
    CHIP_ESP32_S3,
    CHIP_ESP32_C3,
    CHIP_ESP32_C6
} ChipModel;

typedef struct {
    const char *name;
    const char *arch;
    int cores;
    int default_mhz;
    int sram_kb;
    int flash_mb;
    int psram_kb;
    int max_pin;
    int default_sda, default_scl;
    int default_sck, default_mosi, default_miso;
    int default_tx, default_rx;
} ChipSpec;

static const ChipSpec chip_specs[] = {
    {"ESP32",    "Xtensa LX6", 2, 240, 320, 4, 0,    39, 21, 22, 18, 23, 19, 17, 16},
    {"ESP32-S3", "Xtensa LX7", 2, 240, 512, 8, 2048, 48,  8,  9, 12, 11, 13, 17, 18},
    {"ESP32-C3", "RISC-V",     1, 160, 400, 4, 0,    21,  8,  9,  6,  7,  2, 21, 20},
    {"ESP32-C6", "RISC-V",     1, 160, 512, 4, 0,    30,  6,  7, 19, 18, 20, 16, 17},
};

static ChipModel cur_chip = CHIP_ESP32;

/* 查询指定芯片下某引脚的硬件属性 */
typedef struct {
    int valid;
    int input_only;
    int flash_reserved;
    int strapping;
    const char *adc;
    const char *note;
} PinCap;

static PinCap query_pin_cap(ChipModel chip, int pin) {
    PinCap c = {0, 0, 0, 0, NULL, "通用 GPIO"};
    const ChipSpec *sp = &chip_specs[chip];
    if (pin < 0 || pin > sp->max_pin) {
        c.note = "超出芯片引脚范围";
        return c;
    }
    if (chip == CHIP_ESP32) {
        /* 经典 ESP32: GPIO 20, 24, 28..31 不存在 */
        if (pin == 20 || pin == 24 || (pin >= 28 && pin <= 31)) {
            c.note = "芯片未引出该编号";
            return c;
        }
        c.valid = 1;
        if (pin >= 6 && pin <= 11) {
            c.flash_reserved = 1;
            c.note = "内部 SPI Flash 占用(禁止用作 GPIO)";
        } else if (pin >= 34 && pin <= 39) {
            c.input_only = 1;
            c.note = "仅输入引脚(无内部上下拉, 不可输出/PWM)";
        } else if (pin == 0 || pin == 2 || pin == 12 || pin == 15) {
            c.strapping = 1;
            c.note = (pin == 2) ? "板载 LED 常用 / 启动 Strapping 引脚" : "启动 Strapping 引脚(上电电平影响启动模式)";
        } else if (pin == 1 || pin == 3) {
            c.note = "UART0 串口引脚(REPL/日志占用)";
        }
        if (pin >= 32 && pin <= 39) c.adc = "ADC1";
        else if (pin == 0 || pin == 2 || pin == 4 || (pin >= 12 && pin <= 15) || (pin >= 25 && pin <= 27)) c.adc = "ADC2";
    } else if (chip == CHIP_ESP32_S3) {
        if (pin >= 22 && pin <= 25) { c.note = "芯片未引出该编号"; return c; }
        c.valid = 1;
        if (pin >= 26 && pin <= 32) {
            c.flash_reserved = 1;
            c.note = "内部 Flash/PSRAM 占用(禁止用作 GPIO)";
        } else if (pin == 0 || pin == 3 || pin == 45 || pin == 46) {
            c.strapping = 1;
            c.note = "启动 Strapping 引脚";
        } else if (pin == 19 || pin == 20) {
            c.note = "原生 USB D-/D+ 引脚";
        }
        if (pin >= 1 && pin <= 10) c.adc = "ADC1";
        else if (pin >= 11 && pin <= 20) c.adc = "ADC2";
    } else if (chip == CHIP_ESP32_C3) {
        c.valid = 1;
        if (pin >= 11 && pin <= 17) {
            c.flash_reserved = 1;
            c.note = "内部 SPI Flash 占用(禁止用作 GPIO)";
        } else if (pin == 2 || pin == 8 || pin == 9) {
            c.strapping = 1;
            c.note = (pin == 8) ? "板载 RGB/LED 常用 / Strapping 引脚" : "启动 Strapping 引脚";
        } else if (pin == 18 || pin == 19) {
            c.note = "原生 USB D-/D+ 引脚";
        }
        if (pin >= 0 && pin <= 4) c.adc = "ADC1";
        else if (pin == 5) c.adc = "ADC2";
    } else { /* ESP32-C6 */
        c.valid = 1;
        if (pin >= 24 && pin <= 29) {
            c.flash_reserved = 1;
            c.note = "内部 SPI Flash 占用(禁止用作 GPIO)";
        } else if (pin == 8 || pin == 9 || pin == 15) {
            c.strapping = 1;
            c.note = "启动 Strapping 引脚";
        }
        if (pin >= 0 && pin <= 6) c.adc = "ADC1";
    }
    return c;
}

static void check_pin_usable(Interp *in, const Node *at, const char *who, int pin, int need_output, int need_pull) {
    const ChipSpec *sp = &chip_specs[cur_chip];
    if (pin < 0 || pin > sp->max_pin) {
        rt_error(in, at, "%s 引脚号 %d 超出 %s 范围 (0..%d)", who, pin, sp->name, sp->max_pin);
    }
    PinCap cap = query_pin_cap(cur_chip, pin);
    if (!cap.valid) {
        rt_error(in, at, "%s 引脚 GPIO %d 在 %s 上不存在 (%s)", who, pin, sp->name, cap.note);
    }
    if (cap.flash_reserved) {
        rt_error(in, at, "%s 引脚 GPIO %d 在 %s 上被内部 Flash 占用, 禁止操作", who, pin, sp->name);
    }
    if (cap.input_only && (need_output || need_pull)) {
        rt_error(in, at, "%s 引脚 GPIO %d 在 %s 上是仅输入引脚(GPIO 34..39), 不支持输出或内部上下拉", who, pin, sp->name);
    }
}

/* Map 辅助写入函数 (保证 GC 根安全) */
static void map_put_int(Interp *in, Map *m, const char *k, int32_t v) {
    map_set(m, str_new(in, k, strlen(k)), val_int(v));
}

static void map_put_bool(Interp *in, Map *m, const char *k, int v) {
    map_set(m, str_new(in, k, strlen(k)), val_bool(v != 0));
}

static void map_put_cstr(Interp *in, Map *m, const char *k, const char *s) {
    if (!s) {
        map_set(m, str_new(in, k, strlen(k)), val_nil());
        return;
    }
    Value sv = val_str(str_new(in, s, strlen(s)));
    gc_push_root(in, sv);
    map_set(m, str_new(in, k, strlen(k)), sv);
    gc_pop_roots(in, 1);
}

static void map_put_val(Interp *in, Map *m, const char *k, Value v) {
    gc_push_root(in, v);
    map_set(m, str_new(in, k, strlen(k)), v);
    gc_pop_roots(in, 1);
}

/* 提取 bytes/array/string 参数为连续字节缓冲区(写入调用者提供的临时缓冲或返回只读指针) */
static const uint8_t *extract_byte_buf(Interp *in, const Node *at, const char *fn,
                                       Value v, uint8_t *tmp, size_t tmp_cap, size_t *out_len) {
    if (v.type == V_BYTES) {
        *out_len = v.as.bytes ? v.as.bytes->len : 0;
        return v.as.bytes ? v.as.bytes->data : tmp;
    }
    if (v.type == V_STR) {
        *out_len = v.as.str ? v.as.str->len : 0;
        return (const uint8_t *)val_get_cstr(v);
    }
    if (v.type == V_ARRAY) {
        Array *a = v.as.array;
        if ((size_t)a->count > tmp_cap)
            rt_error(in, at, "%s 数据数组过长(上限 %zu)", fn, tmp_cap);
        for (int i = 0; i < a->count; i++) {
            int b = int_arg(in, at, fn, a->items, i);
            if (b < 0 || b > 255) rt_error(in, at, "%s 字节值必须在 0..255, 实际是 %d", fn, b);
            tmp[i] = (uint8_t)b;
        }
        *out_len = (size_t)a->count;
        return tmp;
    }
    if (v.type == V_INT) {
        int b = v.as.i;
        if (b < 0 || b > 255) rt_error(in, at, "%s 字节值必须在 0..255, 实际是 %d", fn, b);
        tmp[0] = (uint8_t)b;
        *out_len = 1;
        return tmp;
    }
    rt_error(in, at, "%s 需要 bytes、数组、字符串或单字节整数, 实际是 %s", fn, type_name(v));
    return NULL;
}

/* ================= 2. ESP32 基础功能类 ================= */

static Value m_esp_chip(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)self; (void)argc; (void)argv;
    /* 真机优先返回 esp_chip_info() 读到的实际芯片型号 (不依赖脚本/固件端的 set_chip),
     * 模拟器仍返回 set_chip() 设置的模拟型号 */
    const char *name = l_hal_is_hardware() ? l_hal_chip_name() : chip_specs[cur_chip].name;
    return val_str(str_new(in, name, strlen(name)));
}

static Value m_esp_set_chip(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "ESP32.set_chip", argv, 0, V_STR, "string");
    const char *s = val_get_cstr(argv[0]);
    for (int i = 0; i < (int)(sizeof chip_specs / sizeof chip_specs[0]); i++) {
        if (strcmp(s, chip_specs[i].name) == 0) {
            cur_chip = (ChipModel)i;
            l_hal_set_cpu_mhz(chip_specs[i].default_mhz);
            return self;
        }
    }
    rt_error(in, at, "ESP32.set_chip() 不支持的芯片型号 \"%s\" (可选: \"ESP32\", \"ESP32-S3\", \"ESP32-C3\", \"ESP32-C6\")", s);
    return self;
}

static Value m_esp_version(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)self; (void)argc; (void)argv;
    const char *v = l_hal_idf_version();
    return val_str(str_new(in, v, strlen(v)));
}

static Value m_esp_free_heap(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)self; (void)argc; (void)argv;
    return val_int(l_hal_free_heap(in->bytes_allocated, chip_specs[cur_chip].sram_kb));
}

static Value m_esp_freq(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    if (argc == 0) return val_int(l_hal_get_cpu_mhz());
    int mhz = int_arg(in, at, "ESP32.freq", argv, 0);
    if (mhz != 40 && mhz != 80 && mhz != 160 && mhz != 240)
        rt_error(in, at, "ESP32.freq() 频率仅支持 40 / 80 / 160 / 240 MHz, 实际是 %d", mhz);
    l_hal_set_cpu_mhz(mhz);
    return val_int(mhz);
}

static Value m_esp_mac(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)self; (void)argc; (void)argv;
    uint8_t mac[6] = {0};
    l_hal_get_mac(mac, (int)cur_chip);
    char buf[32];
    snprintf(buf, sizeof buf, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return val_str(str_new(in, buf, strlen(buf)));
}

static Value m_esp_ticks_ms(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)self; (void)argc; (void)argv;
    hw_poll_events(in);   /* ticks_ms 是事件循环的天然心跳点 */
    return val_int((int32_t)l_hal_ticks_ms());
}

static Value m_esp_delay_ms(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    int ms = int_arg(in, at, "ESP32.delay_ms", argv, 0);
    if (ms < 0) rt_error(in, at, "delay_ms() 时间不能为负数");
    /* 分片睡眠 + 分片派发: 延时期间定时器/中断事件也能得到响应(协作式事件循环) */
    int32_t left = ms;
    while (left > 0) {
        int32_t step = (left > 20) ? 20 : left;
        l_hal_delay_ms((uint32_t)step);
        left -= step;
        hw_poll_events(in);
    }
    if (ms == 0) hw_poll_events(in);
    return self;
}

static Value m_esp_info(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    const ChipSpec *sp = &chip_specs[cur_chip];
    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);
    map_put_cstr(in, m, "chip", sp->name);
    map_put_cstr(in, m, "model", sp->name);
    map_put_cstr(in, m, "arch", sp->arch);
    map_put_int(in, m, "cores", sp->cores);
    map_put_int(in, m, "freq_mhz", l_hal_get_cpu_mhz());
    map_put_int(in, m, "sram_kb", sp->sram_kb);
    map_put_int(in, m, "flash_mb", sp->flash_mb);
    map_put_int(in, m, "psram_kb", sp->psram_kb);
    map_put_int(in, m, "max_pin", sp->max_pin);
    map_put_int(in, m, "gpio_count", sp->max_pin + 1);
    map_put_val(in, m, "free_heap", m_esp_free_heap(in, at, self, argc, argv));
    map_put_val(in, m, "idf", m_esp_version(in, at, self, argc, argv));
    map_put_val(in, m, "mac", m_esp_mac(in, at, self, argc, argv));
    map_put_bool(in, m, "is_hardware", l_hal_is_hardware());
    int wifi_conn = l_hal_wifi_is_connected();
    map_put_bool(in, m, "wifi_connected", wifi_conn);
    char ip_buf[32] = {0};
    char ssid_buf[64] = {0};
    if (wifi_conn && l_hal_wifi_get_ip(ip_buf, sizeof ip_buf) == 0) {
        map_put_cstr(in, m, "ip", ip_buf);
        map_put_cstr(in, m, "wifi_ip", ip_buf);
    } else {
        map_put_cstr(in, m, "ip", NULL);
        map_put_cstr(in, m, "wifi_ip", NULL);
    }
    if (wifi_conn && l_hal_wifi_get_ssid(ssid_buf, sizeof ssid_buf) == 0 && ssid_buf[0]) {
        map_put_cstr(in, m, "ssid", ssid_buf);
        map_put_cstr(in, m, "wifi_ssid", ssid_buf);
    } else {
        map_put_cstr(in, m, "ssid", NULL);
        map_put_cstr(in, m, "wifi_ssid", NULL);
    }
    gc_pop_roots(in, 1);
    return mv;
}

static Value m_esp_status(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)self; (void)argc; (void)argv;
    const ChipSpec *sp = &chip_specs[cur_chip];
    char mac[32] = {0};
    l_hal_wifi_get_mac(mac, sizeof mac);
    int connected = l_hal_wifi_is_connected();
    char ip[32] = {0};
    char ssid[64] = {0};
    if (connected) {
        l_hal_wifi_get_ip(ip, sizeof ip);
        l_hal_wifi_get_ssid(ssid, sizeof ssid);
    }
    LScriptHeader hdr;
    size_t part_sz = 0;
    LFlashStatus st = l_hal_flash_info(&hdr, &part_sz);
    int is_hw = l_hal_is_hardware();

    printf("====================================================\n");
    printf("  ESP32 系统状态与网络信息看板\n");
    printf("====================================================\n");
    printf("  [运行环境与硬件规格]\n");
    if (is_hw) {
        printf("    运行平台:        真实 ESP32 物理硬件\n");
        printf("    芯片型号:        %s (%s)\n", sp->name, sp->arch);
        printf("    CPU 核心/主频:   %d 核, %d MHz\n", sp->cores, l_hal_get_cpu_mhz());
        printf("    SRAM / 堆内存:   %d KB / 剩余 %d 字节\n", sp->sram_kb, (int)l_hal_free_heap(0, 0));
        printf("    固件 / IDF:      L %s (%s)\n", L_VERSION, l_hal_idf_version());
    } else {
        printf("    运行平台:        PC 宿主模拟环境 (当前未连接物理 ESP32)\n");
        printf("    模拟芯片:        %s (可在软件中模拟引脚、外设与网络)\n", sp->name);
        printf("    CPU / 堆模拟:    %d 核, %d MHz / %d KB\n", sp->cores, l_hal_get_cpu_mhz(), sp->sram_kb);
        printf("    固件 / 模拟层:   L %s (%s)\n", L_VERSION, l_hal_idf_version());
        printf("    [提示] 如需连接物理开发板: 请插入 USB，运行 ports() 查看串口，使用 connect(\"COMx\") 进入板载终端\n");
    }
    printf("\n  [无线网络 (WiFi)]\n");
    printf("    WiFi 名称(SSID): %s\n", (connected && ssid[0]) ? ssid : "未连接 (None)");
    printf("    连接状态:        %s\n", connected ? "已连接 (Connected)" : "未连接 (Disconnected)");
    printf("    局域网 IP:       %s\n", (connected && ip[0]) ? ip : "-");
    printf("    物理 MAC:        %s\n", mac[0] ? mac : "00:00:00:00:00:00");
    printf("\n  [裸 Flash 存储 (l_script)]\n");
    printf("    分区容量:        %d KB\n", (int)(part_sz / 1024));
    printf("    脚本状态:        %s\n", st == L_FLASH_OK ? "有效 (CRC32 校验通过)" : (st == L_FLASH_EMPTY ? "空 (未烧录脚本)" : "损坏/错误"));
    if (st == L_FLASH_OK) {
        printf("    有效脚本长度:    %u 字节 (CRC32=0x%08X)\n", (unsigned)hdr.length, (unsigned)hdr.crc32);
        printf("    上电自启标志:    %s\n", (hdr.flags & 1u) ? "已开启" : "未开启");
    }
    printf("====================================================\n");
    return val_nil();
}

static Value m_esp_pin_info(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self; (void)argc;
    int pin = int_arg(in, at, "ESP32.pin_info", argv, 0);
    PinCap cap = query_pin_cap(cur_chip, pin);
    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);
    char name_buf[16];
    snprintf(name_buf, sizeof name_buf, "GPIO%d", pin);
    int usable = cap.valid && !cap.flash_reserved;
    int can_out = usable && !cap.input_only;
    map_put_int(in, m, "pin", pin);
    map_put_cstr(in, m, "name", name_buf);
    map_put_bool(in, m, "valid", cap.valid);
    map_put_bool(in, m, "safe", usable);
    map_put_bool(in, m, "usable", usable);
    map_put_bool(in, m, "in", usable);
    map_put_bool(in, m, "out", can_out);
    const char *mstr = (pin >= 0 && pin <= L_MAX_GPIO_PIN) ? l_hal_gpio_mode_name(l_hal_gpio_get_mode(pin)) : "invalid";
    map_put_cstr(in, m, "mode", mstr);
    map_put_int(in, m, "value", (pin >= 0 && pin <= L_MAX_GPIO_PIN) ? l_hal_gpio_read(pin) : 0);
    map_put_bool(in, m, "input_only", cap.input_only);
    map_put_bool(in, m, "flash", cap.flash_reserved);
    map_put_bool(in, m, "flash_reserved", cap.flash_reserved);
    map_put_bool(in, m, "strapping", cap.strapping);
    map_put_bool(in, m, "pwm", can_out);
    map_put_cstr(in, m, "adc", cap.adc);
    map_put_cstr(in, m, "note", cap.note);
    gc_pop_roots(in, 1);
    return mv;
}

static Value m_esp_pins(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)self; (void)argc; (void)argv;
    const ChipSpec *sp = &chip_specs[cur_chip];
    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);

    Array *safe_pins = array_new(in);
    map_put_val(in, m, "usable_pins", val_array(safe_pins));
    Array *in_only = array_new(in);
    map_put_val(in, m, "input_only_pins", val_array(in_only));
    Array *flash_pins = array_new(in);
    map_put_val(in, m, "flash_pins", val_array(flash_pins));
    Array *strap_pins = array_new(in);
    map_put_val(in, m, "strapping_pins", val_array(strap_pins));

    for (int p = 0; p <= sp->max_pin; p++) {
        PinCap c = query_pin_cap(cur_chip, p);
        if (!c.valid) continue;
        if (c.flash_reserved) array_push(flash_pins, val_int(p));
        else {
            array_push(safe_pins, val_int(p));
            if (c.input_only) array_push(in_only, val_int(p));
            if (c.strapping) array_push(strap_pins, val_int(p));
        }
    }

    map_put_cstr(in, m, "chip", sp->name);
    map_put_cstr(in, m, "model", sp->name);

    Map *i2c_m = map_new(in);
    map_put_val(in, m, "default_i2c", val_map(i2c_m));
    map_put_int(in, i2c_m, "sda", sp->default_sda);
    map_put_int(in, i2c_m, "scl", sp->default_scl);

    Map *spi_m = map_new(in);
    map_put_val(in, m, "default_spi", val_map(spi_m));
    map_put_int(in, spi_m, "sck", sp->default_sck);
    map_put_int(in, spi_m, "mosi", sp->default_mosi);
    map_put_int(in, spi_m, "miso", sp->default_miso);

    Map *uart_m = map_new(in);
    map_put_val(in, m, "default_uart", val_map(uart_m));
    map_put_int(in, uart_m, "tx", sp->default_tx);
    map_put_int(in, uart_m, "rx", sp->default_rx);

    gc_pop_roots(in, 1);
    return mv;
}

static Value m_esp_pinout(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)self; (void)argc; (void)argv;
    const ChipSpec *sp = &chip_specs[cur_chip];
    printf("=== %s (%s, %d核 %dMHz, SRAM %dKB) 引脚速查表 ===\n",
           sp->name, sp->arch, sp->cores, l_hal_get_cpu_mhz(), sp->sram_kb);
    printf("GPIO  状态/模式     电平  ADC    功能与限制说明\n");
    printf("----  ------------  ----  -----  ------------------------------\n");
    for (int p = 0; p <= sp->max_pin; p++) {
        PinCap c = query_pin_cap(cur_chip, p);
        if (!c.valid) continue;
        printf("  %2d  %-12s   %d    %-5s  %s\n",
               p, l_hal_gpio_mode_name(l_hal_gpio_get_mode(p)), l_hal_gpio_read(p),
               c.adc ? c.adc : "-", c.note);
    }
    return val_nil();
}

/* 裸 Flash 脚本分区 (l_script) 方法: 支撑无文件系统的 ESP32 脚本存储与校验 */
static Value m_esp_crc32(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self; (void)argc;
    uint8_t tmp[256];
    size_t len = 0;
    const uint8_t *buf = extract_byte_buf(in, at, "ESP32.crc32", argv[0], tmp, sizeof tmp, &len);
    uint32_t crc = l_hal_crc32(buf, len);
    char hex[16];
    snprintf(hex, sizeof hex, "0x%08X", (unsigned)crc);
    return val_str(str_new(in, hex, strlen(hex)));
}

static Value m_esp_flash_write(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    need_arg(in, at, "ESP32.flash_write", argv, 0, V_STR, "string");
    uint32_t flags = (argc >= 2) ? (uint32_t)int_arg(in, at, "ESP32.flash_write", argv, 1) : 1u;
    const char *src = val_get_cstr(argv[0]);
    size_t len = argv[0].as.str ? argv[0].as.str->len : 0;
    LFlashStatus st = l_hal_flash_write_script(src, len, flags);
    if (st != L_FLASH_OK) {
        rt_error(in, at, "ESP32.flash_write() 烧录 l_script 分区失败(状态码 %d)", (int)st);
    }
    return val_int((int32_t)len);
}

static Value m_esp_flash_read(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self; (void)argc; (void)argv;
    size_t len = 0;
    LFlashStatus st = L_FLASH_EMPTY;
    const char *ptr = l_hal_flash_read_script(&len, &st);
    if (st == L_FLASH_EMPTY) return val_nil();
    if (st == L_FLASH_ERR_CRC) {
        rt_error(in, at, "ESP32.flash_read() 分区 l_script CRC32 校验失败, 脚本数据已损坏");
    }
    if (st != L_FLASH_OK || !ptr) {
        rt_error(in, at, "ESP32.flash_read() 读取 l_script 分区失败(状态码 %d)", (int)st);
    }
    return val_str(str_new(in, ptr, len));
}

static Value m_esp_flash_erase(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc; (void)argv;
    if (l_hal_flash_erase_script() != L_FLASH_OK) {
        rt_error(in, at, "ESP32.flash_erase() 擦除 l_script 分区失败");
    }
    return self;
}

static Value m_esp_flash_info(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)self; (void)argc; (void)argv;
    LScriptHeader hdr = {0, 0, 0, 0};
    size_t part_sz = 0;
    LFlashStatus st = l_hal_flash_info(&hdr, &part_sz);
    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);
    map_put_cstr(in, m, "partition", L_FLASH_PART_NAME);
    map_put_int(in, m, "size", (int32_t)part_sz);
    map_put_bool(in, m, "valid", st == L_FLASH_OK);
    const char *st_str = (st == L_FLASH_OK) ? "ok"
                       : (st == L_FLASH_EMPTY) ? "empty"
                       : (st == L_FLASH_ERR_CRC) ? "crc_error" : "io_error";
    map_put_cstr(in, m, "status", st_str);
    map_put_int(in, m, "length", (st == L_FLASH_OK) ? (int32_t)hdr.length : 0);
    map_put_int(in, m, "flags", (st == L_FLASH_OK) ? (int32_t)hdr.flags : 0);
    char crc_buf[16];
    snprintf(crc_buf, sizeof crc_buf, "0x%08X", (unsigned)(st == L_FLASH_OK ? hdr.crc32 : 0));
    map_put_cstr(in, m, "crc32", crc_buf);
    gc_pop_roots(in, 1);
    return mv;
}

static Value m_esp_flash_exec(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self; (void)argc; (void)argv;
    size_t len = 0;
    LFlashStatus st = L_FLASH_EMPTY;
    const char *ptr = l_hal_flash_read_script(&len, &st);
    if (st == L_FLASH_EMPTY) {
        rt_error(in, at, "ESP32.flash_exec() 裸 Flash 分区 l_script 为空, 尚未写入脚本");
    }
    if (st == L_FLASH_ERR_CRC) {
        rt_error(in, at, "ESP32.flash_exec() 分区 l_script CRC32 校验失败, 拒绝执行损坏的脚本");
    }
    if (st != L_FLASH_OK || !ptr) {
        rt_error(in, at, "ESP32.flash_exec() 读取 l_script 分区失败(状态码 %d)", (int)st);
    }
    /* 直接在 mmap 映射区或模拟 Flash 区上零拷贝解析并执行。
     * 在全局环境的新子环境中运行: 设备端烧录后热重载、REPL 内重复执行
     * (upload("sim",...) 自动运行 / ESP32.flash_exec()) 时, 上一次运行的
     * 顶层 const 与全局定义不会残留, 语义等同一次全新上电启动。
     * 回调注册表 (__l_irq_handlers 等) 与内置对象都在 in->globals, 不受影响 */
    Env *saved_env = in->env;
    in->env = env_new(in, in->globals);
    Value r = interp_eval_source(in, at, ptr, "<flash:l_script>");
    in->env = saved_env;
    return r;
}

static Value m_esp_config(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)self; (void)argc; (void)argv;
    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);
    map_put_cstr(in, m, "platform", L_PLATFORM_ESP_IDF ? "esp-idf" : "host-sim");
    map_put_int(in, m, "gc_init_threshold", (int32_t)L_GC_INIT_THRESHOLD);
    map_put_int(in, m, "max_call_depth", (int32_t)L_MAX_CALL_DEPTH);
    map_put_cstr(in, m, "flash_partition", L_FLASH_PART_NAME);
    map_put_int(in, m, "flash_size", (int32_t)L_FLASH_PART_SIZE);
    Map *mods = map_new(in);
    map_put_val(in, m, "modules", val_map(mods));
    map_put_bool(in, mods, "string", L_WITH_STDLIB_STRING);
    map_put_bool(in, mods, "math",   L_WITH_STDLIB_MATH);
    map_put_bool(in, mods, "time",   L_WITH_STDLIB_TIME);
    map_put_bool(in, mods, "io",     L_WITH_STDLIB_IO);
    map_put_bool(in, mods, "sys",    L_WITH_STDLIB_SYS);
    map_put_bool(in, mods, "hw",     L_WITH_STDLIB_HW);
    gc_pop_roots(in, 1);
    return mv;
}

static Value f_ports(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    char names[32][16];
    int n = l_hal_serial_list_ports(names, 32);
    Array *a = array_new(in);
    Value av = val_array(a);
    gc_push_root(in, av);
    for (int i = 0; i < n; i++) {
        array_push(a, val_str(str_new(in, names[i], strlen(names[i]))));
    }
    gc_pop_roots(in, 1);
    return av;
}

static Value m_esp_ports(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    return f_ports(in, at, argc, argv);
}

static Value f_upload(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "upload", argv, 0, V_STR, "string");
    need_arg(in, at, "upload", argv, 1, V_STR, "string");
    const char *port = val_get_cstr(argv[0]);
    const char *target = val_get_cstr(argv[1]);
    int baud = (argc >= 3) ? int_arg(in, at, "upload", argv, 2) : 115200;

    char *file_src = read_file_text(target);
    const char *src = target;
    size_t len = argv[1].as.str ? argv[1].as.str->len : 0;

    if (file_src) {
        src = file_src;
        len = strlen(file_src);
    } else {
        size_t tlen = strlen(target);
        if (tlen >= 2 && strcmp(target + tlen - 2, ".l") == 0) {
            rt_error(in, at, "upload() 找不到脚本文件: \"%s\"", target);
        }
    }

    char err_buf[256] = {0};
    int rc = l_hal_serial_upload(port, baud, src, len, err_buf, sizeof err_buf);
    if (file_src) free(file_src);
    if (rc < 0) {
        rt_error(in, at, "upload() 烧录失败: %s", err_buf);
    }
    if (rc == 1) {
        /* "sim" 离线模拟烧录模式: 自动触发裸 Flash 分区零拷贝执行 */
        return m_esp_flash_exec(in, at, val_nil(), 0, NULL);
    }
    return val_bool(1);
}

static Value m_esp_upload(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    return f_upload(in, at, argc, argv);
}

static Value f_connect(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "connect", argv, 0, V_STR, "string");
    const char *port = val_get_cstr(argv[0]);
    int baud = 115200;
    const char *code = NULL;

    if (argc >= 2) {
        if (argv[1].type == V_STR) {
            code = val_get_cstr(argv[1]);
            if (argc >= 3) baud = int_arg(in, at, "connect", argv, 2);
        } else {
            baud = int_arg(in, at, "connect", argv, 1);
            if (argc >= 3 && argv[2].type == V_STR) {
                code = val_get_cstr(argv[2]);
            }
        }
    }

    char err_buf[256] = {0};
    if (code) {
        if (l_hal_serial_exec(port, baud, code, err_buf, sizeof err_buf) < 0) {
            rt_error(in, at, "connect() 远程执行失败: %s", err_buf);
        }
    } else {
        if (l_hal_serial_monitor(port, baud, NULL, err_buf, sizeof err_buf) < 0) {
            rt_error(in, at, "connect() 连接失败: %s", err_buf);
        }
    }
    return val_bool(1);
}

static Value m_esp_connect(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    return f_connect(in, at, argc, argv);
}

static const ObjMethod esp32_methods[] = {
    {"chip",        0, 0, m_esp_chip,        "ESP32.chip()",                   "返回芯片型号(\"ESP32\"/\"ESP32-S3\"/\"ESP32-C3\"/\"ESP32-C6\")"},
    {"model",       0, 0, m_esp_chip,        "ESP32.model()",                  "同 chip()"},
    {"set_chip",    1, 1, m_esp_set_chip,    "ESP32.set_chip(型号)",           "切换模拟的芯片型号及对应引脚约束"},
    {"version",     0, 0, m_esp_version,     "ESP32.version()",                "返回 ESP-IDF 框架版本"},
    {"status",      0, 0, m_esp_status,      "ESP32.status()",                 "输出完整的 ESP32 系统状态与 WiFi/IP/Flash 看板"},
    {"info",        0, 0, m_esp_info,        "ESP32.info()",                   "返回芯片完整硬件信息字典(核心数/主频/SRAM/Flash/MAC等)"},
    {"config",      0, 0, m_esp_config,      "ESP32.config()",                 "返回当前 HAL 平台与模块裁剪配置字典"},
    {"pins",        0, 0, m_esp_pins,        "ESP32.pins()",                   "返回可用引脚分类及默认 I2C/SPI/UART 引脚映射"},
    {"pin_info",    1, 1, m_esp_pin_info,    "ESP32.pin_info(引脚号)",         "返回指定引脚的详细硬件能力、限制与当前状态"},
    {"pinout",      0, 0, m_esp_pinout,      "ESP32.pinout()",                 "打印整块芯片的引脚状态与限制速查表"},
    {"free_heap",   0, 0, m_esp_free_heap,   "ESP32.free_heap()",              "返回当前可用堆内存字节数"},
    {"freq",        0, 1, m_esp_freq,        "ESP32.freq([MHz])",              "读取或设置 CPU 主频(40/80/160/240 MHz)"},
    {"mac",         0, 0, m_esp_mac,         "ESP32.mac()",                    "返回设备 MAC 地址字符串"},
    {"ticks_ms",    0, 0, m_esp_ticks_ms,    "ESP32.ticks_ms()",               "返回启动以来的毫秒计数"},
    {"delay_ms",    1, 1, m_esp_delay_ms,    "ESP32.delay_ms(毫秒)",           "毫秒级阻塞延时"},
    {"crc32",       1, 1, m_esp_crc32,       "ESP32.crc32(数据)",              "计算字符串或 bytes 的 CRC32 校验值(返回 0xXXXXXXXX)"},
    {"flash_write", 1, 2, m_esp_flash_write, "ESP32.flash_write(源码,[标志])", "将脚本带 16B 头与 CRC32 写入裸 Flash 分区 l_script"},
    {"flash_read",  0, 0, m_esp_flash_read,  "ESP32.flash_read()",             "校验 CRC32 并从裸 Flash 分区读取脚本(空分区返回 nil)"},
    {"flash_exec",  0, 0, m_esp_flash_exec,  "ESP32.flash_exec()",             "直接零拷贝映射并执行裸 Flash 分区 l_script 中的脚本"},
    {"flash_erase", 0, 0, m_esp_flash_erase, "ESP32.flash_erase()",            "擦除裸 Flash 分区 l_script"},
    {"flash_info",  0, 0, m_esp_flash_info,  "ESP32.flash_info()",             "查询裸 Flash 脚本分区状态 {partition, size, valid, status, length, crc32}"},
    {"ports",       0, 0, m_esp_ports,       "ESP32.ports()",                  "扫描并列出本机可用串口(如 [\"COM3\"])"},
    {"upload",      2, 3, m_esp_upload,      "ESP32.upload(串口,脚本,[波特率])","通过串口(或 \"sim\")将脚本文件烧录到 ESP32 的 l_script 分区"},
    {"connect",     1, 3, m_esp_connect,     "ESP32.connect(串口,[代码或波特率],[代码])", "进入 ESP32 串口交互终端，或直接远程发送执行代码"},
    {"deepsleep",   0, 1, m_esp_deepsleep,  "ESP32.deepsleep([毫秒])",        "进入深度睡眠, 定时唤醒(真机复位重启, PC 模拟立即返回)"},
    {"wake_reason", 0, 0, m_esp_wake_reason,"ESP32.wake_reason()",            "返回唤醒原因字典 {code, reason}"},
    {"watchdog_enable", 1, 1, m_esp_wdt_enable,  "ESP32.watchdog_enable(超时毫秒)", "启用看门狗, 超时未喂狗触发错误(真机为复位)"},
    {"watchdog_feed",   0, 0, m_esp_wdt_feed,    "ESP32.watchdog_feed()",           "喂狗, 重置看门狗计时"},
    {"watchdog_disable",0, 0, m_esp_wdt_disable, "ESP32.watchdog_disable()",        "关闭看门狗"},
};

static void esp32_format(const NativeObj *obj, char *buf, size_t size) {
    (void)obj;
    const ChipSpec *sp = &chip_specs[cur_chip];
    snprintf(buf, size, "<ESP32(%s, %s, %dMHz, SRAM=%dKB)>",
             sp->name, sp->arch, l_hal_get_cpu_mhz(), sp->sram_kb);
}

const ObjClass esp32_class = {
    "ESP32",
    esp32_methods,
    sizeof esp32_methods / sizeof esp32_methods[0],
    esp32_format,
    NULL
};

/* ================= 3. Pin (GPIO) 类 ================= */

static LPinMode parse_pin_mode(Interp *in, const Node *at, const char *s) {
    for (int i = L_PIN_OUT; i <= L_PIN_OPEN_DRAIN; i++) {
        if (strcmp(s, l_hal_gpio_mode_name((LPinMode)i)) == 0) return (LPinMode)i;
    }
    rt_error(in, at, "Pin 不支持的模式 \"%s\" (可选: \"out\", \"in\", \"in_pullup\", \"in_pulldown\", \"open_drain\")", s);
    return L_PIN_OUT;
}

static void hw_gpio_apply_mode(Interp *in, const Node *at, int pin, LPinMode mode) {
    int need_out  = (mode == L_PIN_OUT || mode == L_PIN_OPEN_DRAIN);
    int need_pull = (mode == L_PIN_IN_PULLUP || mode == L_PIN_IN_PULLDOWN);
    check_pin_usable(in, at, "Pin", pin, need_out, need_pull);
    l_hal_gpio_set_mode(pin, mode);
}

static Value m_pin_on(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    l_hal_gpio_write(self.as.obj->fields[0], 1);
    return self;
}

static Value m_pin_off(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    l_hal_gpio_write(self.as.obj->fields[0], 0);
    return self;
}

static Value m_pin_toggle(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    int pin = self.as.obj->fields[0];
    l_hal_gpio_write(pin, !l_hal_gpio_read(pin));
    return self;
}

static Value m_pin_value(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    int pin = self.as.obj->fields[0];
    if (argc == 0) return val_int(l_hal_gpio_read(pin));
    int v = (argv[0].type == V_BOOL) ? argv[0].as.boolean : (int_arg(in, at, "Pin.value", argv, 0) != 0);
    l_hal_gpio_write(pin, v);
    return self;
}

static Value m_pin_mode(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    int pin = self.as.obj->fields[0];
    if (argc == 0) {
        const char *m = l_hal_gpio_mode_name(l_hal_gpio_get_mode(pin));
        return val_str(str_new(in, m, strlen(m)));
    }
    need_arg(in, at, "Pin.mode", argv, 0, V_STR, "string");
    LPinMode mode = parse_pin_mode(in, at, val_get_cstr(argv[0]));
    self.as.obj->fields[1] = (int32_t)mode;
    hw_gpio_apply_mode(in, at, pin, mode);
    return self;
}

static Value m_pin_num(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    return val_int(self.as.obj->fields[0]);
}

static Value m_pin_info(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    int pin = self.as.obj->fields[0];
    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);
    map_put_int(in, m, "pin", pin);
    map_put_cstr(in, m, "mode", l_hal_gpio_mode_name(l_hal_gpio_get_mode(pin)));
    map_put_int(in, m, "value", l_hal_gpio_read(pin));
    gc_pop_roots(in, 1);
    return mv;
}

static const ObjMethod pin_methods[] = {
    {"on",     0, 0, m_pin_on,     "pin.on()",          "输出高电平(1), 返回自身以支持链式调用"},
    {"off",    0, 0, m_pin_off,    "pin.off()",         "输出低电平(0), 返回自身"},
    {"high",   0, 0, m_pin_on,     "pin.high()",        "同 on()"},
    {"low",    0, 0, m_pin_off,    "pin.low()",         "同 off()"},
    {"toggle", 0, 0, m_pin_toggle, "pin.toggle()",      "翻转当前电平(0<->1), 返回自身"},
    {"value",  0, 1, m_pin_value,  "pin.value([电平])", "无参读取电平(0/1); 有参设置电平并返回自身"},
    {"mode",   0, 1, m_pin_mode,   "pin.mode([模式])",  "无参读取模式; 有参设置模式(\"out\"/\"in\"/\"in_pullup\"/\"in_pulldown\"/\"open_drain\")"},
    {"pin",    0, 0, m_pin_num,    "pin.pin()",         "返回引脚编号"},
    {"info",   0, 0, m_pin_info,   "pin.info()",        "返回 {pin, mode, value} 字典"},
    {"irq",    0, 2, m_pin_irq,    "pin.irq(回调, [触发=\"change\"])", "注册电平中断回调(可传 nil 注销), 触发可选 \"rise\"/\"fall\"/\"change\""},
    {"write_sim", 1, 1, m_pin_write_sim, "pin.write_sim(电平)", "PC 模拟: 注入外部电平跳变并触发已注册中断(离线调试用)"},
};

static void pin_format(const NativeObj *obj, char *buf, size_t size) {
    int pin = obj->fields[0];
    if (pin < 0 || pin > L_MAX_GPIO_PIN) pin = 0;
    snprintf(buf, size, "<Pin(%d, mode=\"%s\", value=%d)>",
             pin, l_hal_gpio_mode_name(l_hal_gpio_get_mode(pin)), l_hal_gpio_read(pin));
}

const ObjClass pin_class = {
    "Pin",
    pin_methods,
    sizeof pin_methods / sizeof pin_methods[0],
    pin_format,
    NULL
};

static Value f_pin(Interp *in, const Node *at, int argc, Value *argv) {
    int pin = int_arg(in, at, "Pin", argv, 0);
    LPinMode mode = L_PIN_OUT;
    if (argc >= 2) {
        need_arg(in, at, "Pin", argv, 1, V_STR, "string");
        mode = parse_pin_mode(in, at, val_get_cstr(argv[1]));
    }
    hw_gpio_apply_mode(in, at, pin, mode);
    NativeObj *o = obj_new(in, &pin_class);
    o->fields[0] = pin;
    o->fields[1] = (int32_t)mode;
    if (argc >= 3) {
        int v = (argv[2].type == V_BOOL) ? argv[2].as.boolean : (int_arg(in, at, "Pin", argv, 2) != 0);
        l_hal_gpio_write(pin, v);
    }
    return val_obj(o);
}

/* ================= 4. bytes 定长字节缓冲类型与方法 ================= */

static Value m_bytes_len(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    return val_int((int32_t)(self.as.bytes ? self.as.bytes->len : 0));
}

static Value m_bytes_hex(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    const char *sep = "";
    if (argc == 1) {
        need_arg(in, at, "bytes.hex", argv, 0, V_STR, "string");
        sep = val_get_cstr(argv[0]);
    }
    size_t n = self.as.bytes ? self.as.bytes->len : 0;
    size_t slen = strlen(sep);
    size_t out_len = n ? (n * 2 + (n - 1) * slen) : 0;
    Str *s = str_new(in, NULL, out_len);
    char *p = s->chars;
    static const char hex_chars[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; i++) {
        if (i > 0 && slen > 0) { memcpy(p, sep, slen); p += slen; }
        uint8_t b = self.as.bytes->data[i];
        *p++ = hex_chars[b >> 4];
        *p++ = hex_chars[b & 0x0F];
    }
    *p = '\0';
    return val_str(s);
}

static Value m_bytes_to_str(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    size_t n = self.as.bytes ? self.as.bytes->len : 0;
    return val_str(str_new(in, (const char *)self.as.bytes->data, n));
}

static Value m_bytes_to_array(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    size_t n = self.as.bytes ? self.as.bytes->len : 0;
    Array *a = array_new(in);
    Value av = val_array(a);
    gc_push_root(in, av);
    for (size_t i = 0; i < n; i++) array_push(a, val_int(self.as.bytes->data[i]));
    gc_pop_roots(in, 1);
    return av;
}

static Value m_bytes_fill(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    int v = int_arg(in, at, "bytes.fill", argv, 0);
    if (v < 0 || v > 255) rt_error(in, at, "bytes.fill() 值必须在 0..255 之间, 实际是 %d", v);
    if (self.as.bytes && self.as.bytes->len > 0)
        memset(self.as.bytes->data, v, self.as.bytes->len);
    return self;
}

static Value m_bytes_slice(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    int n = (int)(self.as.bytes ? self.as.bytes->len : 0);
    int s = int_arg(in, at, "bytes.slice", argv, 0);
    int e = (argc >= 2) ? int_arg(in, at, "bytes.slice", argv, 1) : n;
    if (s < 0) s += n;
    if (e < 0) e += n;
    if (s < 0) s = 0;
    if (e > n) e = n;
    if (e < s) e = s;
    return val_bytes(bytes_new(in, (size_t)(e - s), self.as.bytes->data + s));
}

static Value m_bytes_u16_be(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    int off = int_arg(in, at, "bytes.u16_be", argv, 0);
    int n = (int)(self.as.bytes ? self.as.bytes->len : 0);
    if (off < 0 || off + 2 > n) rt_error(in, at, "bytes.u16_be() 偏移 %d 越界(长度 %d)", off, n);
    const uint8_t *d = self.as.bytes->data + off;
    return val_int(((int)d[0] << 8) | d[1]);
}

static Value m_bytes_u16_le(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    int off = int_arg(in, at, "bytes.u16_le", argv, 0);
    int n = (int)(self.as.bytes ? self.as.bytes->len : 0);
    if (off < 0 || off + 2 > n) rt_error(in, at, "bytes.u16_le() 偏移 %d 越界(长度 %d)", off, n);
    const uint8_t *d = self.as.bytes->data + off;
    return val_int(((int)d[1] << 8) | d[0]);
}

static Value m_bytes_set_u16_be(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    int off = int_arg(in, at, "bytes.set_u16_be", argv, 0);
    int val = int_arg(in, at, "bytes.set_u16_be", argv, 1);
    int n = (int)(self.as.bytes ? self.as.bytes->len : 0);
    if (off < 0 || off + 2 > n) rt_error(in, at, "bytes.set_u16_be() 偏移 %d 越界(长度 %d)", off, n);
    self.as.bytes->data[off]     = (uint8_t)((val >> 8) & 0xFF);
    self.as.bytes->data[off + 1] = (uint8_t)(val & 0xFF);
    return self;
}

static Value m_bytes_set_u16_le(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    int off = int_arg(in, at, "bytes.set_u16_le", argv, 0);
    int val = int_arg(in, at, "bytes.set_u16_le", argv, 1);
    int n = (int)(self.as.bytes ? self.as.bytes->len : 0);
    if (off < 0 || off + 2 > n) rt_error(in, at, "bytes.set_u16_le() 偏移 %d 越界(长度 %d)", off, n);
    self.as.bytes->data[off]     = (uint8_t)(val & 0xFF);
    self.as.bytes->data[off + 1] = (uint8_t)((val >> 8) & 0xFF);
    return self;
}

static const ObjMethod bytes_methods[] = {
    {"len",        0, 0, m_bytes_len,        "b.len()",                  "返回字节数"},
    {"hex",        0, 1, m_bytes_hex,        "b.hex([分隔符])",          "转为十六进制字符串, 如 \"4865\" 或 \"48 65\""},
    {"to_str",     0, 0, m_bytes_to_str,     "b.to_str()",               "按原始字节转为字符串"},
    {"to_array",   0, 0, m_bytes_to_array,   "b.to_array()",             "转为 0..255 的整数数组"},
    {"fill",       1, 1, m_bytes_fill,       "b.fill(字节值)",           "用指定字节填充整个缓冲区并返回自身"},
    {"slice",      1, 2, m_bytes_slice,      "b.slice(起, [止])",        "截取子 bytes"},
    {"u16_be",     1, 1, m_bytes_u16_be,     "b.u16_be(偏移)",           "按大端序读取 16 位无符号整数"},
    {"u16_le",     1, 1, m_bytes_u16_le,     "b.u16_le(偏移)",           "按小端序读取 16 位无符号整数"},
    {"set_u16_be", 2, 2, m_bytes_set_u16_be, "b.set_u16_be(偏移, 值)",   "按大端序写入 16 位整数并返回自身"},
    {"set_u16_le", 2, 2, m_bytes_set_u16_le, "b.set_u16_le(偏移, 值)",   "按小端序写入 16 位整数并返回自身"},
};

const ObjClass bytes_class = {
    "bytes",
    bytes_methods,
    sizeof bytes_methods / sizeof bytes_methods[0],
    NULL,
    NULL
};

/* bytes(长度) | bytes([0x01, 0x02]) | bytes("字符串") */
static Value f_bytes(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    if (argv[0].type == V_INT || argv[0].type == V_NUM) {
        int n = int_arg(in, at, "bytes", argv, 0);
        if (n < 0 || n > 1048576) rt_error(in, at, "bytes() 长度无效: %d", n);
        return val_bytes(bytes_new(in, (size_t)n, NULL));
    }
    if (argv[0].type == V_STR) {
        size_t len = argv[0].as.str ? argv[0].as.str->len : 0;
        return val_bytes(bytes_new(in, len, (const uint8_t *)val_get_cstr(argv[0])));
    }
    if (argv[0].type == V_ARRAY) {
        Array *a = argv[0].as.array;
        Bytes *b = bytes_new(in, (size_t)a->count, NULL);
        for (int i = 0; i < a->count; i++) {
            int v = int_arg(in, at, "bytes", a->items, i);
            if (v < 0 || v > 255)
                rt_error(in, at, "bytes() 数组第 %d 项超出 0..255: %d", i, v);
            b->data[i] = (uint8_t)v;
        }
        return val_bytes(b);
    }
    if (argv[0].type == V_BYTES) {
        Bytes *src = argv[0].as.bytes;
        return val_bytes(bytes_new(in, src ? src->len : 0, src ? src->data : NULL));
    }
    rt_error(in, at, "bytes() 参数应为整数长度、数组或字符串, 实际是 %s", type_name(argv[0]));
}

/* ================= 5. PWM 类 (LEDC) ================= */

/* fields[0] = pin, fields[1] = freq_hz, fields[2] = duty (0..1023), fields[3] = active */
static Value m_pwm_freq(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    if (argc == 0) return val_int(self.as.obj->fields[1]);
    int hz = int_arg(in, at, "PWM.freq", argv, 0);
    if (hz <= 0 || hz > 40000000) rt_error(in, at, "PWM.freq() 频率超出范围: %d", hz);
    self.as.obj->fields[1] = hz;
    l_hal_pwm_set_freq(self.as.obj->fields[0], hz);
    return self;
}

static Value m_pwm_duty(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    if (argc == 0) return val_int(self.as.obj->fields[2]);
    int d = int_arg(in, at, "PWM.duty", argv, 0);
    if (d < 0 || d > 1023) rt_error(in, at, "PWM.duty() 占空比应在 0..1023 之间, 实际是 %d", d);
    self.as.obj->fields[2] = d;
    l_hal_pwm_set_duty(self.as.obj->fields[0], d);
    return self;
}

static Value m_pwm_duty_pct(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    if (argc == 0) return val_int((self.as.obj->fields[2] * 100 + 511) / 1023);
    double pct = num_arg(in, at, "PWM.duty_percent", argv, 0);
    if (pct < 0 || pct > 100) rt_error(in, at, "PWM.duty_percent() 百分比应在 0..100, 实际是 %g", pct);
    int d = (int)(pct * 1023.0 / 100.0 + 0.5);
    self.as.obj->fields[2] = d;
    l_hal_pwm_set_duty(self.as.obj->fields[0], d);
    return self;
}

static Value m_pwm_stop(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    self.as.obj->fields[2] = 0;
    self.as.obj->fields[3] = 0;
    l_hal_pwm_stop(self.as.obj->fields[0]);
    return self;
}

static const ObjMethod pwm_methods[] = {
    {"freq",         0, 1, m_pwm_freq,     "pwm.freq([Hz])",            "读取或设置 PWM 频率(Hz), 设置时返回自身"},
    {"duty",         0, 1, m_pwm_duty,     "pwm.duty([0..1023])",       "读取或设置 10 位占空比(0..1023), 设置时返回自身"},
    {"duty_percent", 0, 1, m_pwm_duty_pct, "pwm.duty_percent([0..100])","按百分比读取或设置占空比"},
    {"stop",         0, 0, m_pwm_stop,     "pwm.stop()",                "停止 PWM 输出"},
    {"deinit",       0, 0, m_pwm_stop,     "pwm.deinit()",              "同 stop(), 停止并释放 PWM 通道"},
};

static void pwm_format(const NativeObj *obj, char *buf, size_t size) {
    snprintf(buf, size, "<PWM(pin=%d, freq=%dHz, duty=%d/1023)>",
             (int)obj->fields[0], (int)obj->fields[1], (int)obj->fields[2]);
}

const ObjClass pwm_class = {
    "PWM", pwm_methods, sizeof pwm_methods / sizeof pwm_methods[0], pwm_format, NULL
};

/* PWM(引脚号, [频率=1000], [占空比=0]) */
static Value f_pwm(Interp *in, const Node *at, int argc, Value *argv) {
    int pin = int_arg(in, at, "PWM", argv, 0);
    check_pin_usable(in, at, "PWM", pin, 1, 0);
    int freq = (argc >= 2) ? int_arg(in, at, "PWM", argv, 1) : 1000;
    int duty = (argc >= 3) ? int_arg(in, at, "PWM", argv, 2) : 0;
    if (freq <= 0 || freq > 40000000) rt_error(in, at, "PWM() 频率超出范围: %d", freq);
    if (duty < 0 || duty > 1023) rt_error(in, at, "PWM() 占空比应在 0..1023 之间, 实际是 %d", duty);
    l_hal_pwm_init(pin, freq, duty);
    NativeObj *o = obj_new(in, &pwm_class);
    o->fields[0] = pin;
    o->fields[1] = freq;
    o->fields[2] = duty;
    o->fields[3] = 1;
    return val_obj(o);
}

/* ================= 6. ADC 类 ================= */

/* fields[0] = pin, fields[1] = full_scale_mv (默认 11db 对应 3300mV) */
static Value m_adc_read(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    return val_int(l_hal_adc_read_raw(self.as.obj->fields[0]));
}

static Value m_adc_read_mv(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    int raw = l_hal_adc_read_raw(self.as.obj->fields[0]);
    int fs  = self.as.obj->fields[1];
    return val_int((raw * fs) / 4095);
}

static Value m_adc_read_uv(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    int raw = l_hal_adc_read_raw(self.as.obj->fields[0]);
    int fs  = self.as.obj->fields[1];
    return val_int(((raw * fs) / 4095) * 1000);
}

static Value m_adc_atten(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    int fs_mv = 3300;
    if (argv[0].type == V_INT || argv[0].type == V_NUM) {
        int db = int_arg(in, at, "ADC.atten", argv, 0);
        if (db == 0) fs_mv = 1100;
        else if (db == 2 || db == 3) fs_mv = 1500;
        else if (db == 6) fs_mv = 2200;
        else if (db == 11 || db == 12) fs_mv = 3300;
        else rt_error(in, at, "ADC.atten() 不支持的衰减值 %d (可选: 0, 2, 6, 11)", db);
    } else if (argv[0].type == V_STR) {
        const char *atn = val_get_cstr(argv[0]);
        if (strcmp(atn, "0db") == 0) fs_mv = 1100;
        else if (strcmp(atn, "2.5db") == 0) fs_mv = 1500;
        else if (strcmp(atn, "6db") == 0) fs_mv = 2200;
        else if (strcmp(atn, "11db") == 0) fs_mv = 3300;
        else rt_error(in, at, "ADC.atten() 不支持的衰减 \"%s\" (可选: \"0db\", \"2.5db\", \"6db\", \"11db\")", atn);
    } else {
        rt_error(in, at, "ADC.atten() 参数类型无效");
    }
    self.as.obj->fields[1] = fs_mv;
    return self;
}

static Value m_adc_write_sim(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    int raw = int_arg(in, at, "ADC.write_sim", argv, 0);
    if (raw < 0 || raw > 4095) rt_error(in, at, "ADC.write_sim() 采样值应在 0..4095, 实际是 %d", raw);
    l_hal_adc_write_sim(self.as.obj->fields[0], raw);
    return self;
}

static const ObjMethod adc_methods[] = {
    {"read",      0, 0, m_adc_read,      "adc.read()",             "读取 12 位 ADC 原始值(0..4095)"},
    {"read_mv",   0, 0, m_adc_read_mv,   "adc.read_mv()",          "读取折算后的毫伏电压(0..3300mV)"},
    {"read_uv",   0, 0, m_adc_read_uv,   "adc.read_uv()",          "读取折算后的微伏电压(uV)"},
    {"atten",     1, 1, m_adc_atten,     "adc.atten(衰减)",        "设置 ADC 衰减(\"0db\"/\"2.5db\"/\"6db\"/\"11db\" 或 0/6/11)"},
    {"write_sim", 1, 1, m_adc_write_sim, "adc.write_sim(0..4095)", "设置模拟 ADC 输入采样值(用于离线调试)"},
};

static void adc_format(const NativeObj *obj, char *buf, size_t size) {
    int pin = (int)obj->fields[0];
    int raw = l_hal_adc_read_raw(pin);
    snprintf(buf, size, "<ADC(pin=%d, raw=%d, mv=%d)>",
             pin, raw, (int)((raw * obj->fields[1]) / 4095));
}

const ObjClass adc_class = {
    "ADC", adc_methods, sizeof adc_methods / sizeof adc_methods[0], adc_format, NULL
};

static Value f_adc(Interp *in, const Node *at, int argc, Value *argv) {
    int pin = int_arg(in, at, "ADC", argv, 0);
    check_pin_usable(in, at, "ADC", pin, 0, 0);
    PinCap cap = query_pin_cap(cur_chip, pin);
    if (!cap.adc) {
        rt_error(in, at, "ADC() 引脚 GPIO %d 在 %s 上不支持 ADC 采样", pin, chip_specs[cur_chip].name);
    }
    int fs_mv = 3300;
    if (argc >= 2) {
        need_arg(in, at, "ADC", argv, 1, V_STR, "string");
        const char *atn = val_get_cstr(argv[1]);
        if (strcmp(atn, "0db") == 0) fs_mv = 1100;
        else if (strcmp(atn, "2.5db") == 0) fs_mv = 1500;
        else if (strcmp(atn, "6db") == 0) fs_mv = 2200;
        else if (strcmp(atn, "11db") == 0) fs_mv = 3300;
        else rt_error(in, at, "ADC() 不支持的衰减 \"%s\" (可选: \"0db\", \"2.5db\", \"6db\", \"11db\")", atn);
    }
    l_hal_adc_init(pin, fs_mv);
    NativeObj *o = obj_new(in, &adc_class);
    o->fields[0] = pin;
    o->fields[1] = fs_mv;
    return val_obj(o);
}

/* ================= 7. I2C 总线类 ================= */

static Value m_i2c_sim_device(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    int addr = int_arg(in, at, "I2C.sim_device", argv, 0);
    if (addr < 0x03 || addr > 0x77) rt_error(in, at, "I2C 从机地址应在 0x03..0x77, 实际是 0x%02X", addr);
    const uint8_t *init_ptr = NULL;
    size_t init_len = 0;
    if (argc >= 2 && argv[1].type == V_BYTES && argv[1].as.bytes) {
        init_ptr = argv[1].as.bytes->data;
        init_len = argv[1].as.bytes->len;
    }
    if (l_hal_i2c_sim_device(self.as.obj->fields[0], (uint8_t)addr, init_ptr, init_len) < 0)
        rt_error(in, at, "虚拟 I2C 设备槽位已满");
    return self;
}

static Value m_i2c_scan(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    uint8_t addrs[120];
    int n = l_hal_i2c_scan(self.as.obj->fields[0], addrs, 120);
    Array *a = array_new(in);
    Value av = val_array(a);
    gc_push_root(in, av);
    for (int i = 0; i < n; i++) array_push(a, val_int(addrs[i]));
    gc_pop_roots(in, 1);
    return av;
}

static Value m_i2c_write(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    int addr = int_arg(in, at, "I2C.write", argv, 0);
    uint8_t tmp[256];
    size_t len = 0;
    const uint8_t *buf = extract_byte_buf(in, at, "I2C.write", argv[1], tmp, sizeof tmp, &len);
    int n = l_hal_i2c_write(self.as.obj->fields[0], (uint8_t)addr, buf, len);
    if (n < 0) rt_error(in, at, "I2C.write() 设备 0x%02X 无应答", addr);
    return val_int(n);
}

static Value m_i2c_read(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    int addr = int_arg(in, at, "I2C.read", argv, 0);
    int len  = int_arg(in, at, "I2C.read", argv, 1);
    if (len < 0 || len > 4096) rt_error(in, at, "I2C.read() 长度无效: %d", len);
    Bytes *b = bytes_new(in, (size_t)len, NULL);
    if (l_hal_i2c_read(self.as.obj->fields[0], (uint8_t)addr, b->data, (size_t)len) < 0)
        rt_error(in, at, "I2C.read() 设备 0x%02X 读取失败", addr);
    return val_bytes(b);
}

static Value m_i2c_write_reg(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    int addr = int_arg(in, at, "I2C.write_reg", argv, 0);
    int reg  = int_arg(in, at, "I2C.write_reg", argv, 1);
    uint8_t tmp[256];
    size_t len = 0;
    const uint8_t *buf = extract_byte_buf(in, at, "I2C.write_reg", argv[2], tmp, sizeof tmp, &len);
    if (l_hal_i2c_write_reg(self.as.obj->fields[0], (uint8_t)addr, (uint8_t)reg, buf, len) < 0)
        rt_error(in, at, "I2C.write_reg() 设备 0x%02X 写寄存器失败", addr);
    return self;
}

static Value m_i2c_read_reg(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    int addr = int_arg(in, at, "I2C.read_reg", argv, 0);
    int reg  = int_arg(in, at, "I2C.read_reg", argv, 1);
    int len  = int_arg(in, at, "I2C.read_reg", argv, 2);
    if (len < 0 || len > 4096) rt_error(in, at, "I2C.read_reg() 长度无效: %d", len);
    Bytes *b = bytes_new(in, (size_t)len, NULL);
    if (l_hal_i2c_read_reg(self.as.obj->fields[0], (uint8_t)addr, (uint8_t)reg, b->data, (size_t)len) < 0)
        rt_error(in, at, "I2C.read_reg() 设备 0x%02X 读寄存器失败", addr);
    return val_bytes(b);
}

static const ObjMethod i2c_methods[] = {
    {"scan",       0, 0, m_i2c_scan,       "i2c.scan()",                       "扫描总线, 返回响应的设备地址数组"},
    {"write",      2, 2, m_i2c_write,      "i2c.write(地址, 数据)",            "向从机写入 bytes/数组/字符串, 返回字节数"},
    {"read",       2, 2, m_i2c_read,       "i2c.read(地址, 长度)",             "从从机读取指定长度, 返回 bytes"},
    {"write_reg",  3, 3, m_i2c_write_reg,  "i2c.write_reg(地址, 寄存器, 值)",  "写从机寄存器并返回自身"},
    {"read_reg",   3, 3, m_i2c_read_reg,   "i2c.read_reg(地址, 寄存器, 长度)", "读从机寄存器, 返回 bytes"},
    {"sim_device", 1, 2, m_i2c_sim_device, "i2c.sim_device(地址, [初值bytes])","注册模拟 I2C 从机设备(离线测试用)"},
};

static void i2c_format(const NativeObj *obj, char *buf, size_t size) {
    snprintf(buf, size, "<I2C(bus=%d, sda=%d, scl=%d, freq=%dHz)>",
             (int)obj->fields[0], (int)obj->fields[1], (int)obj->fields[2], (int)obj->fields[3]);
}

const ObjClass i2c_class = {
    "I2C", i2c_methods, sizeof i2c_methods / sizeof i2c_methods[0], i2c_format, NULL
};

static Value f_i2c(Interp *in, const Node *at, int argc, Value *argv) {
    const ChipSpec *sp = &chip_specs[cur_chip];
    int bus  = (argc >= 1) ? int_arg(in, at, "I2C", argv, 0) : 0;
    int sda  = (argc >= 2) ? int_arg(in, at, "I2C", argv, 1) : sp->default_sda;
    int scl  = (argc >= 3) ? int_arg(in, at, "I2C", argv, 2) : sp->default_scl;
    int freq = (argc >= 4) ? int_arg(in, at, "I2C", argv, 3) : 400000;
    check_pin_usable(in, at, "I2C SDA", sda, 1, 1);
    check_pin_usable(in, at, "I2C SCL", scl, 1, 1);
    l_hal_i2c_init(bus, sda, scl, freq);
    NativeObj *o = obj_new(in, &i2c_class);
    o->fields[0] = bus;
    o->fields[1] = sda;
    o->fields[2] = scl;
    o->fields[3] = freq;
    return val_obj(o);
}

/* ================= 8. SPI 总线类 ================= */

static Value m_spi_transfer(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    uint8_t tmp[256];
    size_t len = 0;
    const uint8_t *tx = extract_byte_buf(in, at, "SPI.transfer", argv[0], tmp, sizeof tmp, &len);
    Bytes *rx = bytes_new(in, len, NULL);
    l_hal_spi_transfer(self.as.obj->fields[0], tx, rx->data, len);
    return val_bytes(rx);
}

static Value m_spi_write(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    uint8_t tmp[256];
    size_t len = 0;
    const uint8_t *tx = extract_byte_buf(in, at, "SPI.write", argv[0], tmp, sizeof tmp, &len);
    l_hal_spi_transfer(self.as.obj->fields[0], tx, NULL, len);
    return val_int((int32_t)len);
}

static Value m_spi_read(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    int len  = int_arg(in, at, "SPI.read", argv, 0);
    int fill = (argc >= 2) ? int_arg(in, at, "SPI.read", argv, 1) : 0;
    if (len < 0 || len > 4096) rt_error(in, at, "SPI.read() 长度无效: %d", len);
    Bytes *b = bytes_new(in, (size_t)len, NULL);
    if (len > 0) memset(b->data, fill & 0xFF, (size_t)len);
    return val_bytes(b);
}

static const ObjMethod spi_methods[] = {
    {"transfer", 1, 1, m_spi_transfer, "spi.transfer(数据)",     "全双工收发 bytes/数组/字符串, 返回接收的 bytes"},
    {"xfer",     1, 1, m_spi_transfer, "spi.xfer(数据)",         "同 transfer()"},
    {"write",    1, 1, m_spi_write,    "spi.write(数据)",        "单向发送数据, 返回发送字节数"},
    {"read",     1, 2, m_spi_read,     "spi.read(长度, [填充])", "读取指定长度, 返回 bytes"},
};

static void spi_format(const NativeObj *obj, char *buf, size_t size) {
    snprintf(buf, size, "<SPI(bus=%d, sck=%d, mosi=%d, miso=%d)>",
             (int)obj->fields[0], (int)obj->fields[1], (int)obj->fields[2], (int)obj->fields[3]);
}

const ObjClass spi_class = {
    "SPI", spi_methods, sizeof spi_methods / sizeof spi_methods[0], spi_format, NULL
};

static Value f_spi(Interp *in, const Node *at, int argc, Value *argv) {
    const ChipSpec *sp = &chip_specs[cur_chip];
    int bus  = (argc >= 1) ? int_arg(in, at, "SPI", argv, 0) : 1;
    int sck  = (argc >= 2) ? int_arg(in, at, "SPI", argv, 1) : sp->default_sck;
    int mosi = (argc >= 3) ? int_arg(in, at, "SPI", argv, 2) : sp->default_mosi;
    int miso = (argc >= 4) ? int_arg(in, at, "SPI", argv, 3) : sp->default_miso;
    int freq = (argc >= 5) ? int_arg(in, at, "SPI", argv, 4) : 1000000;
    check_pin_usable(in, at, "SPI SCK", sck, 1, 0);
    check_pin_usable(in, at, "SPI MOSI", mosi, 1, 0);
    check_pin_usable(in, at, "SPI MISO", miso, 0, 0);
    l_hal_spi_init(bus, sck, mosi, miso, freq);
    NativeObj *o = obj_new(in, &spi_class);
    o->fields[0] = bus;
    o->fields[1] = sck;
    o->fields[2] = mosi;
    o->fields[3] = miso;
    return val_obj(o);
}

/* ================= 9. UART 串口类 ================= */

static Value m_uart_write(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    uint8_t tmp[256];
    size_t len = 0;
    const uint8_t *buf = extract_byte_buf(in, at, "UART.write", argv[0], tmp, sizeof tmp, &len);
    return val_int(l_hal_uart_write(self.as.obj->fields[0], buf, len));
}

static Value m_uart_any(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    return val_int((int32_t)l_hal_uart_any(self.as.obj->fields[0]));
}

static Value m_uart_read(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    int port = self.as.obj->fields[0];
    size_t avail = l_hal_uart_any(port);
    size_t want  = (argc >= 1) ? (size_t)int_arg(in, at, "UART.read", argv, 0) : avail;
    if (want > avail) want = avail;
    Bytes *b = bytes_new(in, want, NULL);
    l_hal_uart_read(port, b->data, want);
    return val_bytes(b);
}

static Value m_uart_read_str(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    int port = self.as.obj->fields[0];
    size_t avail = l_hal_uart_any(port);
    Str *s = str_new(in, NULL, avail);
    size_t n = l_hal_uart_read(port, (uint8_t *)s->chars, avail);
    s->chars[n] = '\0';
    s->len = n;
    return val_str(s);
}

static Value m_uart_sim_input(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    uint8_t tmp[256];
    size_t len = 0;
    const uint8_t *buf = extract_byte_buf(in, at, "UART.sim_input", argv[0], tmp, sizeof tmp, &len);
    l_hal_uart_sim_input(self.as.obj->fields[0], buf, len);
    return self;
}

static const ObjMethod uart_methods[] = {
    {"write",     1, 1, m_uart_write,     "uart.write(数据)",      "发送字符串或 bytes, 返回字节数"},
    {"read",      0, 1, m_uart_read,      "uart.read([最大长度])", "读取接收缓冲区, 返回 bytes"},
    {"read_str",  0, 0, m_uart_read_str,  "uart.read_str()",       "读取接收缓冲区全部内容为字符串"},
    {"any",       0, 0, m_uart_any,       "uart.any()",            "返回接收缓冲区可读字节数"},
    {"sim_input", 1, 1, m_uart_sim_input, "uart.sim_input(数据)",  "向模拟串口接收区注入数据(离线调试用)"},
};

static void uart_format(const NativeObj *obj, char *buf, size_t size) {
    snprintf(buf, size, "<UART(port=%d, baud=%d, tx=%d, rx=%d)>",
             (int)obj->fields[0], (int)obj->fields[1], (int)obj->fields[2], (int)obj->fields[3]);
}

const ObjClass uart_class = {
    "UART", uart_methods, sizeof uart_methods / sizeof uart_methods[0], uart_format, NULL
};

static Value f_uart(Interp *in, const Node *at, int argc, Value *argv) {
    const ChipSpec *sp = &chip_specs[cur_chip];
    int port = (argc >= 1) ? int_arg(in, at, "UART", argv, 0) : 1;
    int baud = 115200;
    int tx   = sp->default_tx;
    int rx   = sp->default_rx;
    if (argc == 2) {
        baud = int_arg(in, at, "UART", argv, 1);
    } else if (argc == 3) {
        tx = int_arg(in, at, "UART", argv, 1);
        rx = int_arg(in, at, "UART", argv, 2);
    } else if (argc >= 4) {
        int a1 = int_arg(in, at, "UART", argv, 1);
        int a2 = int_arg(in, at, "UART", argv, 2);
        int a3 = int_arg(in, at, "UART", argv, 3);
        if (a1 < 300 && a3 >= 300) {
            tx = a1; rx = a2; baud = a3;
        } else {
            baud = a1; tx = a2; rx = a3;
        }
    }
    check_pin_usable(in, at, "UART TX", tx, 1, 0);
    check_pin_usable(in, at, "UART RX", rx, 0, 0);
    l_hal_uart_init(port, baud, tx, rx);
    NativeObj *o = obj_new(in, &uart_class);
    o->fields[0] = port;
    o->fields[1] = baud;
    o->fields[2] = tx;
    o->fields[3] = rx;
    return val_obj(o);
}

/* ================= 11. M7: 事件派发 / Pin.irq / Timer / 看门狗 / 深度睡眠 ================= */

/* 回调注册表: 以隐藏全局 Map 保存(键为引脚号或定时器 id 字符串)。
 * 全局环境是 GC 根, 因此事件在排队/等待期间回调闭包不会被回收。 */
static const char IRQ_REG_NAME[]   = "__l_irq_handlers";
static const char TIMER_REG_NAME[] = "__l_timer_handlers";

static Map *hw_handler_map(Interp *in, const char *name) {
    Value *v = env_find(in->globals, name);
    if (v && v->type == V_MAP) return v->as.map;
    Map *m = map_new(in);
    env_define(in->globals, name, val_map(m));
    return m;
}

/* 待派发事件缓存: HAL 层 trampoline 只负责入缓存,
 * 由 hw_poll_events() 在脚本安全上下文(delay_ms/ticks_ms)统一调用脚本回调 */
#define L_MAX_PEND 32
typedef struct { int used, pin, level; } PendIrq;
typedef struct { int used, id; }          PendTimer;
static PendIrq   s_pend_irq[L_MAX_PEND];
static PendTimer s_pend_timer[L_MAX_PEND];
static uint32_t  s_timer_fires[L_MAX_SW_TIMERS];
static uint8_t   s_timer_alloc_mask = 0;   /* 定时器 id 位图(一次分配不复用) */

static void irq_trampoline(void *user, int pin, int level) {
    (void)user;
    for (int i = 0; i < L_MAX_PEND; i++) {
        if (!s_pend_irq[i].used) {
            s_pend_irq[i].used  = 1;
            s_pend_irq[i].pin   = pin;
            s_pend_irq[i].level = level;
            return;
        }
    }
}

static void timer_trampoline(void *user, int id) {
    (void)user;
    if (id < 0 || id >= L_MAX_SW_TIMERS) return;
    s_timer_fires[id]++;
    for (int i = 0; i < L_MAX_PEND; i++) {
        if (!s_pend_timer[i].used) {
            s_pend_timer[i].used = 1;
            s_pend_timer[i].id   = id;
            return;
        }
    }
}

static void dispatch_pending(Interp *in) {
    /* 1. 引脚中断事件: 回调签名 fn(level) */
    for (int i = 0; i < L_MAX_PEND; i++) {
        if (!s_pend_irq[i].used) continue;
        int pin = s_pend_irq[i].pin, level = s_pend_irq[i].level;
        s_pend_irq[i].used = 0;
        char key[16];
        snprintf(key, sizeof key, "%d", pin);
        Value *fn = map_get(hw_handler_map(in, IRQ_REG_NAME), key);
        if (!fn || (fn->type != V_FN && fn->type != V_NATIVE)) continue;
        Value cb = *fn;
        Value arg = val_int(level);
        gc_push_root(in, cb);
        gc_push_root(in, arg);
        interp_call(in, NULL, cb, 1, &arg);
        gc_pop_roots(in, 2);
    }
    /* 2. 定时器事件: 回调无参(闭包自行捕获上下文) */
    for (int i = 0; i < L_MAX_PEND; i++) {
        if (!s_pend_timer[i].used) continue;
        int id = s_pend_timer[i].id;
        s_pend_timer[i].used = 0;
        char key[16];
        snprintf(key, sizeof key, "t%d", id);
        Value *fn = map_get(hw_handler_map(in, TIMER_REG_NAME), key);
        if (!fn || (fn->type != V_FN && fn->type != V_NATIVE)) continue;
        Value cb = *fn;
        gc_push_root(in, cb);
        interp_call(in, NULL, cb, 0, NULL);
        gc_pop_roots(in, 1);
    }
}

void hw_poll_events(Interp *in) {
    l_hal_poll_events();
    dispatch_pending(in);
#if L_PLATFORM_ESP_IDF
    /* 检查串口 Ctrl+C (0x03): 允许用户打断 while true 等死循环脚本,
     * 否则脚本独占主任务后 REPL 与串口烧录通道将永久失联。
     * PC 宿主端不做此检查 (stdin 阻塞读会卡死交互终端)。 */
    {
        int ch = getchar();
        while (ch != EOF && ch != -1 && ch != 0xFF) {
            if (ch == 3) {
                rt_error(in, NULL, "脚本已被 Ctrl+C 中断, 已返回 REPL");
            }
            ch = getchar();
        }
    }
#endif
    if (l_hal_wdt_expired()) {
        l_hal_wdt_disable();
        rt_error(in, NULL, "看门狗超时 (Watchdog): 超过时限未调用 ESP32.watchdog_feed()");
    }
}

/* ---------- Pin 中断与模拟电平注入 ---------- */

static Value m_pin_irq(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    int pin = self.as.obj->fields[0];
    Map *reg = hw_handler_map(in, IRQ_REG_NAME);
    char key[16];
    snprintf(key, sizeof key, "%d", pin);

    if (argc == 0 || argv[0].type == V_NIL) {
        l_hal_gpio_irq_detach(pin);
        map_remove(reg, key);
        return self;
    }
    if (argv[0].type != V_FN) {
        rt_error(in, at, "Pin.irq() 回调必须是函数, 实际是 %s", type_name(argv[0]));
    }
    int mode = L_IRQ_CHANGE;
    if (argc >= 2) {
        need_arg(in, at, "Pin.irq", argv, 1, V_STR, "string");
        const char *m = val_get_cstr(argv[1]);
        if (strcmp(m, "rise") == 0)        mode = L_IRQ_RISE;
        else if (strcmp(m, "fall") == 0)   mode = L_IRQ_FALL;
        else if (strcmp(m, "change") == 0) mode = L_IRQ_CHANGE;
        else rt_error(in, at, "Pin.irq() 不支持的触发方式 \"%s\" (可选: \"rise\", \"fall\", \"change\")", m);
    }
    check_pin_usable(in, at, "Pin.irq", pin, 0, 0);
    if (l_hal_gpio_irq_attach(pin, mode, irq_trampoline, NULL) != 0) {
        rt_error(in, at, "Pin.irq() 注册中断失败");
    }
    map_set(reg, str_new(in, key, strlen(key)), argv[0]);
    return self;
}

static Value m_pin_write_sim(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    int pin = self.as.obj->fields[0];
    int v = int_arg(in, at, "Pin.write_sim", argv, 0);
    check_pin_usable(in, at, "Pin.write_sim", pin, 0, 0);
    l_hal_gpio_sim_write(pin, v);
    return self;
}

/* ---------- Timer 软件定时器类 ---------- */

static Value m_timer_start(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc; (void)argv;
    NativeObj *o = self.as.obj;
    if (l_hal_timer_start(o->fields[0], o->fields[1], o->fields[2], timer_trampoline, NULL) != 0) {
        rt_error(in, at, "Timer.start() 启动失败");
    }
    return self;
}

static Value m_timer_stop(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    l_hal_timer_stop(self.as.obj->fields[0]);
    return self;
}

static Value m_timer_running(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    return val_bool(l_hal_timer_running(self.as.obj->fields[0]));
}

static Value m_timer_period(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    NativeObj *o = self.as.obj;
    if (argc >= 1) {
        int ms = int_arg(in, at, "Timer.period", argv, 0);
        if (ms <= 0) rt_error(in, at, "Timer 周期必须大于 0 毫秒");
        o->fields[1] = ms;
        if (l_hal_timer_running(o->fields[0])) {
            l_hal_timer_start(o->fields[0], ms, o->fields[2], timer_trampoline, NULL);
        }
    }
    return val_int(o->fields[1]);
}

static Value m_timer_count(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    return val_int((int32_t)s_timer_fires[self.as.obj->fields[0]]);
}

static void timer_destroy(NativeObj *obj) {
    l_hal_timer_stop(obj->fields[0]);
}

static void timer_format(const NativeObj *obj, char *buf, size_t size) {
    int id = obj->fields[0];
    if (id < 0 || id >= L_MAX_SW_TIMERS) id = 0;
    snprintf(buf, size, "<Timer(%dms, %s, fires=%u)>",
             (int)obj->fields[1],
             obj->fields[2] ? "periodic" : "oneshot",
             (unsigned)s_timer_fires[id]);
}

static const ObjMethod timer_methods[] = {
    {"start",   0, 0, m_timer_start,   "timer.start()",           "启动(或重启)定时器, 返回自身"},
    {"stop",    0, 0, m_timer_stop,    "timer.stop()",            "停止定时器, 返回自身"},
    {"running", 0, 0, m_timer_running, "timer.running()",         "返回定时器是否运行中"},
    {"period",  0, 1, m_timer_period,  "timer.period([毫秒])",    "无参读取周期; 有参修改并重启运行中的定时器"},
    {"count",   0, 0, m_timer_count,   "timer.count()",           "返回已触发次数"},
};

const ObjClass timer_class = {
    "Timer", timer_methods, sizeof timer_methods / sizeof timer_methods[0],
    timer_format, timer_destroy
};

static Value f_timer(Interp *in, const Node *at, int argc, Value *argv) {
    int ms = int_arg(in, at, "Timer", argv, 0);
    if (ms <= 0) rt_error(in, at, "Timer 周期必须大于 0 毫秒");
    if (argv[1].type != V_FN) {
        rt_error(in, at, "Timer 回调必须是函数, 实际是 %s", type_name(argv[1]));
    }
    int repeating = 1;
    if (argc >= 3) {
        if (argv[2].type == V_BOOL) repeating = argv[2].as.boolean ? 1 : 0;
        else repeating = (int_arg(in, at, "Timer", argv, 2) != 0);
    }
    int id = -1;
    for (int i = 0; i < L_MAX_SW_TIMERS; i++) {
        if (!(s_timer_alloc_mask & (1u << i))) { id = i; break; }
    }
    if (id < 0) rt_error(in, at, "软件定时器数量已达上限 (%d)", L_MAX_SW_TIMERS);
    s_timer_alloc_mask |= (uint8_t)(1u << id);
    s_timer_fires[id] = 0;

    NativeObj *o = obj_new(in, &timer_class);
    o->fields[0] = id;
    o->fields[1] = ms;
    o->fields[2] = repeating;
    o->fields[3] = 0;
    char key[16];
    snprintf(key, sizeof key, "t%d", id);
    Value fv = argv[1];
    gc_push_root(in, fv);
    map_set(hw_handler_map(in, TIMER_REG_NAME), str_new(in, key, strlen(key)), fv);
    gc_pop_roots(in, 1);
    l_hal_timer_start(id, ms, repeating, timer_trampoline, NULL);
    return val_obj(o);
}

/* ---------- ESP32 深度睡眠与看门狗 ---------- */

static Value m_esp_deepsleep(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    int ms = (argc >= 1) ? int_arg(in, at, "ESP32.deepsleep", argv, 0) : 0;
    if (ms < 0) rt_error(in, at, "deepsleep() 时间不能为负数");
    /* 真机: 睡眠后芯片复位重启不会返回; PC 模拟: 记录唤醒原因后立即返回 */
    l_hal_deepsleep(ms);
    return self;
}

static Value m_esp_wake_reason(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)self; (void)argc; (void)argv;
    int code = l_hal_wake_reason();
    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);
    map_put_int(in, m, "code", code);
    map_put_cstr(in, m, "reason", code == 1 ? "timer" : code == 2 ? "pin" : "power_on");
    gc_pop_roots(in, 1);
    return mv;
}

static Value m_esp_wdt_enable(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self; (void)argc;
    int ms = int_arg(in, at, "ESP32.watchdog_enable", argv, 0);
    if (ms <= 0) rt_error(in, at, "看门狗超时时间必须大于 0 毫秒");
    if (l_hal_wdt_enable(ms) != 0) rt_error(in, at, "看门狗启动失败");
    return self;
}

static Value m_esp_wdt_feed(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)self; (void)argc; (void)argv;
    l_hal_wdt_feed();
    return self;
}

static Value m_esp_wdt_disable(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)self; (void)argc; (void)argv;
    l_hal_wdt_disable();
    return self;
}

/* ================= 10. 注册与内置模块导出 ================= */

static const Native hw_natives[] = {
    {"Pin",     1, 3, f_pin,     "Pin(引脚号, [模式=\"out\"], [初值])",           "创建 GPIO 引脚对象, 支持 .on() .off() .toggle() .value() .mode()"},
    {"pin",     1, 3, f_pin,     "pin(引脚号, [模式=\"out\"], [初值])",           "同 Pin(...), 创建 GPIO 引脚对象"},
    {"bytes",   1, 1, f_bytes,   "bytes(长度 | 数组 | 字符串)",                   "创建定长字节缓冲, 支持 b[i] 读写与 .hex() .u16_be() 等方法"},
    {"PWM",     1, 3, f_pwm,     "PWM(引脚号, [频率=1000], [占空比=0])",          "创建 LEDC PWM 对象, 支持 .freq() .duty() .duty_percent() .stop()"},
    {"ADC",     1, 2, f_adc,     "ADC(引脚号, [衰减=\"11db\"])",                  "创建 ADC 采样对象, 支持 .read() .read_mv()"},
    {"I2C",     0, 4, f_i2c,     "I2C([bus=0], [sda], [scl], [freq])",            "创建 I2C 总线对象, 支持 .scan() .write() .read() .write_reg() .read_reg()"},
    {"SPI",     0, 5, f_spi,     "SPI([bus=1], [sck], [mosi], [miso], [freq])",   "创建 SPI 总线对象, 支持 .transfer() .write() .read()"},
    {"UART",    0, 4, f_uart,    "UART([port=1], [baud=115200], [tx], [rx])",     "创建 UART 串口对象, 支持 .write() .read() .read_str() .any()"},
    {"Timer",   2, 3, f_timer,   "Timer(周期毫秒, 回调, [周期模式=true])",        "创建软件定时器并自动启动, 支持 .stop() .start() .count()"},
    {"ports",   0, 0, f_ports,   "ports()",                                       "扫描本机可用串口(如 [\"COM3\"])"},
    {"upload",  2, 3, f_upload,  "upload(串口, 脚本文件或代码, [波特率=115200])", "通过串口(或 \"sim\")将脚本烧录到 ESP32 的 l_script 分区"},
    {"connect", 1, 3, f_connect, "connect(串口, [代码或波特率], [代码])",            "连接 ESP32 串口交互终端，或直接远程发送执行代码"},
};

const NativeGroup group_hw = {
    "hw", "硬件外设(ESP32)", hw_natives, sizeof hw_natives / sizeof hw_natives[0]
};

void hw_install_globals(Interp *in) {
    NativeObj *esp = obj_new(in, &esp32_class);
    Value ev = val_obj(esp);
    env_define(in->globals, "ESP32", ev);
    env_define(in->globals, "esp32", ev);
}

Value hw_build_module(Interp *in) {
    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);
    for (size_t i = 0; i < group_hw.count; i++) {
        Value nv = val_nil();
        nv.type = V_NATIVE;
        nv.as.native = &group_hw.items[i];
        map_set(m, str_new(in, group_hw.items[i].name, strlen(group_hw.items[i].name)), nv);
    }
    Value *esp = map_get(m, "ESP32");
    if (!esp) {
        NativeObj *eo = obj_new(in, &esp32_class);
        map_set(m, str_new(in, "ESP32", 5), val_obj(eo));
        map_set(m, str_new(in, "esp32", 5), val_obj(eo));
    }
    gc_pop_roots(in, 1);
    return mv;
}

#endif /* L_WITH_STDLIB_HW */
