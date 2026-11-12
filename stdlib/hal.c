/* L 语言硬件抽象层 (HAL) 实现:
 * - 当 L_PLATFORM_ESP_IDF=1 时调用 ESP-IDF 原生驱动与 esp_partition API
 * - 当 L_PLATFORM_HOST=1 时提供 PC 跨平台(Windows/Linux/macOS)全功能硬件与裸 Flash 分区模拟 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../l_hal.h"

#if L_PLATFORM_ESP_IDF
#  include "driver/gpio.h"
#  include "driver/ledc.h"
#  include "driver/i2c.h"
#  include "driver/spi_master.h"
#  include "driver/uart.h"
#  include "esp_idf_version.h"
#  include "esp_timer.h"
#  include "esp_system.h"
#  include "esp_sleep.h"
#  include "esp_task_wdt.h"
#  include "esp_mac.h"
#  include "esp_chip_info.h"
#  include "esp_partition.h"
#  include "esp_rom_sys.h"
#  include "esp_wifi.h"
#  include "esp_netif.h"
#  include "esp_event.h"
#  include "esp_http_client.h"
#  include "esp_sntp.h"
#  include "nvs_flash.h"
#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"
#  include "freertos/queue.h"
#  include "freertos/event_groups.h"
#elif defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  include <wininet.h>
#  include <conio.h>
#else
#  include <unistd.h>
#  include <fcntl.h>
#  include <termios.h>
#endif

/* ================= 公共状态与模式表 ================= */

static const char *const pin_mode_names[] = {
    "disabled", "out", "in", "in_pullup", "in_pulldown", "open_drain", "pwm", "adc"
};

const char *l_hal_gpio_mode_name(LPinMode mode) {
    if (mode < L_PIN_DISABLED || mode > L_PIN_ADC) return "disabled";
    return pin_mode_names[mode];
}

static int      s_cpu_mhz = 240;
static uint8_t  s_pin_level[L_MAX_GPIO_PIN + 1] = {0};
static LPinMode s_pin_mode[L_MAX_GPIO_PIN + 1]  = {0};
static uint16_t s_adc_raw[L_MAX_GPIO_PIN + 1]   = {0};
static int      s_wake_reason = 0;   /* 深度睡眠唤醒原因: 0=上电 1=定时器 2=外部引脚 */

/* ================= IEEE 802.3 CRC32 计算 (无查表, 节省 Flash/RAM) ================= */

uint32_t l_hal_crc32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

/* ================= 1. 系统、时钟与芯片信息 HAL ================= */

int64_t l_hal_ticks_ms(void) {
#if L_PLATFORM_ESP_IDF
    return (int64_t)(esp_timer_get_time() / 1000);
#elif defined(_WIN32)
    /* Windows CRT 的 clock() 为墙钟时间 */
    return (int64_t)((double)clock() * 1000.0 / CLOCKS_PER_SEC);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

int64_t l_hal_ticks_us(void) {
#if L_PLATFORM_ESP_IDF
    return (int64_t)esp_timer_get_time();
#elif defined(_WIN32)
    return (int64_t)((double)clock() * 1000000.0 / CLOCKS_PER_SEC);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}

void l_hal_delay_ms(uint32_t ms) {
#if L_PLATFORM_ESP_IDF
    if (ms >= portTICK_PERIOD_MS) {
        vTaskDelay(ms / portTICK_PERIOD_MS);
    }
    uint32_t rem = ms % portTICK_PERIOD_MS;
    if (rem > 0) esp_rom_delay_us(rem * 1000);
#elif defined(_WIN32)
    Sleep((unsigned long)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

int32_t l_hal_free_heap(size_t vm_allocated_bytes, int sram_kb) {
#if L_PLATFORM_ESP_IDF
    (void)vm_allocated_bytes; (void)sram_kb;
    return (int32_t)esp_get_free_heap_size();
#else
    int32_t total = sram_kb * 1024;
    int32_t used  = (int32_t)vm_allocated_bytes;
    return total > used ? total - used : 0;
#endif
}

const char *l_hal_idf_version(void) {
#if L_PLATFORM_ESP_IDF
    return esp_get_idf_version();
#else
    return "ESP-IDF v5.3-sim";
#endif
}

int l_hal_is_hardware(void) {
#if L_PLATFORM_ESP_IDF
    return 1;
#else
    return 0;
#endif
}

const char *l_hal_chip_name(void) {
#if L_PLATFORM_ESP_IDF
    esp_chip_info_t info;
    esp_chip_info(&info);
    switch (info.model) {
    case CHIP_ESP32:   return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3";
    case CHIP_ESP32C6: return "ESP32-C6";
    default:           return "ESP32-UNKNOWN";
    }
#else
    return "SIM";
#endif
}

void l_hal_get_mac(uint8_t mac[6], int chip_id) {
#if L_PLATFORM_ESP_IDF
    (void)chip_id;
    esp_efuse_mac_get_default(mac);
#else
    mac[0] = 0x24; mac[1] = 0x6F; mac[2] = 0x28;
    mac[3] = 0xE5; mac[4] = 0x32; mac[5] = (uint8_t)(chip_id & 0xFF);
#endif
}

int l_hal_get_cpu_mhz(void) {
    return s_cpu_mhz;
}

void l_hal_set_cpu_mhz(int mhz) {
    s_cpu_mhz = mhz;
}

/* ================= 2. GPIO HAL ================= */

void l_hal_gpio_set_mode(int pin, LPinMode mode) {
    if (pin < 0 || pin > L_MAX_GPIO_PIN) return;
#if L_PLATFORM_ESP_IDF
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (mode == L_PIN_IN) {
        cfg.mode = GPIO_MODE_INPUT;
    } else if (mode == L_PIN_IN_PULLUP) {
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    } else if (mode == L_PIN_IN_PULLDOWN) {
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    } else if (mode == L_PIN_OPEN_DRAIN) {
        cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    } else if (mode == L_PIN_OUT) {
        cfg.mode = GPIO_MODE_INPUT_OUTPUT;
    }
    if (mode >= L_PIN_OUT && mode <= L_PIN_OPEN_DRAIN) {
        gpio_config(&cfg);
    }
#endif
    s_pin_mode[pin] = mode;
}

LPinMode l_hal_gpio_get_mode(int pin) {
    if (pin < 0 || pin > L_MAX_GPIO_PIN) return L_PIN_DISABLED;
    return s_pin_mode[pin];
}

void l_hal_gpio_write(int pin, int level) {
    if (pin < 0 || pin > L_MAX_GPIO_PIN) return;
    int v = level ? 1 : 0;
#if L_PLATFORM_ESP_IDF
    gpio_set_level((gpio_num_t)pin, v);
#endif
    s_pin_level[pin] = (uint8_t)v;
}

int l_hal_gpio_read(int pin) {
    if (pin < 0 || pin > L_MAX_GPIO_PIN) return 0;
#if L_PLATFORM_ESP_IDF
    return gpio_get_level((gpio_num_t)pin);
#else
    return s_pin_level[pin];
#endif
}

/* ================= 3. PWM (LEDC) HAL ================= */

void l_hal_pwm_init(int pin, int freq_hz, int duty_10bit) {
    if (pin < 0 || pin > L_MAX_GPIO_PIN) return;
#if L_PLATFORM_ESP_IDF
    ledc_timer_config_t tcfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = (uint32_t)freq_hz,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&tcfg);
    ledc_channel_config_t ccfg = {
        .gpio_num   = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = (uint32_t)duty_10bit,
        .hpoint     = 0,
    };
    ledc_channel_config(&ccfg);
#else
    (void)freq_hz;
#endif
    s_pin_mode[pin]  = L_PIN_PWM;
    s_pin_level[pin] = (duty_10bit >= 512) ? 1 : 0;
}

void l_hal_pwm_set_freq(int pin, int freq_hz) {
    (void)pin;
#if L_PLATFORM_ESP_IDF
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, (uint32_t)freq_hz);
#else
    (void)freq_hz;
#endif
}

void l_hal_pwm_set_duty(int pin, int duty_10bit) {
    if (pin < 0 || pin > L_MAX_GPIO_PIN) return;
#if L_PLATFORM_ESP_IDF
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)duty_10bit);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#endif
    s_pin_level[pin] = (duty_10bit >= 512) ? 1 : 0;
}

void l_hal_pwm_stop(int pin) {
    if (pin < 0 || pin > L_MAX_GPIO_PIN) return;
#if L_PLATFORM_ESP_IDF
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
#endif
    s_pin_level[pin] = 0;
    s_pin_mode[pin]  = L_PIN_OUT;
}

/* ================= 4. ADC HAL ================= */

void l_hal_adc_init(int pin, int full_scale_mv) {
    (void)full_scale_mv;
    if (pin >= 0 && pin <= L_MAX_GPIO_PIN) s_pin_mode[pin] = L_PIN_ADC;
}

int l_hal_adc_read_raw(int pin) {
    if (pin < 0 || pin > L_MAX_GPIO_PIN) return 0;
    return s_adc_raw[pin];
}

void l_hal_adc_write_sim(int pin, int raw) {
    if (pin >= 0 && pin <= L_MAX_GPIO_PIN) s_adc_raw[pin] = (uint16_t)raw;
}

/* ================= 5. I2C 总线 HAL ================= */

#define MAX_SIM_I2C_DEVS 8
typedef struct {
    int     active;
    uint8_t addr;
    uint8_t regs[256];
    uint8_t cur_reg;
} SimI2cDev;

static SimI2cDev s_sim_i2c[MAX_SIM_I2C_DEVS];

static SimI2cDev *find_sim_i2c(uint8_t addr, int create) {
    for (int i = 0; i < MAX_SIM_I2C_DEVS; i++) {
        if (s_sim_i2c[i].active && s_sim_i2c[i].addr == addr)
            return &s_sim_i2c[i];
    }
    if (!create) return NULL;
    for (int i = 0; i < MAX_SIM_I2C_DEVS; i++) {
        if (!s_sim_i2c[i].active) {
            memset(&s_sim_i2c[i], 0, sizeof(SimI2cDev));
            s_sim_i2c[i].active = 1;
            s_sim_i2c[i].addr   = addr;
            return &s_sim_i2c[i];
        }
    }
    return NULL;
}

