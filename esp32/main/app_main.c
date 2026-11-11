/* L 语言 ESP32 固件入口 (基于 ESP-IDF v5.x):
 * 1. 上电自动校验并运行 l_script 裸 Flash 分区中的脚本
 * 2. 提供 UART0 / USB Serial 实时交互终端 (REPL)
 * 3. 支持电脑端 `l -u COM3 main.l` 或 REPL 内 `upload("COM3", "main.l")` 秒级烧录更新 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interp.h"
#include "l_hal.h"
#include "parser.h"
#include "version.h"

/* 解释器实例放在静态 BSS 段, 避免占用 FreeRTOS 任务栈 */
static Interp s_interp;

static void exec_snippet(const char *code, const char *vfile, int echo) {
    int had_err = 0;
    Node *prog = parse_program_file(code, vfile, &had_err);
    if (!had_err && prog) {
        interp_run(&s_interp, prog, echo);
    }
}

/* 从 stdin 阻塞式读取一行输入 (处理字符输入、退格删除、换行回车与终端回显，绝不无故重复刷屏) */
static int esp32_readline(char *buf, size_t max_len) {
    size_t len = 0;
    for (;;) {
        int ch = getchar();
        if (ch == EOF || ch == -1 || ch == 0xFF) {
            /* 暂无串口字符，休眠 10ms 让出 CPU 给 FreeRTOS IDLE 任务与 WiFi 协议栈 */
            l_hal_delay_ms(10);
            continue;
        }

        /* 回车换行: '\r' 或 '\n' */
        if (ch == '\r' || ch == '\n') {
            buf[len] = '\0';
            if (ch == '\r') {
                /* 短暂等待并吸收紧跟的 '\n'; 若读到其它字符则退回 stdin, 绝不丢弃 */
                for (int k = 0; k < 5; k++) {
                    l_hal_delay_ms(2);
                    int next_ch = getchar();
                    if (next_ch == '\n') {
                        break;
                    }
                    if (next_ch != EOF && next_ch != -1 && next_ch != 0xFF) {
                        ungetc(next_ch, stdin);
                        break;
                    }
                }
            }
            printf("\r\n");
            fflush(stdout);
            return (int)len;
        }

        /* 退格键处理 (Backspace 0x08 或 DEL 0x7F) */
        if (ch == '\b' || ch == 127) {
            if (len > 0) {
                len--;
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }

        /* Ctrl+C (0x03): 取消当前输入行 */
        if (ch == 3) {
            buf[0] = '\0';
            printf("^C\r\n");
            fflush(stdout);
            return -1;
        }

        /* 制表符 Tab 转为 4 个空格 */
        if (ch == '\t') {
            for (int k = 0; k < 4 && len + 1 < max_len; k++) {
                buf[len++] = ' ';
                putchar(' ');
            }
            fflush(stdout);
            continue;
        }

        /* 过滤不可见控制字符 */
        if (ch < 32) {
            continue;
        }

        /* 正常字符存入缓冲区并实时回显到串口终端 */
        if (len + 1 < max_len) {
            buf[len++] = (char)ch;
            putchar(ch);
            fflush(stdout);
        }
    }
}

/* 接收电脑端发来的 __L_UPLOAD_BEGIN__ <len> <crc_hex> <flags> 串口烧录帧 */
static void handle_serial_upload(const char *header_line) {
    unsigned long exp_len = 0, exp_crc = 0, flags = 1;
    if (sscanf(header_line, "__L_UPLOAD_BEGIN__ %lu %lx %lu", &exp_len, &exp_crc, &flags) < 2) {
        printf("__L_UPLOAD_ERR__ invalid header\r\n");
        return;
    }
    if (exp_len == 0 || exp_len > L_FLASH_PART_SIZE - sizeof(LScriptHeader) - 2) {
        printf("__L_UPLOAD_ERR__ script too large (%lu bytes)\r\n", exp_len);
        return;
    }

    char *buf = (char *)malloc(exp_len + 1);
    if (!buf) {
        printf("__L_UPLOAD_ERR__ out of memory\r\n");
        return;
    }

    /* 0. 排干协议头残留的换行字节。
     * ESP-IDF 串口控制台可能把输入的 '\r' 翻译为 '\n', 导致 PC 端 "\r\n"
     * 结尾的协议头在读行后仍残留一个 '\n' 混入脚本正文首部, 使整段正文
     * 错位一字节, CRC32 校验必然失败。此刻 PC 端在收到 READY 前不会发送
     * 正文, 因此 RX 队列中的 '\r'/'\n' 只可能是协议头残留, 可安全丢弃;
     * 若读到其它字符 (理论不发生) 则退回 stdin, 绝不丢失。 */
    for (;;) {
        int ch = getchar();
        if (ch == '\r' || ch == '\n') {
            continue;
        }
        if (ch != EOF && ch != -1 && ch != 0xFF) {
            ungetc(ch, stdin);
        }
        break;
    }

    /* 1. 向 PC 发送就绪握手信号，通知 PC 可以发送源码正文字节 */
    printf("__L_UPLOAD_READY__\r\n");
    fflush(stdout);

    /* 2. 精确读取 exp_len 个字节 (带 5 秒超时保护) */
    size_t got = 0;
    int timeout_ms = 0;
    while (got < exp_len) {
        int ch = getchar();
        if (ch == EOF || ch == -1 || ch == 0xFF) {
            l_hal_delay_ms(2);
            timeout_ms += 2;
            if (timeout_ms > 5000) {
                printf("__L_UPLOAD_ERR__ transfer timeout (got %zu/%lu bytes)\r\n", got, exp_len);
                free(buf);
                return;
            }
            continue;
        }
        timeout_ms = 0;
        buf[got++] = (char)(ch & 0xFF);
    }
    buf[exp_len] = '\0';

    /* 3. 校验 CRC32 */
    uint32_t actual_crc = l_hal_crc32(buf, exp_len);
    if (actual_crc != (uint32_t)exp_crc) {
        printf("__L_UPLOAD_ERR__ CRC32 mismatch (expected 0x%08lX, got 0x%08X)\r\n",
               exp_crc, (unsigned)actual_crc);
        free(buf);
        return;
    }

    /* 4. 写入裸 Flash 分区 l_script */
    LFlashStatus st = l_hal_flash_write_script(buf, exp_len, (uint32_t)flags);
    free(buf);
    if (st != L_FLASH_OK) {
        printf("__L_UPLOAD_ERR__ flash write failed (%d)\r\n", (int)st);
        return;
    }

    printf("__L_UPLOAD_OK__ %lu bytes written (CRC32=0x%08X)\r\n", exp_len, (unsigned)actual_crc);
    fflush(stdout);

    /* 5. 烧录成功后立即执行新脚本 */
    exec_snippet("ESP32.flash_exec();", "<boot>", 0);
}

void app_main(void) {
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

    printf("\r\n================================================\r\n");
    printf("  L 语言 ESP32 固件 %s (%s)\r\n", L_VERSION, l_hal_idf_version());
    printf("  输入 help() 查看帮助, 输入 ESP32.status() 查看系统与网络状态\r\n");
    printf("================================================\r\n");

    /* 1. 检查 l_script 裸 Flash 分区是否存在有效脚本, 有则自动执行 */
    LScriptHeader hdr;
    size_t part_sz = 0;
    LFlashStatus st = l_hal_flash_info(&hdr, &part_sz);
    if (st == L_FLASH_OK && (hdr.flags & 1u)) {
        printf("[boot] 检测到 l_script 分区脚本 (%u 字节, CRC32=0x%08X), 正在执行...\r\n",
               (unsigned)hdr.length, (unsigned)hdr.crc32);
        exec_snippet("ESP32.flash_exec();", "<boot>", 0);
    } else if (st == L_FLASH_ERR_CRC) {
        printf("[boot] 警告: l_script 分区 CRC32 校验失败, 已跳过启动脚本。\r\n");
    }

    /* 2. 进入串口 REPL 与在线烧录监听循环 (首轮输出提示符) */
    char line[1024];
    printf("esp32>>> ");
    fflush(stdout);

    for (;;) {
        int n = esp32_readline(line, sizeof line);
        if (n < 0) {
            /* Ctrl+C 取消输入，输出新提示符 */
            printf("esp32>>> ");
            fflush(stdout);
            continue;
        }
        if (n == 0) {
            /* 用户直接敲了回车，输出新提示符并继续等待 */
            printf("esp32>>> ");
            fflush(stdout);
            continue;
        }

        /* 识别 PC 端 upload() 协议头 */
        if (strncmp(line, "__L_UPLOAD_BEGIN__", 18) == 0) {
            handle_serial_upload(line);
            printf("esp32>>> ");
            fflush(stdout);
            continue;
        }

        /* 交互式语句末尾自动补 ';' */
        size_t len = (size_t)n;
        if (line[len - 1] != ';' && line[len - 1] != '}' && len + 2 < sizeof(line)) {
            line[len] = ';';
            line[len + 1] = '\0';
        }
        char *heap_line = strdup(line);
        exec_snippet(heap_line, "<repl>", 1);

        /* 每次执行完毕后才打印下一次输入的提示符 */
        printf("esp32>>> ");
        fflush(stdout);
    }
}
