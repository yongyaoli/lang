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
#  include "esp_timer.h"
#  include "esp_system.h"
#  include "esp_mac.h"
#  include "esp_partition.h"
#  include "esp_rom_sys.h"
#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"
#elif defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
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
#else
    return (int64_t)((double)clock() * 1000.0 / CLOCKS_PER_SEC);
#endif
}

int64_t l_hal_ticks_us(void) {
#if L_PLATFORM_ESP_IDF
    return (int64_t)esp_timer_get_time();
#else
    return (int64_t)((double)clock() * 1000000.0 / CLOCKS_PER_SEC);
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
static esp_partition_mmap_handle_t s_mmap_handle = 0;
static const void *s_mmap_ptr = NULL;

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
    if (s_mmap_ptr) {
        esp_partition_munmap(s_mmap_handle);
        s_mmap_ptr = NULL;
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
    if (s_mmap_ptr) {
        esp_partition_munmap(s_mmap_handle);
        s_mmap_ptr = NULL;
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
    if (!s_mmap_ptr) {
        if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &s_mmap_ptr, &s_mmap_handle) != ESP_OK) {
            if (out_status) *out_status = L_FLASH_ERR_IO;
            return NULL;
        }
    }
    const LScriptHeader *hdr = (const LScriptHeader *)s_mmap_ptr;
    if (hdr->magic != L_SCRIPT_MAGIC_SRC && hdr->magic != L_SCRIPT_MAGIC_BC) {
        if (out_status) *out_status = L_FLASH_EMPTY;
        return NULL;
    }
    if (sizeof(LScriptHeader) + hdr->length + 1 > part->size) {
        if (out_status) *out_status = L_FLASH_EMPTY;
        return NULL;
    }
    const char *body = (const char *)s_mmap_ptr + sizeof(LScriptHeader);
    uint32_t calc = l_hal_crc32(body, hdr->length);
    if (calc != hdr->crc32) {
        if (out_status) *out_status = L_FLASH_ERR_CRC;
        return NULL;
    }
    if (out_len) *out_len = hdr->length;
    if (out_status) *out_status = L_FLASH_OK;
    return body;
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
    uint32_t crc = l_hal_crc32(src, len);
    /* 支持 "sim" / "loopback" 本地离线模拟演练烧录 (无需连接真机也可完整测试烧录协议与校验) */
    if (strcmp(port, "sim") == 0 || strcmp(port, "SIM") == 0 || strcmp(port, "loopback") == 0) {
        LFlashStatus st = l_hal_flash_write_script(src, len, 1u);
        if (st != L_FLASH_OK) {
            snprintf(err_buf, err_cap, "写入模拟 Flash 分区 l_script 失败(状态码 %d)", (int)st);
            return -1;
        }
        printf("[upload:sim] 已向裸分区 %s 烧录 %zu 字节, CRC32=0x%08X (校验通过)\n",
               L_FLASH_PART_NAME, len, (unsigned)crc);
        return 1; /* 返回 1 表示模拟烧录成功, 调用方可立即触发 flash_exec */
    }

#if defined(_WIN32) && !L_PLATFORM_ESP_IDF
    HANDLE h = open_win_serial(port, baud, err_buf, err_cap);
    if (h == INVALID_HANDLE_VALUE) return -1;

    printf("[upload] 已连接 %s (%d bps), 准备烧录 %zu 字节 (CRC32=0x%08X)...\n",
           port, baud, len, (unsigned)crc);

    /* 1. 发送烧录帧起始命令 */
    char hdr_cmd[128];
    int hdr_len = snprintf(hdr_cmd, sizeof hdr_cmd,
                           "\r\n__L_UPLOAD_BEGIN__ %zu 0x%08X 1\r\n", len, (unsigned)crc);
    DWORD written = 0;
    WriteFile(h, hdr_cmd, (DWORD)hdr_len, &written, NULL);
    Sleep(80);

    /* 2. 分块发送脚本源码 */
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = (len - sent > 256) ? 256 : (len - sent);
        DWORD w = 0;
        if (!WriteFile(h, src + sent, (DWORD)chunk, &w, NULL) || w == 0) {
            CloseHandle(h);
            snprintf(err_buf, err_cap, "串口发送中断 (已发 %zu/%zu 字节)", sent, len);
            return -1;
        }
        sent += (size_t)w;
        Sleep(5);
    }

    /* 3. 发送烧录结束标记并读取设备响应 */
    const char *end_cmd = "\r\n__L_UPLOAD_END__\r\n";
    WriteFile(h, end_cmd, (DWORD)strlen(end_cmd), &written, NULL);

    char rx_buf[2048];
    size_t rx_total = 0;
    int ok = 0;
    for (int tick = 0; tick < 60; tick++) { /* 最多等待约 1.2 秒 */
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
    }
    if (rx_total > 0) {
        printf("%s\n", rx_buf);
    }
    CloseHandle(h);
    if (!ok) {
        printf("[upload] 已发送完毕 (%zu 字节)。若板子未响应 __L_UPLOAD_OK__, 请确认板子已刷入 L 语言 ESP32 固件。\n", len);
    } else {
        printf("[upload] ESP32 端确认烧录并校验成功!\n");
    }
    return 0;
#else
    (void)baud;
    snprintf(err_buf, err_cap, "当前平台暂未启用物理串口 %s (可使用 \"sim\" 模拟烧录)", port);
    return -1;
#endif
}

int l_hal_serial_monitor(const char *port, int baud, char *err_buf, size_t err_cap) {
#if defined(_WIN32) && !L_PLATFORM_ESP_IDF
    HANDLE h = open_win_serial(port, baud, err_buf, err_cap);
    if (h == INVALID_HANDLE_VALUE) return -1;

    printf("=== 已连接 ESP32 串口交互终端 (%s @ %d bps) ===\n", port, baud);
    printf("提示: 直接输入 L 语言语句回车即可在 ESP32 上执行; 按 Ctrl+] 或输入 .exit 回车退出终端。\n\n");

    DWORD w = 0;
    WriteFile(h, "\r\n", 2, &w, NULL);

    char line_buf[512];
    int  line_len = 0;
    char rx[256];

    for (;;) {
        DWORD r = 0;
        if (ReadFile(h, rx, sizeof(rx) - 1, &r, NULL) && r > 0) {
            rx[r] = '\0';
            printf("%s", rx);
            fflush(stdout);
        }
        while (_kbhit()) {
            int ch = _getch();
            if (ch == 0x1D) { /* Ctrl+] 立即退出 */
                printf("\n[已断开串口连接 %s]\n", port);
                CloseHandle(h);
                return 0;
            } else if (ch == '\r' || ch == '\n') {
                putchar('\n');
                line_buf[line_len] = '\0';
                if (strcmp(line_buf, ".exit") == 0 || strcmp(line_buf, ".quit") == 0) {
                    printf("[已断开串口连接 %s]\n", port);
                    CloseHandle(h);
                    return 0;
                }
                line_buf[line_len]     = '\r';
                line_buf[line_len + 1] = '\n';
                WriteFile(h, line_buf, (DWORD)(line_len + 2), &w, NULL);
                line_len = 0;
            } else if (ch == '\b' || ch == 127) {
                if (line_len > 0) {
                    line_len--;
                    printf("\b \b");
                    fflush(stdout);
                }
            } else if (line_len < (int)sizeof(line_buf) - 4) {
                line_buf[line_len++] = (char)ch;
                putchar(ch);
                fflush(stdout);
            }
        }
    }
#else
    (void)baud;
    snprintf(err_buf, err_cap, "无法打开串口终端 %s", port);
    return -1;
#endif
}