void l_hal_i2c_init(int bus, int sda, int scl, int freq_hz) {
#if L_PLATFORM_ESP_IDF
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = sda,
        .scl_io_num       = scl,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = (uint32_t)freq_hz,
    };
    i2c_param_config((i2c_port_t)bus, &conf);
    i2c_driver_install((i2c_port_t)bus, conf.mode, 0, 0, 0);
#else
    (void)bus; (void)sda; (void)scl; (void)freq_hz;
#endif
}

int l_hal_i2c_sim_device(int bus, uint8_t addr, const uint8_t *init_regs, size_t init_len) {
    (void)bus;
    SimI2cDev *dev = find_sim_i2c(addr, 1);
    if (!dev) return -1;
    if (init_regs && init_len > 0) {
        size_t n = init_len > 256 ? 256 : init_len;
        memcpy(dev->regs, init_regs, n);
    }
    return 0;
}

int l_hal_i2c_scan(int bus, uint8_t *out_addrs, int max_addrs) {
    int count = 0;
#if L_PLATFORM_ESP_IDF
    for (int addr = 0x03; addr <= 0x77 && count < max_addrs; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (uint8_t)((addr << 1) | I2C_MASTER_WRITE), true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin((i2c_port_t)bus, cmd, 10 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) out_addrs[count++] = (uint8_t)addr;
    }
#else
    (void)bus;
    for (int addr = 0x03; addr <= 0x77 && count < max_addrs; addr++) {
        if (find_sim_i2c((uint8_t)addr, 0)) out_addrs[count++] = (uint8_t)addr;
    }
#endif
    return count;
}

int l_hal_i2c_write(int bus, uint8_t addr, const uint8_t *data, size_t len) {
#if L_PLATFORM_ESP_IDF
    esp_err_t err = i2c_master_write_to_device((i2c_port_t)bus, addr, data, len, 100 / portTICK_PERIOD_MS);
    return (err == ESP_OK) ? (int)len : -1;
#else
    (void)bus;
    SimI2cDev *dev = find_sim_i2c(addr, 1);
    if (dev && len > 0) {
        dev->cur_reg = data[0];
        for (size_t i = 1; i < len; i++) {
            dev->regs[(uint8_t)(dev->cur_reg + i - 1)] = data[i];
        }
    }
    return (int)len;
#endif
}

int l_hal_i2c_read(int bus, uint8_t addr, uint8_t *data, size_t len) {
#if L_PLATFORM_ESP_IDF
    esp_err_t err = i2c_master_read_from_device((i2c_port_t)bus, addr, data, len, 100 / portTICK_PERIOD_MS);
    return (err == ESP_OK) ? (int)len : -1;
#else
    (void)bus;
    SimI2cDev *dev = find_sim_i2c(addr, 0);
    if (dev) {
        for (size_t i = 0; i < len; i++) data[i] = dev->regs[(uint8_t)(dev->cur_reg + i)];
    } else if (len > 0) {
        memset(data, 0, len);
    }
    return (int)len;
#endif
}

int l_hal_i2c_write_reg(int bus, uint8_t addr, uint8_t reg, const uint8_t *data, size_t len) {
#if L_PLATFORM_ESP_IDF
    uint8_t tmp[257];
    if (len > 256) len = 256;
    tmp[0] = reg;
    memcpy(tmp + 1, data, len);
    return l_hal_i2c_write(bus, addr, tmp, len + 1);
#else
    (void)bus;
    SimI2cDev *dev = find_sim_i2c(addr, 1);
    if (dev) {
        dev->cur_reg = reg;
        for (size_t i = 0; i < len; i++) dev->regs[(uint8_t)(reg + i)] = data[i];
    }
    return (int)len;
#endif
}

int l_hal_i2c_read_reg(int bus, uint8_t addr, uint8_t reg, uint8_t *data, size_t len) {
#if L_PLATFORM_ESP_IDF
    esp_err_t err = i2c_master_write_read_device((i2c_port_t)bus, addr, &reg, 1, data, len, 100 / portTICK_PERIOD_MS);
    return (err == ESP_OK) ? (int)len : -1;
#else
    (void)bus;
    SimI2cDev *dev = find_sim_i2c(addr, 0);
    if (dev) {
        for (size_t i = 0; i < len; i++) data[i] = dev->regs[(uint8_t)(reg + i)];
    } else if (len > 0) {
        memset(data, 0, len);
    }
    return (int)len;
#endif
}

/* ================= 6. SPI 总线 HAL ================= */

void l_hal_spi_init(int bus, int sck, int mosi, int miso, int freq_hz) {
    (void)bus; (void)sck; (void)mosi; (void)miso; (void)freq_hz;
}

int l_hal_spi_transfer(int bus, const uint8_t *tx, uint8_t *rx, size_t len) {
    (void)bus;
    /* PC 模拟默认回环 (MISO = MOSI) */
    if (rx && tx && len > 0) memcpy(rx, tx, len);
    else if (rx && len > 0) memset(rx, 0, len);
    return (int)len;
}

/* ================= 7. UART 串口 HAL ================= */

static uint8_t s_sim_uart_rx[1024];
static size_t  s_sim_uart_rx_len = 0;

void l_hal_uart_init(int port, int baud, int tx, int rx) {
#if L_PLATFORM_ESP_IDF
    uart_config_t ucfg = {
        .baud_rate  = baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config((uart_port_t)port, &ucfg);
    uart_set_pin((uart_port_t)port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install((uart_port_t)port, 1024, 0, 0, NULL, 0);
#else
    (void)port; (void)baud; (void)tx; (void)rx;
#endif
}

int l_hal_uart_write(int port, const uint8_t *data, size_t len) {
#if L_PLATFORM_ESP_IDF
    return uart_write_bytes((uart_port_t)port, data, len);
#else
    (void)port; (void)data;
    return (int)len;
#endif
}

size_t l_hal_uart_any(int port) {
#if L_PLATFORM_ESP_IDF
    size_t avail = 0;
    uart_get_buffered_data_len((uart_port_t)port, &avail);
    return avail;
#else
    (void)port;
    return s_sim_uart_rx_len;
#endif
}

size_t l_hal_uart_read(int port, uint8_t *buf, size_t max_len) {
#if L_PLATFORM_ESP_IDF
    int n = uart_read_bytes((uart_port_t)port, buf, (uint32_t)max_len, 0);
    return n > 0 ? (size_t)n : 0;
#else
    (void)port;
    size_t want = max_len < s_sim_uart_rx_len ? max_len : s_sim_uart_rx_len;
    if (want > 0) {
        memcpy(buf, s_sim_uart_rx, want);
        if (want < s_sim_uart_rx_len)
            memmove(s_sim_uart_rx, s_sim_uart_rx + want, s_sim_uart_rx_len - want);
        s_sim_uart_rx_len -= want;
    }
    return want;
#endif
}

void l_hal_uart_sim_input(int port, const uint8_t *data, size_t len) {
    (void)port;
    size_t room = sizeof(s_sim_uart_rx) - s_sim_uart_rx_len;
    if (len > room) len = room;
    if (len > 0) {
        memcpy(s_sim_uart_rx + s_sim_uart_rx_len, data, len);
        s_sim_uart_rx_len += len;
    }
}

/* ================= 8. 裸 Flash 脚本分区 (l_script) HAL =================
 * 在 ESP-IDF 下使用 esp_partition_find_first + esp_partition_mmap 零 RAM 拷贝映射;
 * 在 PC 宿主下使用 64KB 模拟 Flash 缓冲区(初始全 0xFF), 完全复现擦写、16 字节头与 CRC32 校验。 */

#if L_PLATFORM_ESP_IDF
static const esp_partition_t *s_script_part = NULL;
static char *s_esp_script_buf = NULL;

static const esp_partition_t *get_script_part(void) {
    if (!s_script_part) {
        s_script_part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)L_FLASH_PART_SUBTYPE, L_FLASH_PART_NAME);
    }
    return s_script_part;
}
#else
static uint8_t s_sim_flash[L_FLASH_PART_SIZE];
static int     s_sim_flash_inited = 0;

static void ensure_sim_flash(void) {
    if (!s_sim_flash_inited) {
        memset(s_sim_flash, 0xFF, sizeof s_sim_flash);
        s_sim_flash_inited = 1;
    }
}
#endif

LFlashStatus l_hal_flash_info(LScriptHeader *out_hdr, size_t *out_part_size) {
#if L_PLATFORM_ESP_IDF
    const esp_partition_t *part = get_script_part();
    if (!part) {
        if (out_part_size) *out_part_size = 0;
        return L_FLASH_ERR_PART;
    }
    if (out_part_size) *out_part_size = part->size;
    LScriptHeader hdr;
    if (esp_partition_read(part, 0, &hdr, sizeof hdr) != ESP_OK) return L_FLASH_ERR_IO;
    if (out_hdr) *out_hdr = hdr;
    if (hdr.magic != L_SCRIPT_MAGIC_SRC && hdr.magic != L_SCRIPT_MAGIC_BC) return L_FLASH_EMPTY;
    if (hdr.length == 0 || sizeof(LScriptHeader) + hdr.length >= part->size) return L_FLASH_EMPTY;
    size_t dummy_len = 0;
    LFlashStatus st = L_FLASH_OK;
    l_hal_flash_read_script(&dummy_len, &st);
    return st;
#else
    ensure_sim_flash();
    if (out_part_size) *out_part_size = L_FLASH_PART_SIZE;
    LScriptHeader hdr;
    memcpy(&hdr, s_sim_flash, sizeof hdr);
    if (out_hdr) *out_hdr = hdr;
    if (hdr.magic != L_SCRIPT_MAGIC_SRC && hdr.magic != L_SCRIPT_MAGIC_BC) return L_FLASH_EMPTY;
    if (hdr.length > L_FLASH_PART_SIZE - sizeof(LScriptHeader) - 1) return L_FLASH_EMPTY;
    uint32_t calc = l_hal_crc32(s_sim_flash + sizeof(LScriptHeader), hdr.length);
    return (calc == hdr.crc32) ? L_FLASH_OK : L_FLASH_ERR_CRC;
#endif
}

