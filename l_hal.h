/* L 语言硬件抽象层 (HAL): 统一隔离 ESP-IDF 硬件驱动与 PC 宿主模拟实现 */
#ifndef L_HAL_H
#define L_HAL_H

#include <stddef.h>
#include <stdint.h>
#include "l_config.h"

/* ================= 1. 引脚工作模式枚举 ================= */
typedef enum {
    L_PIN_DISABLED    = 0,
    L_PIN_OUT         = 1,
    L_PIN_IN          = 2,
    L_PIN_IN_PULLUP   = 3,
    L_PIN_IN_PULLDOWN = 4,
    L_PIN_OPEN_DRAIN  = 5,
    L_PIN_PWM         = 6,
    L_PIN_ADC         = 7
} LPinMode;

#define L_MAX_GPIO_PIN 48

/* ================= 2. 裸 Flash 脚本分区 16 字节头部 ================= */
typedef struct {
    uint32_t magic;    /* L_SCRIPT_MAGIC_SRC ("LSCR") 或 L_SCRIPT_MAGIC_BC ("LBC1") */
    uint32_t length;   /* 脚本有效字节长度 (不含结尾 '\0') */
    uint32_t crc32;    /* 内容区的 IEEE 802.3 CRC32 校验值 */
    uint32_t flags;    /* 预留标志位 (bit0: 上电自启动) */
} LScriptHeader;

/* Flash 脚本读取状态码 */
typedef enum {
    L_FLASH_OK        = 0,  /* 校验通过, 脚本有效 */
    L_FLASH_EMPTY     = 1,  /* 分区未烧录脚本 (魔数不匹配或全 0xFF) */
    L_FLASH_ERR_CRC   = -1, /* CRC32 校验失败 (数据损坏) */
    L_FLASH_ERR_PART  = -2, /* 未找到 l_script 分区或长度越界 */
    L_FLASH_ERR_IO    = -3  /* 擦写/映射底层硬件错误 */
} LFlashStatus;

/* ================= 3. 系统、时钟与芯片信息 HAL ================= */
int64_t     l_hal_ticks_ms(void);
int64_t     l_hal_ticks_us(void);
void        l_hal_delay_ms(uint32_t ms);
int32_t     l_hal_free_heap(size_t vm_allocated_bytes, int sram_kb);
const char *l_hal_idf_version(void);
int         l_hal_is_hardware(void);
void        l_hal_get_mac(uint8_t mac[6], int chip_id);
int         l_hal_get_cpu_mhz(void);
void        l_hal_set_cpu_mhz(int mhz);

/* ================= 4. GPIO HAL ================= */
void        l_hal_gpio_set_mode(int pin, LPinMode mode);
LPinMode    l_hal_gpio_get_mode(int pin);
void        l_hal_gpio_write(int pin, int level);
int         l_hal_gpio_read(int pin);
const char *l_hal_gpio_mode_name(LPinMode mode);

/* ================= 5. PWM (LEDC) HAL ================= */
void        l_hal_pwm_init(int pin, int freq_hz, int duty_10bit);
void        l_hal_pwm_set_freq(int pin, int freq_hz);
void        l_hal_pwm_set_duty(int pin, int duty_10bit);
void        l_hal_pwm_stop(int pin);

/* ================= 6. ADC HAL ================= */
void        l_hal_adc_init(int pin, int full_scale_mv);
int         l_hal_adc_read_raw(int pin);
void        l_hal_adc_write_sim(int pin, int raw);

/* ================= 7. I2C 总线 HAL ================= */
void        l_hal_i2c_init(int bus, int sda, int scl, int freq_hz);
int         l_hal_i2c_scan(int bus, uint8_t *out_addrs, int max_addrs);
int         l_hal_i2c_write(int bus, uint8_t addr, const uint8_t *data, size_t len);
int         l_hal_i2c_read(int bus, uint8_t addr, uint8_t *data, size_t len);
int         l_hal_i2c_write_reg(int bus, uint8_t addr, uint8_t reg, const uint8_t *data, size_t len);
int         l_hal_i2c_read_reg(int bus, uint8_t addr, uint8_t reg, uint8_t *data, size_t len);
int         l_hal_i2c_sim_device(int bus, uint8_t addr, const uint8_t *init_regs, size_t init_len);

/* ================= 8. SPI 总线 HAL ================= */
void        l_hal_spi_init(int bus, int sck, int mosi, int miso, int freq_hz);
int         l_hal_spi_transfer(int bus, const uint8_t *tx, uint8_t *rx, size_t len);

/* ================= 9. UART 串口 HAL ================= */
void        l_hal_uart_init(int port, int baud, int tx, int rx);
int         l_hal_uart_write(int port, const uint8_t *data, size_t len);
size_t      l_hal_uart_any(int port);
size_t      l_hal_uart_read(int port, uint8_t *buf, size_t max_len);
void        l_hal_uart_sim_input(int port, const uint8_t *data, size_t len);

/* ================= 10. 裸 Flash 脚本分区 (l_script) HAL ================= */
uint32_t     l_hal_crc32(const void *data, size_t len);
LFlashStatus l_hal_flash_info(LScriptHeader *out_hdr, size_t *out_part_size);
LFlashStatus l_hal_flash_write_script(const char *src, size_t len, uint32_t flags);
const char  *l_hal_flash_read_script(size_t *out_len, LFlashStatus *out_status);
LFlashStatus l_hal_flash_erase_script(void);

