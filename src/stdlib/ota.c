/*
 * L 语言无线网络空中升级 (OTA) 标准库实现 (M15)
 *
 * 专注于嵌入式与 PC 跨平台的统一 OTA 升级体系:
 * - 脚本级空中升级 (Script OTA): 从 HTTP(S) 端点拉取脚本源码，校验 CRC32，流式写入裸 Flash 分区 (l_script) 并热执行
 * - 局域网 Web OTA 支撑: 支持直接从内存字符串写入 Flash 分区并热重载，完美配合 Web OTA 服务器
 * - 内置 PC 宿主环境高保真模拟器 (sim_set_response)，支持自动化回归测试与边界异常演练
 * - 纯 C99 零第三方依赖，与 l_heap、l_hal 及异常定位机制深度整合
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "l_ota.h"
#include "l_hal.h"
#include "l_heap.h"

/* ================= 1. PC 离线模拟 HTTP 响应表 ================= */

typedef struct SimOtaResp {
    char url[256];
    char *content;
    size_t length;
    struct SimOtaResp *next;
} SimOtaResp;

static SimOtaResp *s_sim_ota_head = NULL;

/* 查找预设的模拟 HTTP 响应 */
static SimOtaResp *find_sim_resp(const char *url) {
    SimOtaResp *cur = s_sim_ota_head;
    while (cur) {
        if (strcmp(cur->url, url) == 0) return cur;
        cur = cur->next;
    }
    return NULL;
}

/* 注册或清除模拟 HTTP 响应 (供自动化回归测试演练) */
static void set_sim_resp(const char *url, const char *content, size_t len) {
    SimOtaResp *cur = s_sim_ota_head;
    SimOtaResp *prev = NULL;
    while (cur) {
        if (strcmp(cur->url, url) == 0) {
            if (!content) {
                /* 清除此条模拟响应 */
                if (prev) prev->next = cur->next;
                else s_sim_ota_head = cur->next;
                free(cur->content);
                free(cur);
                return;
            }
            /* 更新内容 */
            free(cur->content);
            cur->content = (char *)malloc(len + 1);
            memcpy(cur->content, content, len);
            cur->content[len] = '\0';
            cur->length = len;
            return;
        }
        prev = cur;
        cur = cur->next;
    }
    if (content) {
        SimOtaResp *item = (SimOtaResp *)malloc(sizeof(SimOtaResp));
        snprintf(item->url, sizeof(item->url), "%s", url);
        item->content = (char *)malloc(len + 1);
        memcpy(item->content, content, len);
        item->content[len] = '\0';
        item->length = len;
        item->next = s_sim_ota_head;
        s_sim_ota_head = item;
    }
}

/* ================= 2. 键值写入辅助函数 ================= */

static void ota_map_set_cstr(Interp *in, Map *m, const char *k, const char *v) {
    map_set(m, str_new(in, k, strlen(k)), val_str(str_new(in, v ? v : "", v ? strlen(v) : 0)));
}

static void ota_map_set_int(Interp *in, Map *m, const char *k, int32_t v) {
    map_set(m, str_new(in, k, strlen(k)), val_int(v));
}

static void ota_map_set_bool(Interp *in, Map *m, const char *k, int v) {
    map_set(m, str_new(in, k, strlen(k)), val_bool(v));
}

/* ================= 3. 核心 OTA 写入与热执行底层 ================= */

static Value execute_ota_write(Interp *in, const Node *at, const char *code_str, size_t code_len,
                               uint32_t expected_crc, int auto_exec) {
    if (!code_str) {
        rt_error(in, at, "OTA 写入失败: 源码内容为空");
    }

    /* 1. 计算 IEEE 802.3 硬件级 CRC32 校验码 */
    uint32_t actual_crc = l_hal_crc32(code_str, code_len);

    /* 2. 若指定了 expected_crc 且不为 0，则进行严格 CRC 完整性比对 */
    if (expected_crc != 0 && actual_crc != expected_crc) {
        rt_error(in, at, "OTA CRC32 校验失败: 期望 0x%08X, 实际为 0x%08X (数据可能在传输中损坏)",
                 (unsigned)expected_crc, (unsigned)actual_crc);
    }

    /* 3. 写入裸 Flash 分区 (l_script)，设置自启标志 flags=1 */
    LFlashStatus st = l_hal_flash_write_script(code_str, code_len, 1u);
    if (st != L_FLASH_OK) {
        rt_error(in, at, "OTA 写入裸 Flash 分区失败 (状态码 %d)", (int)st);
    }

    /* 4. 如果启用了 auto_exec (默认开启)，立即热重载执行新脚本 */
    if (auto_exec) {
        size_t dummy_len = 0;
        LFlashStatus read_st = L_FLASH_EMPTY;
        const char *flash_src = l_hal_flash_read_script(&dummy_len, &read_st);
        if (read_st == L_FLASH_OK && flash_src) {
            Env *saved_env = in->env;
            in->env = env_new(in, in->globals);
            interp_eval_source(in, at, flash_src, "<ota:script>");
            in->env = saved_env;
        }
    }

    /* 5. 构造结果字典并返回 */
    Map *res = map_new(in);
    Value rv = val_map(res);
    gc_push_root(in, rv);

    ota_map_set_bool(in, res, "ok", 1);
    ota_map_set_int(in, res, "bytes", (int32_t)code_len);
    char crc_buf[32];
    snprintf(crc_buf, sizeof(crc_buf), "0x%08X", (unsigned)actual_crc);
    ota_map_set_cstr(in, res, "crc32", crc_buf);
    ota_map_set_int(in, res, "crc32_int", (int32_t)actual_crc);
    ota_map_set_bool(in, res, "auto_exec", auto_exec);

    gc_pop_roots(in, 1);
    return rv;
}