LFlashStatus l_hal_flash_erase_script(void) {
#if L_PLATFORM_ESP_IDF
    const esp_partition_t *part = get_script_part();
    if (!part) return L_FLASH_ERR_PART;
    if (s_esp_script_buf) {
        free(s_esp_script_buf);
        s_esp_script_buf = NULL;
    }
    return (esp_partition_erase_range(part, 0, part->size) == ESP_OK) ? L_FLASH_OK : L_FLASH_ERR_IO;
#else
    memset(s_sim_flash, 0xFF, sizeof s_sim_flash);
    s_sim_flash_inited = 1;
    return L_FLASH_OK;
#endif
}

LFlashStatus l_hal_flash_write_script(const char *src, size_t len, uint32_t flags) {
    LScriptHeader hdr;
    hdr.magic  = L_SCRIPT_MAGIC_SRC;
    hdr.length = (uint32_t)len;
    hdr.crc32  = l_hal_crc32(src, len);
    hdr.flags  = flags;

#if L_PLATFORM_ESP_IDF
    const esp_partition_t *part = get_script_part();
    if (!part || sizeof(LScriptHeader) + len + 1 > part->size) return L_FLASH_ERR_PART;
    if (s_esp_script_buf) {
        free(s_esp_script_buf);
        s_esp_script_buf = NULL;
    }
    size_t erase_sz = (sizeof(LScriptHeader) + len + 1 + 4095) & ~(size_t)4095;
    if (erase_sz > part->size) erase_sz = part->size;
    if (esp_partition_erase_range(part, 0, erase_sz) != ESP_OK) return L_FLASH_ERR_IO;
    if (esp_partition_write(part, 0, &hdr, sizeof hdr) != ESP_OK) return L_FLASH_ERR_IO;
    if (len > 0 && esp_partition_write(part, sizeof hdr, src, len) != ESP_OK) return L_FLASH_ERR_IO;
    char zero = '\0';
    if (esp_partition_write(part, sizeof hdr + len, &zero, 1) != ESP_OK) return L_FLASH_ERR_IO;
    return L_FLASH_OK;
#else
    ensure_sim_flash();
    if (sizeof(LScriptHeader) + len + 1 > L_FLASH_PART_SIZE) return L_FLASH_ERR_PART;
    memset(s_sim_flash, 0xFF, sizeof s_sim_flash);
    memcpy(s_sim_flash, &hdr, sizeof hdr);
    if (len > 0) memcpy(s_sim_flash + sizeof hdr, src, len);
    s_sim_flash[sizeof hdr + len] = '\0';
    return L_FLASH_OK;
#endif
}

const char *l_hal_flash_read_script(size_t *out_len, LFlashStatus *out_status) {
    if (out_len) *out_len = 0;
#if L_PLATFORM_ESP_IDF
    const esp_partition_t *part = get_script_part();
    if (!part) {
        if (out_status) *out_status = L_FLASH_ERR_PART;
        return NULL;
    }
    LScriptHeader hdr;
    if (esp_partition_read(part, 0, &hdr, sizeof hdr) != ESP_OK) {
        if (out_status) *out_status = L_FLASH_ERR_IO;
        return NULL;
    }
    if (hdr.magic != L_SCRIPT_MAGIC_SRC && hdr.magic != L_SCRIPT_MAGIC_BC) {
        if (out_status) *out_status = L_FLASH_EMPTY;
        return NULL;
    }
    if (sizeof(LScriptHeader) + hdr.length + 1 > part->size) {
        if (out_status) *out_status = L_FLASH_EMPTY;
        return NULL;
    }
    if (s_esp_script_buf) {
        free(s_esp_script_buf);
        s_esp_script_buf = NULL;
    }
    s_esp_script_buf = (char *)malloc(hdr.length + 1);
    if (!s_esp_script_buf) {
        if (out_status) *out_status = L_FLASH_ERR_IO;
        return NULL;
    }
    if (esp_partition_read(part, sizeof(LScriptHeader), s_esp_script_buf, hdr.length) != ESP_OK) {
        free(s_esp_script_buf);
        s_esp_script_buf = NULL;
        if (out_status) *out_status = L_FLASH_ERR_IO;
        return NULL;
    }
    s_esp_script_buf[hdr.length] = '\0';
    uint32_t calc = l_hal_crc32(s_esp_script_buf, hdr.length);
    if (calc != hdr.crc32) {
        if (out_status) *out_status = L_FLASH_ERR_CRC;
        return NULL;
    }
    if (out_len) *out_len = hdr.length;
    if (out_status) *out_status = L_FLASH_OK;
    return s_esp_script_buf;
#else
    ensure_sim_flash();
    LScriptHeader hdr;
    memcpy(&hdr, s_sim_flash, sizeof hdr);
    if (hdr.magic != L_SCRIPT_MAGIC_SRC && hdr.magic != L_SCRIPT_MAGIC_BC) {
        if (out_status) *out_status = L_FLASH_EMPTY;
        return NULL;
    }
    if (sizeof(LScriptHeader) + hdr.length + 1 > L_FLASH_PART_SIZE) {
        if (out_status) *out_status = L_FLASH_EMPTY;
        return NULL;
    }
    const char *body = (const char *)(s_sim_flash + sizeof(LScriptHeader));
    uint32_t calc = l_hal_crc32(body, hdr.length);
    if (calc != hdr.crc32) {
        if (out_status) *out_status = L_FLASH_ERR_CRC;
        return NULL;
    }
    if (out_len) *out_len = hdr.length;
    if (out_status) *out_status = L_FLASH_OK;
    return body;
#endif
}

/* ================= 9. PC 宿主串口扫描、一键烧录与远程终端 HAL ================= */

int l_hal_serial_list_ports(char names[][16], int max_ports) {
    int count = 0;
#if defined(_WIN32) && !L_PLATFORM_ESP_IDF
    char target[256];
    for (int i = 1; i <= 64 && count < max_ports; i++) {
        char com[16];
        snprintf(com, sizeof com, "COM%d", i);
        if (QueryDosDeviceA(com, target, sizeof target) > 0) {
            snprintf(names[count++], 16, "%s", com);
        }
    }
#elif !L_PLATFORM_ESP_IDF
    static const char *const prefixes[] = {
        "/dev/ttyUSB", "/dev/ttyACM", "/dev/cu.usbserial", "/dev/cu.usbmodem"
    };
    for (int p = 0; p < 4 && count < max_ports; p++) {
        for (int i = 0; i < 8 && count < max_ports; i++) {
            char dev[32];
            snprintf(dev, sizeof dev, "%s%d", prefixes[p], i);
            if (access(dev, F_OK) == 0) {
                snprintf(names[count++], 16, "%s", dev);
            }
        }
    }
#else
    (void)names; (void)max_ports;
#endif
    return count;
}

#if defined(_WIN32) && !L_PLATFORM_ESP_IDF
static HANDLE open_win_serial(const char *port, int baud, char *err_buf, size_t err_cap) {
    char dev_path[64];
    if (strncmp(port, "\\\\.\\", 4) == 0) {
        snprintf(dev_path, sizeof dev_path, "%s", port);
    } else {
        snprintf(dev_path, sizeof dev_path, "\\\\.\\%s", port);
    }
    HANDLE h = CreateFileA(dev_path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND)
            snprintf(err_buf, err_cap, "未找到串口 %s (请调用 ports() 查看可用串口, 或用 \"sim\" 进行离线模拟烧录)", port);
        else if (code == ERROR_ACCESS_DENIED)
            snprintf(err_buf, err_cap, "串口 %s 被其他程序占用, 请先关闭串口助手或监视器", port);
        else
            snprintf(err_buf, err_cap, "无法打开串口 %s (错误码 %lu)", port, (unsigned long)code);
        return INVALID_HANDLE_VALUE;
    }
    DCB dcb;
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    GetCommState(h, &dcb);
    dcb.BaudRate     = (DWORD)(baud > 0 ? baud : 115200);
    dcb.ByteSize     = 8;
    dcb.StopBits     = ONESTOPBIT;
    dcb.Parity       = NOPARITY;
    dcb.fBinary      = TRUE;
    dcb.fDtrControl  = DTR_CONTROL_DISABLE;
    dcb.fRtsControl  = RTS_CONTROL_DISABLE;
    SetCommState(h, &dcb);

    COMMTIMEOUTS to;
    to.ReadIntervalTimeout         = 10;
    to.ReadTotalTimeoutMultiplier  = 0;
    to.ReadTotalTimeoutConstant    = 20;
    to.WriteTotalTimeoutMultiplier = 1;
    to.WriteTotalTimeoutConstant   = 500;
    SetCommTimeouts(h, &to);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return h;
}
#endif

int l_hal_serial_upload(const char *port, int baud, const char *src, size_t len,
                        char *err_buf, size_t err_cap) {
    if (len == 0 || !src) {
        snprintf(err_buf, err_cap, "脚本内容为空");
        return -1;
    }

    /* 1. 规范化换行符为 '\n' (消除 Windows \r\n 传输中被截断或改变体积的问题) */
    char *clean_src = (char *)malloc(len + 1);
    size_t clean_len = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] != '\r') {
            clean_src[clean_len++] = src[i];
        }
    }
    clean_src[clean_len] = '\0';
    uint32_t crc = l_hal_crc32(clean_src, clean_len);

    /* 支持 "sim" / "loopback" 本地离线模拟演练烧录 */
    if (strcmp(port, "sim") == 0 || strcmp(port, "SIM") == 0 || strcmp(port, "loopback") == 0) {
        LFlashStatus st = l_hal_flash_write_script(clean_src, clean_len, 1u);
        free(clean_src);
        if (st != L_FLASH_OK) {
            snprintf(err_buf, err_cap, "写入模拟 Flash 分区 l_script 失败(状态码 %d)", (int)st);
            return -1;
        }
        printf("[upload:sim] 已向裸分区 %s 烧录 %zu 字节, CRC32=0x%08X (校验通过)\n",
               L_FLASH_PART_NAME, clean_len, (unsigned)crc);
        return 1;
    }