/* ================= 11. PC 宿主串口扫描、一键烧录与远程终端 HAL ================= */
/* 扫描本机可用串口名(如 "COM3"), 写入 names[][16], 返回找到的串口数量 */
int l_hal_serial_list_ports(char names[][16], int max_ports);

/* 通过串口(或 "sim" 本地模拟)向 ESP32 的 l_script 裸分区上传脚本并校验 CRC32:
 * 成功返回 0, 失败返回负数并将原因写入 err_buf */
int l_hal_serial_upload(const char *port, int baud, const char *src, size_t len,
                        char *err_buf, size_t err_cap);

/* 进入 ESP32 串口交互终端 (按 Ctrl+] 或输入 .exit 退出), 成功返回 0, 失败返回负数。
 * log_path 非空时把所有收发内容带本地时间戳追加写入该日志文件 (NULL 则不记录) */
int l_hal_serial_monitor(const char *port, int baud, const char *log_path,
                         char *err_buf, size_t err_cap);

/* 通过串口向 ESP32 发送单条代码语句并接收回显输出, 成功返回 0, 失败返回负数 */
int l_hal_serial_exec(const char *port, int baud, const char *code, char *err_buf, size_t err_cap);

/* ================= 12. WiFi 网络与热点 HAL ================= */
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t authmode;
} LWifiApRecord;

int  l_hal_wifi_connect(const char *ssid, const char *password, int timeout_ms);
int  l_hal_wifi_disconnect(void);
int  l_hal_wifi_is_connected(void);
int  l_hal_wifi_get_ssid(char *buf, size_t max_len);
int  l_hal_wifi_get_ip(char *buf, size_t max_len);
int  l_hal_wifi_get_mac(char *buf, size_t max_len);
int  l_hal_wifi_start_ap(const char *ssid, const char *password);
int  l_hal_wifi_scan(LWifiApRecord *records, int max_records);
/* 返回最近一次 WiFi 操作失败的详细原因描述(中文诊断信息, 供脚本 try/catch 或日志使用), 无错误时返回空串 */
const char *l_hal_wifi_last_error(void);

/* ================= 13. HTTP 客户端与 SNTP 网络对时 HAL ================= */
typedef struct {
    int    status_code;
    char  *body;
    size_t body_len;
    char  *err_msg;
} LHttpResponse;

LHttpResponse l_hal_http_get(const char *url, const char *headers, int timeout_ms);
LHttpResponse l_hal_http_post(const char *url, const char *data, size_t data_len, const char *headers, int timeout_ms);
int           l_hal_sntp_sync(const char *ntp_server, int timeout_ms);

/* ================= 14. M7: GPIO 中断 / 软硬件定时器 / 看门狗 / 深度睡眠 HAL ================= */
/* 统一事件模型: 硬件中断(真机 ISR)或模拟事件(PC)不直接回调脚本, 而是入队,
 * 由 l_hal_poll_events() 在脚本安全上下文(如 delay_ms/ticks_ms)中排队派发回调,
 * 避免在 ISR 栈上执行解释器代码。 */
typedef enum {
    L_IRQ_RISE   = 1,   /* 上升沿 0->1 */
    L_IRQ_FALL   = 2,   /* 下降沿 1->0 */
    L_IRQ_CHANGE = 3    /* 任意跳变 */
} LIrqMode;

typedef void (*LIrqFn)(void *user, int pin, int level);
typedef void (*LTimerFn)(void *user, int timer_id);

#define L_MAX_SW_TIMERS 8

/* 注册引脚中断回调, mode 为 LIrqMode; 成功返回 0, 失败返回负数 */
int  l_hal_gpio_irq_attach(int pin, int mode, LIrqFn cb, void *user);
/* 注销引脚中断回调 */
int  l_hal_gpio_irq_detach(int pin);
/* PC 模拟: 注入一次外部电平跳变并按已注册模式触发中断; 真机: 直接写电平 */
void l_hal_gpio_sim_write(int pin, int level);

/* 启动软件定时器(真机映射 esp_timer): repeating=0 表示单次触发 */
int  l_hal_timer_start(int id, int period_ms, int repeating, LTimerFn cb, void *user);
void l_hal_timer_stop(int id);
int  l_hal_timer_running(int id);

/* 派发排队的事件(定时器到期、中断、看门狗超时检测), 在脚本安全点调用 */
void l_hal_poll_events(void);

/* 看门狗: enable 后若超过 timeout_ms 未 feed, l_hal_wdt_expired() 置位
 * (真机直接触发系统复位, PC 模拟为可被 try/catch 捕获的超时错误) */
int  l_hal_wdt_enable(int timeout_ms);
void l_hal_wdt_feed(void);
void l_hal_wdt_disable(void);
int  l_hal_wdt_expired(void);

/* 深度睡眠(真机 esp_deep_sleep 不返回, 重启后由 wake_reason 查询唤醒源;
 * PC 模拟仅记录唤醒原因并立即返回, 便于离线测试业务逻辑) */
void l_hal_deepsleep(int ms);
/* 唤醒原因: 0=上电 1=定时器唤醒 2=外部引脚唤醒 */
int  l_hal_wake_reason(void);

#endif /* L_HAL_H */