/* ================= 4. OTA 原生方法实现 ================= */

/* OTA.update_script(url, [expected_crc=0], [auto_exec=true]) */
static Value m_ota_update_script(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    if (argc < 1 || argv[0].type != V_STR) {
        rt_error(in, at, "OTA.update_script(url, [expected_crc], [auto_exec]) 需要提供有效的目标 HTTP(S) URL 字符串");
    }
    const char *url = val_get_cstr(argv[0]);

    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        rt_error(in, at, "OTA.update_script() 非法的 URL 协议 (必须以 http:// 或 https:// 开头): %s", url);
    }

    uint32_t exp_crc = 0;
    if (argc >= 2) {
        if (argv[1].type == V_INT) exp_crc = (uint32_t)argv[1].as.i;
        else if (argv[1].type == V_NUM) exp_crc = (uint32_t)argv[1].as.num;
    }

    int auto_exec = 1;
    if (argc >= 3 && argv[2].type == V_BOOL) {
        auto_exec = argv[2].as.boolean;
    }

    /* 优先检查是否存在预设的模拟 HTTP 响应 (用于自动化测试演练) */
    SimOtaResp *sim = find_sim_resp(url);
    if (sim) {
        return execute_ota_write(in, at, sim->content, sim->length, exp_crc, auto_exec);
    }

    /* 真实网络发起 HTTP GET 请求拉取新版本脚本 */
    LHttpResponse resp = l_hal_http_get(url, NULL, 10000);
    if (resp.status_code != 200) {
        int sc = resp.status_code;
        const char *errmsg = resp.err_msg ? resp.err_msg : "HTTP 请求失败或对端无响应";
        if (resp.body) free(resp.body);
        if (resp.err_msg) free(resp.err_msg);
        rt_error(in, at, "OTA.update_script() 下载失败: 状态码 %d (%s)", sc, errmsg);
    }

    if (!resp.body || resp.body_len == 0) {
        if (resp.body) free(resp.body);
        if (resp.err_msg) free(resp.err_msg);
        rt_error(in, at, "OTA.update_script() 下载内容为空");
    }

    Value ret = execute_ota_write(in, at, resp.body, resp.body_len, exp_crc, auto_exec);
    free(resp.body);
    if (resp.err_msg) free(resp.err_msg);
    return ret;
}

/* OTA.update_from_string(src_code, [expected_crc=0], [auto_exec=true]) */
static Value m_ota_update_from_string(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    if (argc < 1 || argv[0].type != V_STR) {
        rt_error(in, at, "OTA.update_from_string(code, [expected_crc], [auto_exec]) 需要提供源码字符串");
    }
    const char *code = val_get_cstr(argv[0]);
    size_t len = strlen(code);

    uint32_t exp_crc = 0;
    if (argc >= 2) {
        if (argv[1].type == V_INT) exp_crc = (uint32_t)argv[1].as.i;
        else if (argv[1].type == V_NUM) exp_crc = (uint32_t)argv[1].as.num;
    }

    int auto_exec = 1;
    if (argc >= 3 && argv[2].type == V_BOOL) {
        auto_exec = argv[2].as.boolean;
    }

    return execute_ota_write(in, at, code, len, exp_crc, auto_exec);
}

/* OTA.status() -> 返回当前 OTA 分区状态与硬件环境字典 */
static Value m_ota_status(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at; (void)self; (void)argc; (void)argv;
    LScriptHeader hdr;
    size_t part_size = 0;
    LFlashStatus st = l_hal_flash_info(&hdr, &part_size);

    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);

    ota_map_set_cstr(in, m, "partition", "l_script");
    ota_map_set_int(in, m, "partition_size", (int32_t)part_size);
    ota_map_set_bool(in, m, "valid", st == L_FLASH_OK);

    const char *st_str = "empty";
    if (st == L_FLASH_OK) st_str = "ok";
    else if (st == L_FLASH_ERR_CRC) st_str = "crc_error";
    else if (st == L_FLASH_ERR_IO) st_str = "io_error";
    else if (st == L_FLASH_ERR_PART) st_str = "no_partition";
    ota_map_set_cstr(in, m, "status", st_str);

    ota_map_set_int(in, m, "script_length", (st == L_FLASH_OK) ? (int32_t)hdr.length : 0);
    char crc_buf[32];
    snprintf(crc_buf, sizeof(crc_buf), "0x%08X", (unsigned)(st == L_FLASH_OK ? hdr.crc32 : 0));
    ota_map_set_cstr(in, m, "script_crc32", crc_buf);
    ota_map_set_int(in, m, "script_crc32_int", (int32_t)(st == L_FLASH_OK ? hdr.crc32 : 0));

    /* 附加运行期内存状态 */
    ota_map_set_int(in, m, "allocated_bytes", (int32_t)in->bytes_allocated);
    ota_map_set_int(in, m, "heap_capacity", (int32_t)l_heap_capacity());

    gc_pop_roots(in, 1);
    return mv;
}