#if defined(_WIN32) && !L_PLATFORM_ESP_IDF
    HANDLE h = open_win_serial(port, baud, err_buf, err_cap);
    if (h == INVALID_HANDLE_VALUE) {
        free(clean_src);
        return -1;
    }

    printf("[upload] 已连接 %s (%d bps), 准备烧录 %zu 字节 (CRC32=0x%08X)...\n",
           port, baud, clean_len, (unsigned)crc);

    /* 2. 清空串口并发送一个 Ctrl+C，确保 ESP32 从任何死循环中退出到提示符 */
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    DWORD written = 0;
    WriteFile(h, "\x03\r\n", 3, &written, NULL);
    Sleep(80);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    /* 3. 发送烧录帧起始命令 (仅以 '\n' 结尾: ESP-IDF 串口控制台会把 '\r'
     *    翻译为 '\n', "\r\n" 结尾会在设备端残留一个 '\n' 混入正文致 CRC 失配) */
    char hdr_cmd[128];
    int hdr_len = snprintf(hdr_cmd, sizeof hdr_cmd,
                           "__L_UPLOAD_BEGIN__ %zu 0x%08X 1\n", clean_len, (unsigned)crc);
    WriteFile(h, hdr_cmd, (DWORD)hdr_len, &written, NULL);

    /* 4. 等待 ESP32 就绪握手 __L_UPLOAD_READY__ (最多等待 2 秒) */
    char rx_buf[2048];
    size_t rx_total = 0;
    int ready = 0;
    for (int tick = 0; tick < 100; tick++) {
        DWORD r = 0;
        if (ReadFile(h, rx_buf + rx_total, (DWORD)(sizeof(rx_buf) - 1 - rx_total), &r, NULL) && r > 0) {
            rx_total += (size_t)r;
            rx_buf[rx_total] = '\0';
            if (strstr(rx_buf, "__L_UPLOAD_READY__")) {
                ready = 1;
                break;
            }
        }
        Sleep(20);
    }
    if (!ready) {
        Sleep(100);
    }

    /* 5. 分块发送脚本源码: 每块 64 字节，休眠 6ms，确保 MCU 串口硬件接收绝不丢包 */
    size_t sent = 0;
    while (sent < clean_len) {
        size_t chunk = (clean_len - sent > 64) ? 64 : (clean_len - sent);
        DWORD w = 0;
        if (!WriteFile(h, clean_src + sent, (DWORD)chunk, &w, NULL) || w == 0) {
            CloseHandle(h);
            free(clean_src);
            snprintf(err_buf, err_cap, "串口发送中断 (已发 %zu/%zu 字节)", sent, clean_len);
            return -1;
        }
        sent += (size_t)w;
        Sleep(6);
    }
    free(clean_src);

    /* 6. 循环读取板子响应 (等待 __L_UPLOAD_OK__ 或 __L_UPLOAD_ERR__) */
    rx_total = 0;
    int ok = 0;
    for (int tick = 0; tick < 150; tick++) { /* 最多等待约 3 秒 */
        DWORD r = 0;
        if (ReadFile(h, rx_buf + rx_total, (DWORD)(sizeof(rx_buf) - 1 - rx_total), &r, NULL) && r > 0) {
            rx_total += (size_t)r;
            rx_buf[rx_total] = '\0';
            if (strstr(rx_buf, "__L_UPLOAD_OK__")) {
                ok = 1;
                break;
            }
            if (strstr(rx_buf, "__L_UPLOAD_ERR__")) {
                CloseHandle(h);
                snprintf(err_buf, err_cap, "ESP32 端报告烧录/CRC 校验失败: %s", rx_buf);
                return -1;
            }
        }
        Sleep(20);
    }
    CloseHandle(h);

    if (!ok) {
        snprintf(err_buf, err_cap, "ESP32 端响应超时 (未收到 __L_UPLOAD_OK__)，请确认固件已升级至最新版本");
        return -1;
    }

    printf("[upload] 烧录成功! ESP32 已校验 CRC32 并热重载执行脚本。\n");
    return 0;
#else
    (void)baud;
    snprintf(err_buf, err_cap, "当前平台暂未启用物理串口 %s (可使用 \"sim\" 模拟烧录)", port);
    return -1;
#endif
}

#if defined(_WIN32) && !L_PLATFORM_ESP_IDF
/* ---- 串口终端日志辅助 (仅 PC 宿主端使用) ---- */
static FILE *s_mon_log = NULL;        /* 日志文件句柄, NULL 表示未启用 */
static char  s_log_line[1024];        /* 正在积累的当前行 */
static size_t s_log_len = 0;

/* 把当前积累行连同本地时间戳写入日志文件 */
static void mon_log_flush(const char *prefix) {
    if (!s_mon_log || s_log_len == 0) return;
    time_t now = time(NULL);
    struct tm tmv;
    localtime_s(&tmv, &now);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);
    fprintf(s_mon_log, "[%s] %s%.*s\n", ts, prefix, (int)s_log_len, s_log_line);
    fflush(s_mon_log); /* 立即落盘, 断电/崩溃也不丢 */
    s_log_len = 0;
}

/* 处理一段设备收到的数据: 按 '\n' 分行, 逐行带时间戳记录 */
static void mon_log_rx(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n') {
            mon_log_flush("");
        } else if (c == '\r') {
            /* 以 '\n' 为行界, 忽略 '\r' */
        } else if (s_log_len + 1 < sizeof(s_log_line)) {
            s_log_line[s_log_len++] = c;
        }
    }
}

/* 记录一条 PC 发往设备的内容 (prefix 形如 "> " 或 "<Ctrl+C>") */
static void mon_log_tx(const char *prefix, const char *text, size_t len) {
    if (!s_mon_log) return;
    s_log_len = 0;
    size_t n = (len < sizeof(s_log_line) - 1) ? len : sizeof(s_log_line) - 1;
    memcpy(s_log_line, text, n);
    s_log_len = n;
    mon_log_flush(prefix);
}
#endif

int l_hal_serial_monitor(const char *port, int baud, const char *log_path,
                         char *err_buf, size_t err_cap) {
#if defined(_WIN32) && !L_PLATFORM_ESP_IDF
    HANDLE h = open_win_serial(port, baud, err_buf, err_cap);
    if (h == INVALID_HANDLE_VALUE) return -1;

    /* 打开日志文件 (追加模式, 便于跨会话持续记录) */
    if (log_path && *log_path) {
        s_mon_log = fopen(log_path, "ab");
        if (!s_mon_log) {
            snprintf(err_buf, err_cap, "无法打开日志文件: %s", log_path);
            CloseHandle(h);
            return -1;
        }
        {
            char meta[128];
            int m = snprintf(meta, sizeof meta, "%s @ %d bps (日志: %s)", port, baud, log_path);
            mon_log_tx("== 会话开始 ", meta, (size_t)m);
        }
    }

    printf("=== 已连接 ESP32 串口交互终端 (%s @ %d bps) ===\n", port, baud);
    if (s_mon_log) printf("=== 日志记录中: %s (Ctrl+] 或 .exit 退出) ===\n", log_path);
    printf("提示: 直接输入 L 语言语句回车即可在 ESP32 上执行; 按 Ctrl+] 或输入 .exit 回车退出终端。\n\n");

    DWORD w = 0;
    WriteFile(h, "\r\n", 2, &w, NULL);

    char exit_buf[16];
    int  exit_len = 0;
    char rx[256];
    char tx_line[256];   /* 记录用户本次输入的整行 (用于日志) */
    int  tx_len = 0;

    for (;;) {
        DWORD r = 0;
        if (ReadFile(h, rx, sizeof(rx) - 1, &r, NULL) && r > 0) {
            rx[r] = '\0';
            printf("%s", rx);
            fflush(stdout);
            mon_log_rx(rx, (size_t)r);
        }
        while (_kbhit()) {
            int ch = _getch();
            if (ch == 0x1D) { /* Ctrl+] 立即退出 */
                mon_log_rx("\n", 1); /* 把未完结的行先落盘 */
                mon_log_tx("== 会话结束 ", port, strlen(port));
                if (s_mon_log) { fclose(s_mon_log); s_mon_log = NULL; }
                printf("\n[已断开串口连接 %s]\n", port);
                CloseHandle(h);
                return 0;
            } else if (ch == '\r' || ch == '\n') {
                char cr = '\r';
                WriteFile(h, &cr, 1, &w, NULL);
                tx_line[tx_len] = '\0';
                mon_log_tx("> ", tx_line, (size_t)tx_len);
                tx_len = 0;
                exit_buf[exit_len] = '\0';
                if (strcmp(exit_buf, ".exit") == 0 || strcmp(exit_buf, ".quit") == 0) {
                    if (s_mon_log) { fclose(s_mon_log); s_mon_log = NULL; }
                    printf("\n[已断开串口连接 %s]\n", port);
                    CloseHandle(h);
                    return 0;
                }
                exit_len = 0;
            } else if (ch == '\b' || ch == 127) {
                char bs = '\b';
                WriteFile(h, &bs, 1, &w, NULL);
                if (exit_len > 0) exit_len--;
                if (tx_len > 0) tx_len--;
            } else if (ch == 3) { /* Ctrl+C 信号 */
                char c = 3;
                WriteFile(h, &c, 1, &w, NULL);
                mon_log_tx("<Ctrl+C>", "", 0);
                tx_len = 0;
                exit_len = 0;
            } else {
                char c = (char)ch;
                WriteFile(h, &c, 1, &w, NULL);
                if (tx_len + 1 < (int)sizeof(tx_line)) {
                    tx_line[tx_len++] = c;
                }
                if (exit_len < (int)sizeof(exit_buf) - 2) {
                    exit_buf[exit_len++] = c;
                }
            }
        }
        Sleep(2);
    }
#else
    (void)baud;
    snprintf(err_buf, err_cap, "无法打开串口终端 %s", port);
    return -1;
#endif
}

int l_hal_serial_exec(const char *port, int baud, const char *code, char *err_buf, size_t err_cap) {
#if defined(_WIN32) && !L_PLATFORM_ESP_IDF
    HANDLE h = open_win_serial(port, baud, err_buf, err_cap);
    if (h == INVALID_HANDLE_VALUE) return -1;

    /* 清空残留输入输出缓冲 */
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    /* 激活 ESP32 终端 */
    DWORD w = 0;
    WriteFile(h, "\r\n", 2, &w, NULL);
    Sleep(60);

    /* 发送用户代码 */
    size_t code_len = strlen(code);
    WriteFile(h, code, (DWORD)code_len, &w, NULL);
    WriteFile(h, "\r\n", 2, &w, NULL);

    /* 接收回显并打印到控制台，等待命令执行完毕 */
    char rx[256];
    DWORD start_t = GetTickCount();
    int got_data = 0;

    while (GetTickCount() - start_t < 2500) {
        DWORD r = 0;
        if (ReadFile(h, rx, sizeof(rx) - 1, &r, NULL) && r > 0) {
            rx[r] = '\0';
            printf("%s", rx);
            fflush(stdout);
            got_data = 1;
            /* 收到返回的 prompt 说明执行结束 */
            if (strstr(rx, "esp32>>>") != NULL) {
                break;
            }
        } else {
            Sleep(10);
            if (got_data && (GetTickCount() - start_t > 800)) {
                break;
            }
        }
    }
    printf("\n");
    CloseHandle(h);
    return 0;
#else
    (void)port; (void)baud; (void)code;
    snprintf(err_buf, err_cap, "当前平台不支持串口通信");
    return -1;
#endif
}

