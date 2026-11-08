/* L 语言 ESP32 固件入口 (基于 ESP-IDF):
 * 1. 上电自动校验并零拷贝运行 l_script 裸 Flash 分区中的脚本
 * 2. 提供 UART0 / USB Serial 实时交互终端 (REPL)
 * 3. 支持电脑端 `l -u COM3 main.l` 或 REPL 内 `upload("COM3", "main.l")` 秒级烧录更新 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interp.h"
#include "l_hal.h"
#include "parser.h"
#include "version.h"

#ifdef ESP_PLATFORM
#  include "driver/uart.h"
#  include "esp_vfs_dev.h"
#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"
#endif

/* 解释器实例放在静态 BSS 段, 避免占用 FreeRTOS 任务栈 */
static Interp s_interp;

static void exec_snippet(const char *code, const char *vfile, int echo) {
    int had_err = 0;
    Node *prog = parse_program_file(code, vfile, &had_err);
    if (!had_err && prog) {
        interp_run(&s_interp, prog, echo);
    }
}

/* 接收电脑端发来的 __L_UPLOAD_BEGIN__ <len> <crc_hex> <flags> 串口烧录帧 */
static void handle_serial_upload(const char *header_line) {
    unsigned long exp_len = 0, exp_crc = 0, flags = 1;
    if (sscanf(header_line, "__L_UPLOAD_BEGIN__ %lu %lx %lu", &exp_len, &exp_crc, &flags) < 2) {
        printf("__L_UPLOAD_ERR__ invalid header\n");
        return;
    }
    if (exp_len == 0 || exp_len > L_FLASH_PART_SIZE - sizeof(LScriptHeader) - 2) {
        printf("__L_UPLOAD_ERR__ script too large (%lu bytes)\n", exp_len);
        return;
    }

    char *buf = (char *)malloc(exp_len + 1);
    if (!buf) {
        printf("__L_UPLOAD_ERR__ out of memory\n");
        return;
    }

    size_t got = 0;
    while (got < exp_len) {
        int ch = getchar();
        if (ch == EOF) {
            l_hal_delay_ms(2);
            continue;
        }
        buf[got++] = (char)ch;
    }
    buf[exp_len] = '\0';

    /* 吸收结尾的 __L_UPLOAD_END__ 行 */
    char tail[64];
    fgets(tail, sizeof tail, stdin);

    uint32_t actual_crc = l_hal_crc32(buf, exp_len);
    if (actual_crc != (uint32_t)exp_crc) {
        printf("__L_UPLOAD_ERR__ CRC32 mismatch (expected 0x%08lX, got 0x%08X)\n",
               exp_crc, (unsigned)actual_crc);
        free(buf);
        return;
    }

    LFlashStatus st = l_hal_flash_write_script(buf, exp_len, (uint32_t)flags);
    free(buf);
    if (st != L_FLASH_OK) {
        printf("__L_UPLOAD_ERR__ flash write failed (%d)\n", (int)st);
        return;
    }

    printf("__L_UPLOAD_OK__ %lu bytes written (CRC32=0x%08X)\n", exp_len, (unsigned)actual_crc);
    /* 烧录成功后立即从 Flash 映射区零拷贝运行新脚本 */
    exec_snippet("ESP32.flash_exec();", "<boot>", 0);
}

void app_main(void) {
#ifdef ESP_PLATFORM
    /* 配置 UART0 标准输入输出为行缓冲/无缓冲以支持交互 REPL */
    uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0);
    esp_vfs_dev_uart_use_driver(UART_NUM_0);
#endif
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    interp_init(&s_interp);

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    exec_snippet("ESP32.set_chip(\"ESP32-S3\");", "<init>", 0);
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    exec_snippet("ESP32.set_chip(\"ESP32-C3\");", "<init>", 0);
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    exec_snippet("ESP32.set_chip(\"ESP32-C6\");", "<init>", 0);
#endif

    printf("\n================================================\n");
    printf("  L 语言 ESP32 固件 %s (%s)\n", L_VERSION, l_hal_idf_version());
    printf("  输入 help() 查看帮助, 输入 ESP32.pinout() 查看引脚表\n");
    printf("================================================\n");

    /* 1. 检查 l_script 裸 Flash 分区是否存在有效脚本, 有则自动执行 */
    LScriptHeader hdr;
    size_t part_sz = 0;
    LFlashStatus st = l_hal_flash_info(&hdr, &part_sz);
    if (st == L_FLASH_OK && (hdr.flags & 1u)) {
        printf("[boot] 检测到 l_script 分区脚本 (%u 字节, CRC32=0x%08X), 正在执行...\n",
               (unsigned)hdr.length, (unsigned)hdr.crc32);
        exec_snippet("ESP32.flash_exec();", "<boot>", 0);
    } else if (st == L_FLASH_ERR_CRC) {
        printf("[boot] 警告: l_script 分区 CRC32 校验失败, 已跳过启动脚本。\n");
    }

    /* 2. 进入串口 REPL 与在线烧录监听循环 */
    char line[1024];
    for (;;) {
        printf("esp32>>> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) {
            l_hal_delay_ms(10);
            continue;
        }
        /* 去除首尾回车换行 */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' '))
            line[--len] = '\0';
        if (len == 0) continue;

        /* 识别 PC 端 upload() 协议头 */
        if (strncmp(line, "__L_UPLOAD_BEGIN__", 18) == 0) {
            handle_serial_upload(line);
            continue;
        }

        /* 交互式语句末尾自动补 ';' */
        if (line[len - 1] != ';' && line[len - 1] != '}' && len + 2 < sizeof(line)) {
            line[len] = ';';
            line[len + 1] = '\0';
        }
        char *heap_line = strdup(line);
        exec_snippet(heap_line, "<repl>", 1);
    }
}