/* OTA.crc32(data) -> 计算字符串或字节数组的 IEEE 802.3 CRC32 校验码 */
static Value m_ota_crc32(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    if (argc < 1) {
        rt_error(in, at, "OTA.crc32(data) 需要提供字符串或 bytes 数据");
    }
    const char *data_ptr = NULL;
    size_t data_len = 0;

    if (argv[0].type == V_STR) {
        data_ptr = val_get_cstr(argv[0]);
        data_len = strlen(data_ptr);
    } else if (argv[0].type == V_BYTES && argv[0].as.bytes) {
        data_ptr = (const char *)argv[0].as.bytes->data;
        data_len = argv[0].as.bytes->len;
    } else {
        rt_error(in, at, "OTA.crc32(data) 参数必须为 string 或 bytes 类型");
    }

    uint32_t c = l_hal_crc32(data_ptr, data_len);
    return val_int((int32_t)c);
}

/* OTA.sim_set_response(url, [content]) -> 设置或清除模拟的 HTTP OTA 下载源 */
static Value m_ota_sim_set_response(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)self;
    if (argc < 1 || argv[0].type != V_STR) {
        rt_error(in, at, "OTA.sim_set_response(url, [content]) 需提供目标 URL 字符串");
    }
    const char *url = val_get_cstr(argv[0]);
    const char *content = NULL;
    size_t len = 0;

    if (argc >= 2 && argv[1].type == V_STR) {
        content = val_get_cstr(argv[1]);
        len = strlen(content);
    }

    set_sim_resp(url, content, len);
    return val_bool(1);
}

/* OTA.erase() -> 清空裸 Flash 分区并标记为 empty */
static Value m_ota_erase(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)self; (void)argc; (void)argv;
    LFlashStatus st = l_hal_flash_erase_script();
    return val_bool(st == L_FLASH_OK);
}

/* ================= 5. 原生方法表与单例类定义 ================= */

static const ObjMethod ota_singleton_methods[] = {
    { "update_script",       1, 3, m_ota_update_script,       "OTA.update_script(url, [expected_crc], [auto_exec=true])", "从 HTTP(S) URL 拉取脚本，校验 CRC32 并写入 Flash 热重载" },
    { "update_from_string",  1, 3, m_ota_update_from_string,  "OTA.update_from_string(code, [expected_crc], [auto_exec=true])", "从内存字符串更新 Flash 脚本分区并可选热执行 (Web OTA 核心)" },
    { "status",              0, 0, m_ota_status,              "OTA.status()",                                             "获取当前 Flash 分区有效性、代码长度、CRC32 与内存看板" },
    { "crc32",               1, 1, m_ota_crc32,               "OTA.crc32(data)",                                          "计算字符串或 bytes 数据的 IEEE 802.3 CRC32 校验和" },
    { "sim_set_response",    1, 2, m_ota_sim_set_response,    "OTA.sim_set_response(url, [content])",                    "离线演练: 预设指定 URL 的模拟下载响应 (PC 自动化回归)" },
    { "erase",               0, 0, m_ota_erase,               "OTA.erase()",                                              "格式化擦除裸 Flash 脚本分区" },
};

static void ota_format(const NativeObj *obj, char *buf, size_t size) {
    (void)obj;
    snprintf(buf, size, "<OTA singleton>");
}

static const ObjClass ota_singleton_class = {
    "OTA",
    ota_singleton_methods,
    sizeof(ota_singleton_methods) / sizeof(ota_singleton_methods[0]),
    ota_format,
    NULL
};

/* ================= 6. 全局环境与模块安装 ================= */

void ota_install_globals(Interp *in) {
    NativeObj *otao = obj_new(in, &ota_singleton_class);
    Value otav = val_obj(otao);
    env_define(in->globals, "OTA", otav);
    env_define(in->globals, "ota", otav);
}

Value ota_build_module(Interp *in) {
    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);

    NativeObj *otao = obj_new(in, &ota_singleton_class);
    Value otav = val_obj(otao);
    map_set(m, str_new(in, "OTA", 3), otav);
    map_set(m, str_new(in, "ota", 3), otav);

    gc_pop_roots(in, 1);
    return mv;
}