/* ================= 12. WiFi 网络与热点 HAL 实现 ================= */

#if L_PLATFORM_ESP_IDF

static EventGroupHandle_t s_wifi_event_group = NULL;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif  = NULL;
static int          s_wifi_inited = 0;
static int          s_wifi_connected = 0;
static char         s_sta_ip_str[32] = {0};

/* 最近一次 WiFi 操作失败的详细诊断信息 (供 l_hal_wifi_last_error() 查询) */
static char s_wifi_last_err[192] = "";

static void wifi_clear_last_err(void) {
    s_wifi_last_err[0] = '\0';
}

/* 将 ESP-IDF 断开原因码翻译为可读的中文诊断描述 */
static const char *wifi_reason_str(int reason) {
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:           return "认证超时, 密码可能已过期或错误";
    case WIFI_REASON_AUTH_LEAVE:            return "认证被主动离开";
    case WIFI_REASON_ASSOC_EXPIRE:          return "关联超时";
    case WIFI_REASON_AUTH_FAIL:             return "认证失败: 密码错误或加密方式不匹配";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:return "四次握手超时: WiFi 密码错误 (最常见原因)";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:     return "握手超时: 信号过弱或路由器无响应";
    case WIFI_REASON_NO_AP_FOUND:           return "未找到该热点: SSID 拼写错误、路由器关闭或为隐藏网络";
    case WIFI_REASON_ASSOC_FAIL:            return "关联失败: 信号太弱或路由器拒绝连接";
    case WIFI_REASON_BEACON_TIMEOUT:        return "信标超时: 信号不稳定, 靠近路由器重试";
    case WIFI_REASON_ASSOC_TOOMANY:         return "路由器已满: 连接设备数达到上限";
#ifdef WIFI_REASON_AP_CANNOT_HANDLE_NEW_STA
    case WIFI_REASON_AP_CANNOT_HANDLE_NEW_STA: return "路由器无法接受新设备"; /* 旧版 IDF 无此枚举, 值与 ASSOC_TOOMANY 相同 */
#endif
    case WIFI_REASON_CONNECTION_FAIL:       return "连接失败: 射频干扰或信道拥挤";
    default:                                return "未知原因";
    }
}

/* 断线自动重连计数 (在 wifi_connect 显式发起连接时清零) */
#define WIFI_RETRY_MAX 10
static int s_wifi_retry = 0;
/* 最近一次断开的 reason 码 (-1 表示无) 与获取到的 IP (网络字节序)。
 * 事件回调运行在 sys_evt 任务 (默认栈仅 2KB), 回调内严禁 printf/snprintf
 * 等重栈操作, 只做赋值与事件位标记; 诊断字符串一律由任务上下文格式化 */
static volatile int      s_wifi_disc_reason = -1;
static volatile uint32_t s_sta_ip_u32 = 0;

/* 把网络字节序 IPv4 格式化为点分十进制字符串 */
static void wifi_ip_to_str(uint32_t ip, char *buf, size_t cap) {
    snprintf(buf, cap, "%u.%u.%u.%u",
             (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
             (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        s_wifi_disc_reason = (int)disc->reason;
        s_wifi_connected = 0;
        s_sta_ip_u32 = 0;
        s_sta_ip_str[0] = '\0';
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        /* 有限次自动重连: reason=2/15 (认证失败/密码错) 等不可恢复错误不重试,
         * 避免在路由器后台表现为无限连接/断开循环 */
        if (s_wifi_retry < WIFI_RETRY_MAX &&
            disc->reason != WIFI_REASON_AUTH_FAIL && disc->reason != WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
            s_wifi_retry++;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_sta_ip_u32 = event->ip_info.ip.addr;
        s_wifi_connected = 1;
        s_wifi_disc_reason = -1;
        s_wifi_retry = 0;
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

static void ensure_wifi_base_inited(void) {
    if (!s_wifi_inited) {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            nvs_flash_init();
        }
        esp_netif_init();
        esp_event_loop_create_default();
        s_wifi_event_group = xEventGroupCreate();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);

        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

        s_wifi_inited = 1;
    }
}

int l_hal_wifi_connect(const char *ssid, const char *password, int timeout_ms) {
    wifi_clear_last_err();
    if (!ssid || !*ssid) {
        snprintf(s_wifi_last_err, sizeof(s_wifi_last_err), "SSID 不能为空, 请传入有效热点名称");
        return -1;
    }
    if (password && strlen(password) > 0 && strlen(password) < 8) {
        snprintf(s_wifi_last_err, sizeof(s_wifi_last_err),
                 "WiFi 密码长度为 %u 位, WPA/WPA2 加密要求至少 8 位 (开放网络请传空密码)",
                 (unsigned)strlen(password));
        return -1;
    }
    ensure_wifi_base_inited();

    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = (password && *password) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    /* 声明支持 PMF(802.11w): WPA2/WPA3 混合模式路由器(常见于运营商网关)会拒绝
     * 不支持 PMF 的客户端, 导致设备反复认证/被踢, 路由器后台表现为连接-断开循环 */
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    /* WPA3-SAE 兼容: 运营商网关(如 CMCC)常用 WPA2/WPA3 混合或 WPA3 H2E 模式 */
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    /* 全信道扫描并按信号强度择优: 单信道快扫在信道拥挤时可能找不到目标 AP */
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    s_wifi_retry = 0;
    s_wifi_disc_reason = -1;
    esp_err_t em = esp_wifi_set_mode(WIFI_MODE_STA);
    if (em != ESP_OK) {
        snprintf(s_wifi_last_err, sizeof(s_wifi_last_err),
                 "esp_wifi_set_mode(STA) 失败 (%s), WiFi 协议栈初始化异常", esp_err_to_name(em));
        return -1;
    }
    esp_err_t ec = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ec != ESP_OK) {
        snprintf(s_wifi_last_err, sizeof(s_wifi_last_err),
                 "esp_wifi_set_config 失败 (%s), 请检查 SSID/密码长度是否超限", esp_err_to_name(ec));
        return -1;
    }
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    esp_wifi_start();
    /* 关闭默认的 modem sleep 省电模式: 部分路由器与之不兼容, 会导致周期性掉线 */
    esp_wifi_set_ps(WIFI_PS_NONE);

    /* STA_START 事件只在首次 esp_wifi_start() 时触发 (事件回调里会自动 esp_wifi_connect)。
     * 若 WiFi 已处于启动状态 (如上次连接失败/断开后再次调用本函数), STA_START 不会重发,
     * 必须在此显式发起连接, 否则 15 秒内不会有任何事件, 表现为无诊断信息的静默超时 */
    esp_wifi_connect();

    int wait_ticks = (timeout_ms > 0 ? timeout_ms : 15000) / portTICK_PERIOD_MS;
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                          pdFALSE, pdFALSE, wait_ticks);

    if (bits & WIFI_CONNECTED_BIT) {
        /* 成功: 在任务上下文把缓存的 IP 格式化为字符串 (回调内禁止 snprintf) */
        wifi_ip_to_str(s_sta_ip_u32, s_sta_ip_str, sizeof(s_sta_ip_str));
        return 0;
    }
    /* 失败: 在任务上下文格式化诊断信息, 区分 "收到断开事件 (带 reason)" 与 "单纯超时" */
    if (bits & WIFI_FAIL_BIT) {
        if (s_wifi_disc_reason >= 0) {
            snprintf(s_wifi_last_err, sizeof(s_wifi_last_err),
                     "STA 断开 (reason=%d: %s)", s_wifi_disc_reason, wifi_reason_str(s_wifi_disc_reason));
        } else {
            snprintf(s_wifi_last_err, sizeof(s_wifi_last_err), "连接被路由器拒绝");
        }
    } else if (s_wifi_last_err[0] == '\0') {
        snprintf(s_wifi_last_err, sizeof(s_wifi_last_err),
                 "连接超时: %dms 内未获取到 IP, 请检查信号强度、密码是否正确, 并用 wifi_scan() 确认热点存在",
                 timeout_ms > 0 ? timeout_ms : 15000);
    }
    return -1;
}

const char *l_hal_wifi_last_error(void) {
    /* 连接成功返回后若再次断开 (后台自动重连期间), 此处按缓存的 reason 补全诊断 */
    if (!s_wifi_connected && s_wifi_disc_reason >= 0 && s_wifi_last_err[0] == '\0') {
        snprintf(s_wifi_last_err, sizeof(s_wifi_last_err),
                 "STA 断开 (reason=%d: %s)", s_wifi_disc_reason, wifi_reason_str(s_wifi_disc_reason));
    }
    return s_wifi_last_err;
}

int l_hal_wifi_disconnect(void) {
    if (!s_wifi_inited) return 0;
    s_wifi_connected = 0;
    s_sta_ip_str[0] = '\0';
    return (esp_wifi_disconnect() == ESP_OK) ? 0 : -1;
}

int l_hal_wifi_is_connected(void) {
    return s_wifi_connected;
}

int l_hal_wifi_get_ssid(char *buf, size_t max_len) {
    if (!buf || max_len == 0) return -1;
    buf[0] = '\0';
    if (!s_wifi_connected) return -1;
    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
        strncpy(buf, (const char *)conf.sta.ssid, max_len - 1);
        buf[max_len - 1] = '\0';
        return 0;
    }
    return -1;
}

int l_hal_wifi_get_ip(char *buf, size_t max_len) {
    if (!buf || max_len == 0) return -1;
    if (s_wifi_connected && s_sta_ip_u32) {
        wifi_ip_to_str(s_sta_ip_u32, buf, max_len);
        return 0;
    }
    buf[0] = '\0';
    return -1;
}

int l_hal_wifi_get_mac(char *buf, size_t max_len) {
    if (!buf || max_len < 18) return -1;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, max_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

int l_hal_wifi_start_ap(const char *ssid, const char *password) {
    wifi_clear_last_err();
    if (!ssid || !*ssid) {
        snprintf(s_wifi_last_err, sizeof(s_wifi_last_err), "热点名称(SSID)不能为空");
        return -1;
    }
    if (password && *password && strlen(password) < 8) {
        snprintf(s_wifi_last_err, sizeof(s_wifi_last_err),
                 "热点密码长度为 %u 位, WPA2 加密要求至少 8 位 (无加密热点请传 nil 或空串)",
                 (unsigned)strlen(password));
        return -1;
    }
    ensure_wifi_base_inited();

    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(ssid);
    if (password && strlen(password) >= 8) {
        strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    wifi_config.ap.max_connection = 4;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        snprintf(s_wifi_last_err, sizeof(s_wifi_last_err),
                 "设置 AP 模式失败: %s", esp_err_to_name(err));
        return -1;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        snprintf(s_wifi_last_err, sizeof(s_wifi_last_err),
                 "配置热点参数失败: %s", esp_err_to_name(err));
        return -1;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        snprintf(s_wifi_last_err, sizeof(s_wifi_last_err),
                 "启动 SoftAP 失败: %s", esp_err_to_name(err));
        return -1;
    }
    return 0;
}

int l_hal_wifi_scan(LWifiApRecord *records, int max_records) {
    if (!records || max_records <= 0) return 0;
    ensure_wifi_base_inited();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    wifi_scan_config_t scan_cfg = {0};
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) return -1;

    uint16_t ap_count = (uint16_t)max_records;
    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * max_records);
    if (!ap_list) return -1;

    if (esp_wifi_scan_get_ap_records(&ap_count, ap_list) != ESP_OK) {
        free(ap_list);
        return -1;
    }

    for (int i = 0; i < (int)ap_count; i++) {
        strncpy(records[i].ssid, (char *)ap_list[i].ssid, sizeof(records[i].ssid) - 1);
        records[i].ssid[sizeof(records[i].ssid) - 1] = '\0';
        records[i].rssi = ap_list[i].rssi;
        records[i].authmode = (uint8_t)ap_list[i].authmode;
    }

    free(ap_list);
    return (int)ap_count;
}

/* ================= 13. HTTP 客户端与 SNTP 网络对时 HAL 实现 (ESP-IDF) ================= */

typedef struct {
    char   *buf;
    size_t  len;
    size_t  cap;
} HttpRespBuffer;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    HttpRespBuffer *resp = (HttpRespBuffer *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (!esp_http_client_is_chunked_response(evt->client)) {
            if (resp && evt->data && evt->data_len > 0) {
                if (resp->len + evt->data_len + 1 > resp->cap) {
                    size_t new_cap = (resp->len + evt->data_len + 1) * 2;
                    char *new_buf = realloc(resp->buf, new_cap);
                    if (new_buf) {
                        resp->buf = new_buf;
                        resp->cap = new_cap;
                    }
                }
                if (resp->buf && resp->len + evt->data_len < resp->cap) {
                    memcpy(resp->buf + resp->len, evt->data, evt->data_len);
                    resp->len += evt->data_len;
                    resp->buf[resp->len] = '\0';
                }
            }
        }
    }
    return ESP_OK;
}

LHttpResponse l_hal_http_get(const char *url, const char *headers, int timeout_ms) {
    (void)headers;
    LHttpResponse res = {-1, NULL, 0, NULL};
    HttpRespBuffer buf = {NULL, 0, 0};
    buf.cap = 1024;
    buf.buf = malloc(buf.cap);
    if (buf.buf) buf.buf[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .user_data = &buf,
        .timeout_ms = timeout_ms > 0 ? timeout_ms : 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(buf.buf);
        res.err_msg = strdup("初始化 HTTP 客户端失败");
        return res;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        res.status_code = esp_http_client_get_status_code(client);
        res.body = buf.buf;
        res.body_len = buf.len;
    } else {
        free(buf.buf);
        res.err_msg = strdup(esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return res;
}

LHttpResponse l_hal_http_post(const char *url, const char *data, size_t data_len, const char *headers, int timeout_ms) {
    (void)headers;
    LHttpResponse res = {-1, NULL, 0, NULL};
    HttpRespBuffer buf = {NULL, 0, 0};
    buf.cap = 1024;
    buf.buf = malloc(buf.cap);
    if (buf.buf) buf.buf[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .user_data = &buf,
        .timeout_ms = timeout_ms > 0 ? timeout_ms : 10000,
        .method = HTTP_METHOD_POST,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(buf.buf);
        res.err_msg = strdup("初始化 HTTP 客户端失败");
        return res;
    }

    if (data && data_len > 0) {
        esp_http_client_set_post_field(client, data, (int)data_len);
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        res.status_code = esp_http_client_get_status_code(client);
        res.body = buf.buf;
        res.body_len = buf.len;
    } else {
        free(buf.buf);
        res.err_msg = strdup(esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return res;
}

int l_hal_sntp_sync(const char *ntp_server, int timeout_ms) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, (ntp_server && *ntp_server) ? ntp_server : "pool.ntp.org");
    esp_sntp_init();

    int waited_ms = 0;
    int step_ms = 100;
    int max_wait = timeout_ms > 0 ? timeout_ms : 10000;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && waited_ms < max_wait) {
        vTaskDelay(step_ms / portTICK_PERIOD_MS);
        waited_ms += step_ms;
    }
    return (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) ? 0 : -1;
}

#else /* ================= PC 宿主端 (Windows / Linux) 网络与模拟实现 ================= */

static int  s_sim_wifi_connected = 0; /* 默认未连接 WiFi */
static char s_sim_wifi_ssid[33]  = "";
static char s_sim_wifi_ip[32]    = "";
static char s_sim_wifi_last_err[192] = ""; /* 最近一次 WiFi 操作失败诊断 (PC 模拟) */

static void ensure_pc_host_ip(void) {
#if defined(_WIN32)
    static int s_wsa_inited = 0;
    if (!s_wsa_inited) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
            s_wsa_inited = 1;
        }
    }
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct hostent *he = gethostbyname(hostname);
        if (he && he->h_addr_list && he->h_addr_list[0]) {
            struct in_addr addr;
            memcpy(&addr, he->h_addr_list[0], sizeof(struct in_addr));
            strncpy(s_sim_wifi_ip, inet_ntoa(addr), sizeof(s_sim_wifi_ip) - 1);
            s_sim_wifi_ip[sizeof(s_sim_wifi_ip) - 1] = '\0';
        }
    }
#endif
}

int l_hal_wifi_connect(const char *ssid, const char *password, int timeout_ms) {
    (void)password; (void)timeout_ms;
    s_sim_wifi_last_err[0] = '\0';
    if (!ssid || !*ssid) {
        snprintf(s_sim_wifi_last_err, sizeof(s_sim_wifi_last_err), "SSID 不能为空, 请传入有效热点名称");
        return -1;
    }
    if (password && *password && strlen(password) < 8) {
        snprintf(s_sim_wifi_last_err, sizeof(s_sim_wifi_last_err),
                 "WiFi 密码长度为 %u 位, WPA/WPA2 加密要求至少 8 位",
                 (unsigned)strlen(password));
        return -1;
    }
    strncpy(s_sim_wifi_ssid, ssid, sizeof(s_sim_wifi_ssid) - 1);
    s_sim_wifi_ssid[sizeof(s_sim_wifi_ssid) - 1] = '\0';
    s_sim_wifi_connected = 1;
    ensure_pc_host_ip();
    return 0;
}

int l_hal_wifi_disconnect(void) {
    s_sim_wifi_connected = 0;
    return 0;
}

int l_hal_wifi_is_connected(void) {
    return s_sim_wifi_connected;
}

int l_hal_wifi_get_ssid(char *buf, size_t max_len) {
    if (!buf || max_len == 0) return -1;
    if (!s_sim_wifi_connected || !s_sim_wifi_ssid[0]) {
        buf[0] = '\0';
        return -1;
    }
    strncpy(buf, s_sim_wifi_ssid, max_len - 1);
    buf[max_len - 1] = '\0';
    return 0;
}

int l_hal_wifi_get_ip(char *buf, size_t max_len) {
    if (!buf || max_len == 0) return -1;
    if (!s_sim_wifi_connected) {
        buf[0] = '\0';
        return -1;
    }
    ensure_pc_host_ip();
    strncpy(buf, s_sim_wifi_ip, max_len - 1);
    buf[max_len - 1] = '\0';
    return 0;
}

int l_hal_wifi_get_mac(char *buf, size_t max_len) {
    if (!buf || max_len < 18) return -1;
    strncpy(buf, "24:0A:C4:B8:31:AA", max_len - 1);
    buf[max_len - 1] = '\0';
    return 0;
}

int l_hal_wifi_start_ap(const char *ssid, const char *password) {
    (void)password;
    s_sim_wifi_last_err[0] = '\0';
    if (!ssid || !*ssid) {
        snprintf(s_sim_wifi_last_err, sizeof(s_sim_wifi_last_err), "热点名称(SSID)不能为空");
        return -1;
    }
    if (password && *password && strlen(password) < 8) {
        snprintf(s_sim_wifi_last_err, sizeof(s_sim_wifi_last_err),
                 "热点密码长度为 %u 位, WPA2 加密要求至少 8 位",
                 (unsigned)strlen(password));
        return -1;
    }
    strncpy(s_sim_wifi_ssid, ssid, sizeof(s_sim_wifi_ssid) - 1);
    s_sim_wifi_ssid[sizeof(s_sim_wifi_ssid) - 1] = '\0';
    strncpy(s_sim_wifi_ip, "192.168.4.1", sizeof(s_sim_wifi_ip) - 1);
    s_sim_wifi_connected = 1;
    return 0;
}

const char *l_hal_wifi_last_error(void) {
    return s_sim_wifi_last_err;
}

int l_hal_wifi_scan(LWifiApRecord *records, int max_records) {
    if (!records || max_records <= 0) return 0;
    int count = 0;

    snprintf(records[count].ssid, sizeof(records[count].ssid), "%s", s_sim_wifi_ssid);
    records[count].rssi = -45;
    records[count].authmode = 3;
    count++;

    if (count < max_records) {
        strncpy(records[count].ssid, "Office_5G", sizeof(records[count].ssid) - 1);
        records[count].rssi = -60;
        records[count].authmode = 3;
        count++;
    }
    if (count < max_records) {
        strncpy(records[count].ssid, "Guest_WiFi", sizeof(records[count].ssid) - 1);
        records[count].rssi = -78;
        records[count].authmode = 0;
        count++;
    }
    return count;
}

#if defined(_WIN32)
LHttpResponse l_hal_http_get(const char *url, const char *headers, int timeout_ms) {
    (void)headers;
    LHttpResponse res = {-1, NULL, 0, NULL};
    HINTERNET hInternet = InternetOpenA("LLang/0.1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        res.err_msg = strdup("无法初始化网络连接 (InternetOpen 失败)");
        return res;
    }

    DWORD timeout = timeout_ms > 0 ? (DWORD)timeout_ms : 10000;
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
    HINTERNET hUrl = InternetOpenUrlA(hInternet, url, NULL, 0, flags, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        res.err_msg = strdup("打开 URL 失败 (网络不可达或 DNS 解析错误)");
        return res;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, NULL)) {
        res.status_code = (int)statusCode;
    } else {
        res.status_code = 200;
    }

    size_t cap = 2048, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        res.err_msg = strdup("内存不足");
        return res;
    }

    DWORD bytesRead = 0;
    char chunk[1024];
    while (InternetReadFile(hUrl, chunk, sizeof(chunk), &bytesRead) && bytesRead > 0) {
        if (len + bytesRead + 1 > cap) {
            cap = (len + bytesRead + 1) * 2;
            char *new_buf = realloc(buf, cap);
            if (!new_buf) break;
            buf = new_buf;
        }
        memcpy(buf + len, chunk, bytesRead);
        len += bytesRead;
    }
    buf[len] = '\0';
    res.body = buf;
    res.body_len = len;

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return res;
}

LHttpResponse l_hal_http_post(const char *url, const char *data, size_t data_len, const char *headers, int timeout_ms) {
    LHttpResponse res = {-1, NULL, 0, NULL};
    HINTERNET hInternet = InternetOpenA("LLang/0.1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        res.err_msg = strdup("无法初始化网络连接");
        return res;
    }

    DWORD timeout = timeout_ms > 0 ? (DWORD)timeout_ms : 10000;
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));

    char host[256] = {0};
    char path[1024] = {0};
    URL_COMPONENTSA comp;
    memset(&comp, 0, sizeof(comp));
    comp.dwStructSize = sizeof(comp);
    comp.lpszHostName = host;
    comp.dwHostNameLength = sizeof(host);
    comp.lpszUrlPath = path;
    comp.dwUrlPathLength = sizeof(path);

    if (!InternetCrackUrlA(url, 0, 0, &comp)) {
        InternetCloseHandle(hInternet);
        res.err_msg = strdup("无效的 URL 格式");
        return res;
    }

    HINTERNET hConnect = InternetConnectA(hInternet, host, comp.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        res.err_msg = strdup("连接主机失败");
        return res;
    }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    if (comp.nScheme == INTERNET_SCHEME_HTTPS) {
        flags |= INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
    }

    HINTERNET hReq = HttpOpenRequestA(hConnect, "POST", path[0] ? path : "/", NULL, NULL, NULL, flags, 0);
    if (!hReq) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        res.err_msg = strdup("创建 HTTP POST 请求失败");
        return res;
    }

    const char *default_hdr = headers ? headers : "Content-Type: application/x-www-form-urlencoded\r\n";
    BOOL sent = HttpSendRequestA(hReq, default_hdr, (DWORD)strlen(default_hdr), (LPVOID)data, (DWORD)data_len);
    if (!sent) {
        InternetCloseHandle(hReq);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        res.err_msg = strdup("发送 HTTP POST 失败");
        return res;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (HttpQueryInfoA(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, NULL)) {
        res.status_code = (int)statusCode;
    } else {
        res.status_code = 200;
    }

    size_t cap = 2048, len = 0;
    char *buf = malloc(cap);
    DWORD bytesRead = 0;
    char chunk[1024];
    while (InternetReadFile(hReq, chunk, sizeof(chunk), &bytesRead) && bytesRead > 0) {
        if (len + bytesRead + 1 > cap) {
            cap = (len + bytesRead + 1) * 2;
            char *new_buf = realloc(buf, cap);
            if (!new_buf) break;
            buf = new_buf;
        }
        memcpy(buf + len, chunk, bytesRead);
        len += bytesRead;
    }
    buf[len] = '\0';
    res.body = buf;
    res.body_len = len;

    InternetCloseHandle(hReq);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return res;
}
#else
LHttpResponse l_hal_http_get(const char *url, const char *headers, int timeout_ms) {
    (void)url; (void)headers; (void)timeout_ms;
    LHttpResponse res = {200, strdup("OK (Host Linux Sim)"), 20, NULL};
    return res;
}
LHttpResponse l_hal_http_post(const char *url, const char *data, size_t data_len, const char *headers, int timeout_ms) {
    (void)url; (void)data; (void)data_len; (void)headers; (void)timeout_ms;
    LHttpResponse res = {200, strdup("OK (Host Linux Sim)"), 20, NULL};
    return res;
}
#endif

int l_hal_sntp_sync(const char *ntp_server, int timeout_ms) {
    (void)ntp_server; (void)timeout_ms;
    return 0; /* PC 宿主端时钟由操作系统同步，默认已处于精准同步状态 */
}

#endif /* L_PLATFORM_ESP_IDF */

/* ================= 14. M7: GPIO 中断 / 软件定时器 / 看门狗 / 深度睡眠 HAL ================= */

#if L_PLATFORM_ESP_IDF

/* 真机实现: ISR 只做入队(FreeRTOS Queue), 由 l_hal_poll_events() 在任务上下文派发 */

#define L_EVQ_LEN 32
typedef struct { int kind; int a, b; } LEv;   /* kind: 0=irq(pin=a,level=b) 1=timer(id=a) */
static QueueHandle_t s_evq = NULL;

static void ensure_evq(void) {
    if (!s_evq) s_evq = xQueueCreate(L_EVQ_LEN, sizeof(LEv));
}

static void ev_push_isr(int kind, int a, int b) {
    ensure_evq();
    if (!s_evq) return;
    LEv ev = { kind, a, b };
    BaseType_t hpw = pdFALSE;
    xQueueSendFromISR(s_evq, &ev, &hpw);
}

static void ev_push_task(int kind, int a, int b) {
    ensure_evq();
    if (!s_evq) return;
    LEv ev = { kind, a, b };
    xQueueSend(s_evq, &ev, 0);
}

static struct {
    int      mode;
    LIrqFn   cb;
    void    *user;
} s_irq_tbl[L_MAX_GPIO_PIN + 1];

static void gpio_isr_thunk(void *arg) {
    int pin = (int)(intptr_t)arg;
    int level = gpio_get_level((gpio_num_t)pin);
    ev_push_isr(0, pin, level);
}

int l_hal_gpio_irq_attach(int pin, int mode, LIrqFn cb, void *user) {
    if (pin < 0 || pin > L_MAX_GPIO_PIN || !cb) return -1;
    ensure_evq();
    gpio_install_isr_service(0); /* 已安装时返回 ESP_ERR_INVALID_STATE, 可忽略 */
    gpio_set_intr_type((gpio_num_t)pin,
                       mode == L_IRQ_RISE ? GPIO_INTR_POSEDGE :
                       mode == L_IRQ_FALL ? GPIO_INTR_NEGEDGE : GPIO_INTR_ANYEDGE);
    s_irq_tbl[pin].mode = mode;
    s_irq_tbl[pin].cb   = cb;
    s_irq_tbl[pin].user = user;
    gpio_isr_handler_add((gpio_num_t)pin, gpio_isr_thunk, (void *)(intptr_t)pin);
    return 0;
}

int l_hal_gpio_irq_detach(int pin) {
    if (pin < 0 || pin > L_MAX_GPIO_PIN) return -1;
    gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_DISABLE);
    gpio_isr_handler_remove((gpio_num_t)pin);
    s_irq_tbl[pin].cb = NULL;
    return 0;
}

void l_hal_gpio_sim_write(int pin, int level) {
    l_hal_gpio_write(pin, level); /* 真机直接写电平, 外部信号才走中断 */
}

static struct {
    int        running, period_ms, repeating;
    int64_t    next_fire_ms;
    LTimerFn   cb;
    void      *user;
} s_timers[L_MAX_SW_TIMERS];

static void esp_timer_thunk(void *arg) {
    int id = (int)(intptr_t)arg;
    ev_push_task(1, id, 0);
}

int l_hal_timer_start(int id, int period_ms, int repeating, LTimerFn cb, void *user) {
    if (id < 0 || id >= L_MAX_SW_TIMERS || period_ms <= 0 || !cb) return -1;
    l_hal_timer_stop(id);
    esp_timer_create_args_t args = {
        .callback = esp_timer_thunk,
        .arg      = (void *)(intptr_t)id,
        .name     = "l_timer",
    };
    esp_timer_handle_t h = NULL;
    if (esp_timer_create(&args, &h) != ESP_OK) return -1;
    s_timers[id].cb   = cb;
    s_timers[id].user = user;
    esp_timer_start_periodic(h, (uint64_t)period_ms * 1000ULL);
    s_timers[id].running    = 1;
    s_timers[id].period_ms  = period_ms;
    s_timers[id].repeating  = repeating;
    s_timers[id].next_fire_ms = l_hal_ticks_ms() + period_ms;
    return 0;
}

void l_hal_timer_stop(int id) {
    if (id < 0 || id >= L_MAX_SW_TIMERS) return;
    s_timers[id].running = 0;
    s_timers[id].cb      = NULL;
    /* esp_timer 句柄由创建时分配, 这里不重复删除以简化句柄管理 */
}

int l_hal_timer_running(int id) {
    if (id < 0 || id >= L_MAX_SW_TIMERS) return 0;
    return s_timers[id].running;
}

static struct {
    int     enabled, timeout_ms, expired;
    int64_t last_feed_ms;
} s_wdt;

void l_hal_poll_events(void) {
    ensure_evq();
#if !CONFIG_ESP_TASK_WDT_EN
    /* 软件看门狗(硬件任务看门狗未编入时): 到期在此置位, 由脚本层抛可捕获错误 */
    if (s_wdt.enabled && !s_wdt.expired &&
        l_hal_ticks_ms() - s_wdt.last_feed_ms > s_wdt.timeout_ms) {
        s_wdt.expired = 1;
    }
#endif
    if (!s_evq) return;
    LEv ev;
    while (xQueueReceive(s_evq, &ev, 0) == pdTRUE) {
        if (ev.kind == 0) {
            if (ev.a >= 0 && ev.a <= L_MAX_GPIO_PIN && s_irq_tbl[ev.a].cb)
                s_irq_tbl[ev.a].cb(s_irq_tbl[ev.a].user, ev.a, ev.b);
        } else {
            if (ev.a >= 0 && ev.a < L_MAX_SW_TIMERS && s_timers[ev.a].cb)
                s_timers[ev.a].cb(s_timers[ev.a].user, ev.a);
        }
    }
}

/* ---- 看门狗 ----
 * 关键: esp_task_wdt_* 符号只有在 CONFIG_ESP_TASK_WDT_EN=y 时才会被编入
 * libesp_system.a, 否则链接期直接 undefined reference (CI 曾因此失败)。
 * 因此这里按 Kconfig 条件编译:
 *   启用 -> 使用硬件任务看门狗(超时由 IDF 处理, 同时记录到期标志供脚本感知)
 *   禁用 -> 退化为软件看门狗(与 PC 模拟一致, 在事件派发点判超时)
 * esp32/sdkconfig.defaults 已默认置 CONFIG_ESP_TASK_WDT_EN=y。 */
#if CONFIG_ESP_TASK_WDT_EN

int l_hal_wdt_enable(int timeout_ms) {
    if (timeout_ms <= 0) return -1;
    esp_task_wdt_config_t cfg = {
        .timeout_ms     = (uint32_t)timeout_ms,
        .idle_core_mask = 0,
        .trigger_panic  = false,   /* 超时只告警不重启, 交给脚本层抛错 */
    };
    esp_err_t err = esp_task_wdt_init(&cfg);
    if (err == ESP_ERR_INVALID_STATE) {
        /* 已在启动阶段自动初始化(CONFIG_ESP_TASK_WDT_INIT), 改为重配置 */
        err = esp_task_wdt_reconfigure(&cfg);
    }
    if (err != ESP_OK) return -1;
    esp_task_wdt_add(NULL);        /* 已添加时返回错误码, 忽略即可 */
    s_wdt.enabled      = 1;
    s_wdt.timeout_ms   = timeout_ms;
    s_wdt.expired      = 0;
    s_wdt.last_feed_ms = l_hal_ticks_ms();
    return 0;
}

void l_hal_wdt_feed(void) {
    if (s_wdt.enabled) {
        esp_task_wdt_reset();
        s_wdt.last_feed_ms = l_hal_ticks_ms();
        s_wdt.expired = 0;
    }
}

void l_hal_wdt_disable(void) {
    if (s_wdt.enabled) esp_task_wdt_delete(NULL);
    s_wdt.enabled = 0;
    s_wdt.expired = 0;
}

int l_hal_wdt_expired(void) {
    return s_wdt.expired;
}

#else /* !CONFIG_ESP_TASK_WDT_EN: 软件看门狗兜底, 保证任何 sdkconfig 下可链接 */

int l_hal_wdt_enable(int timeout_ms) {
    if (timeout_ms <= 0) return -1;
    s_wdt.enabled      = 1;
    s_wdt.timeout_ms   = timeout_ms;
    s_wdt.expired      = 0;
    s_wdt.last_feed_ms = l_hal_ticks_ms();
    return 0;
}

void l_hal_wdt_feed(void) {
    if (s_wdt.enabled) {
        s_wdt.last_feed_ms = l_hal_ticks_ms();
        s_wdt.expired = 0;
    }
}

void l_hal_wdt_disable(void) {
    s_wdt.enabled = 0;
    s_wdt.expired = 0;
}

int l_hal_wdt_expired(void) {
    return s_wdt.expired;
}

#endif /* CONFIG_ESP_TASK_WDT_EN */

void l_hal_deepsleep(int ms) {
    /* 真机: 睡眠后芯片复位重启, 不会执行到这里 */
    if (ms > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
        esp_deep_sleep_start();
    } else {
        esp_deep_sleep_start();
    }
}

int l_hal_wake_reason(void) {
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER:    return 1;
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
        case ESP_SLEEP_WAKEUP_GPIO:     return 2;
        default:                        return 0;
    }
}

#else /* PC 宿主全功能模拟 */

static struct {
    int      mode;
    LIrqFn   cb;
    void    *user;
    int      last_level;
} s_irq_tbl[L_MAX_GPIO_PIN + 1];

/* 事件环形队列: 中断/定时器事件先入队, poll 时统一派发(复刻真机 ISR 入队模型) */
#define L_EV_CAP 64
typedef struct { int kind; int a, b; } LEv;   /* kind: 0=irq(pin=a,level=b) 1=timer(id=a) */
static LEv  s_ev[L_EV_CAP];
static int  s_ev_head = 0, s_ev_cnt = 0;

static struct {
    int     enabled, timeout_ms, expired;
    int64_t last_feed_ms;
} s_wdt;

static void ev_push(int kind, int a, int b) {
    if (s_ev_cnt >= L_EV_CAP) return;   /* 队满丢弃(与真机 ISR 行为一致) */
    s_ev[(s_ev_head + s_ev_cnt) % L_EV_CAP] = (LEv){ kind, a, b };
    s_ev_cnt++;
}

int l_hal_gpio_irq_attach(int pin, int mode, LIrqFn cb, void *user) {
    if (pin < 0 || pin > L_MAX_GPIO_PIN || !cb) return -1;
    s_irq_tbl[pin].mode       = mode;
    s_irq_tbl[pin].cb         = cb;
    s_irq_tbl[pin].user       = user;
    s_irq_tbl[pin].last_level = s_pin_level[pin];
    return 0;
}

int l_hal_gpio_irq_detach(int pin) {
    if (pin < 0 || pin > L_MAX_GPIO_PIN) return -1;
    s_irq_tbl[pin].cb = NULL;
    return 0;
}

void l_hal_gpio_sim_write(int pin, int level) {
    if (pin < 0 || pin > L_MAX_GPIO_PIN) return;
    int v = level ? 1 : 0;
    int old = s_pin_level[pin];
    s_pin_level[pin] = (uint8_t)v;
    if (!s_irq_tbl[pin].cb) return;
    int hit = (s_irq_tbl[pin].mode == L_IRQ_CHANGE && v != old) ||
              (s_irq_tbl[pin].mode == L_IRQ_RISE   && old == 0 && v == 1) ||
              (s_irq_tbl[pin].mode == L_IRQ_FALL   && old == 1 && v == 0);
    if (hit) ev_push(0, pin, v);
}

static struct {
    int        running, period_ms, repeating;
    int64_t    next_fire_ms;
    LTimerFn   cb;
    void      *user;
} s_timers[L_MAX_SW_TIMERS];

int l_hal_timer_start(int id, int period_ms, int repeating, LTimerFn cb, void *user) {
    if (id < 0 || id >= L_MAX_SW_TIMERS || period_ms <= 0 || !cb) return -1;
    s_timers[id].running      = 1;
    s_timers[id].period_ms    = period_ms;
    s_timers[id].repeating    = repeating;
    s_timers[id].next_fire_ms = l_hal_ticks_ms() + period_ms;
    s_timers[id].cb           = cb;
    s_timers[id].user         = user;
    return 0;
}

void l_hal_timer_stop(int id) {
    if (id < 0 || id >= L_MAX_SW_TIMERS) return;
    s_timers[id].running = 0;
    s_timers[id].cb      = NULL;
}

int l_hal_timer_running(int id) {
    if (id < 0 || id >= L_MAX_SW_TIMERS) return 0;
    return s_timers[id].running;
}

void l_hal_poll_events(void) {
    /* 1. 软件定时器到期检查 */
    int64_t now = l_hal_ticks_ms();
    for (int i = 0; i < L_MAX_SW_TIMERS; i++) {
        if (!s_timers[i].running) continue;
        while (now >= s_timers[i].next_fire_ms) {
            ev_push(1, i, 0);
            if (s_timers[i].repeating) s_timers[i].next_fire_ms += s_timers[i].period_ms;
            else { s_timers[i].running = 0; break; }
        }
    }
    /* 2. 看门狗超时检测(PC 模拟为可捕获标志, 真机为系统复位) */
    if (s_wdt.enabled && !s_wdt.expired &&
        now - s_wdt.last_feed_ms > s_wdt.timeout_ms) {
        s_wdt.expired = 1;
    }
    /* 3. 派发队列事件 */
    while (s_ev_cnt > 0) {
        LEv ev = s_ev[s_ev_head];
        s_ev_head = (s_ev_head + 1) % L_EV_CAP;
        s_ev_cnt--;
        if (ev.kind == 0) {
            if (s_irq_tbl[ev.a].cb) s_irq_tbl[ev.a].cb(s_irq_tbl[ev.a].user, ev.a, ev.b);
        } else {
            if (s_timers[ev.a].cb) s_timers[ev.a].cb(s_timers[ev.a].user, ev.a);
        }
    }
}

int l_hal_wdt_enable(int timeout_ms) {
    if (timeout_ms <= 0) return -1;
    s_wdt.enabled      = 1;
    s_wdt.timeout_ms   = timeout_ms;
    s_wdt.expired      = 0;
    s_wdt.last_feed_ms = l_hal_ticks_ms();
    return 0;
}

void l_hal_wdt_feed(void) {
    if (s_wdt.enabled) {
        s_wdt.last_feed_ms = l_hal_ticks_ms();
        s_wdt.expired = 0;
    }
}

void l_hal_wdt_disable(void) {
    s_wdt.enabled = 0;
    s_wdt.expired = 0;
}

int l_hal_wdt_expired(void) {
    return s_wdt.expired;
}

void l_hal_deepsleep(int ms) {
    /* PC 模拟: 不真正睡眠(保证离线测试秒级完成), 仅记录唤醒原因 */
    if (ms > 0) s_wake_reason = 1;
}

int l_hal_wake_reason(void) {
    return s_wake_reason;
}

#endif /* L_PLATFORM_ESP_IDF */
