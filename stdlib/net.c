/* 标准库: 网络通信与物联网 IoT (WiFi / HTTP / SNTP 网络对时)
 * 所有底层操作经由 l_hal.h 抽象层, 支持 ESP32 原生 WiFi/HTTP 与 PC 宿主环境跨平台运行。 */
#include "../l_config.h"

#if L_WITH_STDLIB_NET

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../interp.h"
#include "../l_hal.h"

static void map_put_bool(Interp *in, Map *m, const char *k, int v) {
    map_set(m, str_new(in, k, strlen(k)), val_bool(v != 0));
}

static void map_put_int(Interp *in, Map *m, const char *k, int32_t v) {
    map_set(m, str_new(in, k, strlen(k)), val_int(v));
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

/* ================= 1. WiFi 管理函数 ================= */

/* wifi_connect(ssid, [pwd], [timeout_ms]): 连接 WiFi 热点 */
static Value f_wifi_connect(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "wifi_connect", argv, 0, V_STR, "string");
    const char *ssid = val_get_cstr(argv[0]);
    const char *pwd  = (argc > 1 && argv[1].type == V_STR) ? val_get_cstr(argv[1]) : NULL;
    int timeout_ms   = (argc > 2 && argv[2].type == V_INT) ? (int)argv[2].as.i : 15000;

    int ret = l_hal_wifi_connect(ssid, pwd, timeout_ms);
    return val_bool(ret == 0);
}

/* wifi_disconnect(): 断开当前 WiFi 连接 */
static Value f_wifi_disconnect(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    return val_bool(l_hal_wifi_disconnect() == 0);
}

/* wifi_is_connected(): 查询 WiFi 是否连接并获取到 IP */
static Value f_wifi_is_connected(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    return val_bool(l_hal_wifi_is_connected() != 0);
}

/* wifi_ssid(): 获取当前连接的 WiFi 名称 (SSID), 未连接返回 nil */
static Value f_wifi_ssid(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    char ssid[64];
    if (l_hal_wifi_is_connected() && l_hal_wifi_get_ssid(ssid, sizeof ssid) == 0 && ssid[0]) {
        return val_str(str_new(in, ssid, strlen(ssid)));
    }
    return val_nil();
}

/* wifi_ip(): 获取当前局域网 IPv4 地址, 未连接返回 nil */
static Value f_wifi_ip(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    char ip[32];
    if (l_hal_wifi_get_ip(ip, sizeof ip) == 0 && ip[0]) {
        return val_str(str_new(in, ip, strlen(ip)));
    }
    return val_nil();
}

/* wifi_mac(): 获取 WiFi 物理 MAC 地址 */
static Value f_wifi_mac(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    char mac[32];
    if (l_hal_wifi_get_mac(mac, sizeof mac) == 0) {
        return val_str(str_new(in, mac, strlen(mac)));
    }
    return val_str(str_new(in, "00:00:00:00:00:00", 17));
}

/* wifi_ap(ssid, [pwd]): 开启 SoftAP 热点模式 */
static Value f_wifi_ap(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "wifi_ap", argv, 0, V_STR, "string");
    const char *ssid = val_get_cstr(argv[0]);
    const char *pwd  = (argc > 1 && argv[1].type == V_STR) ? val_get_cstr(argv[1]) : NULL;

    int ret = l_hal_wifi_start_ap(ssid, pwd);
    return val_bool(ret == 0);
}

/* wifi_scan(): 扫描周围热点, 返回热点信息字典数组 */
static Value f_wifi_scan(Interp *in, const Node *at, int argc, Value *argv) {
    (void)at; (void)argc; (void)argv;
    LWifiApRecord records[16];
    int count = l_hal_wifi_scan(records, 16);
    if (count < 0) count = 0;

    Array *arr = array_new(in);
    Value arr_val = val_array(arr);
    gc_push_root(in, arr_val);

    const char *auth_names[] = {"OPEN", "WEP", "WPA", "WPA2", "WPA_WPA2", "WPA3"};
    for (int i = 0; i < count; i++) {
        Map *m = map_new(in);
        Value mv = val_map(m);
        gc_push_root(in, mv);

        map_put_cstr(in, m, "ssid", records[i].ssid);
        map_put_int(in, m, "rssi", (int32_t)records[i].rssi);
        int auth_idx = records[i].authmode;
        if (auth_idx < 0 || auth_idx >= (int)(sizeof(auth_names)/sizeof(auth_names[0]))) {
            auth_idx = 0;
        }
        map_put_cstr(in, m, "auth", auth_names[auth_idx]);

        array_push(arr, mv);
        gc_pop_roots(in, 1);
    }

    gc_pop_roots(in, 1);
    return arr_val;
}

/* ================= 2. HTTP 请求与 SNTP 网络对时 ================= */

/* http_get(url, [timeout_ms]): 发起 HTTP GET 请求, 返回 {status, body, ok} */
static Value f_http_get(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "http_get", argv, 0, V_STR, "string");
    const char *url = val_get_cstr(argv[0]);
    int timeout_ms  = (argc > 1 && argv[1].type == V_INT) ? (int)argv[1].as.i : 10000;

    LHttpResponse resp = l_hal_http_get(url, NULL, timeout_ms);

    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);

    map_put_int(in, m, "status", (int32_t)resp.status_code);
    map_put_bool(in, m, "ok", resp.status_code >= 200 && resp.status_code < 300);
    if (resp.body) {
        map_put_cstr(in, m, "body", resp.body);
        free(resp.body);
    } else {
        map_put_cstr(in, m, "body", "");
    }
    if (resp.err_msg) {
        map_put_cstr(in, m, "error", resp.err_msg);
        free(resp.err_msg);
    } else {
        map_put_cstr(in, m, "error", NULL);
    }

    gc_pop_roots(in, 1);
    return mv;
}

/* http_post(url, body, [headers], [timeout_ms]): 发起 HTTP POST 请求 */
static Value f_http_post(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "http_post", argv, 0, V_STR, "string");
    const char *url = val_get_cstr(argv[0]);

    const char *body_data = NULL;
    size_t body_len = 0;
    if (argc > 1) {
        if (argv[1].type == V_STR && argv[1].as.str) {
            body_data = argv[1].as.str->chars;
            body_len  = argv[1].as.str->len;
        } else if (argv[1].type == V_BYTES && argv[1].as.bytes) {
            body_data = (const char *)argv[1].as.bytes->data;
            body_len  = argv[1].as.bytes->len;
        }
    }

    const char *headers = (argc > 2 && argv[2].type == V_STR) ? val_get_cstr(argv[2]) : NULL;
    int timeout_ms      = (argc > 3 && argv[3].type == V_INT) ? (int)argv[3].as.i : 10000;

    LHttpResponse resp = l_hal_http_post(url, body_data, body_len, headers, timeout_ms);

    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);

    map_put_int(in, m, "status", (int32_t)resp.status_code);
    map_put_bool(in, m, "ok", resp.status_code >= 200 && resp.status_code < 300);
    if (resp.body) {
        map_put_cstr(in, m, "body", resp.body);
        free(resp.body);
    } else {
        map_put_cstr(in, m, "body", "");
    }
    if (resp.err_msg) {
        map_put_cstr(in, m, "error", resp.err_msg);
        free(resp.err_msg);
    } else {
        map_put_cstr(in, m, "error", NULL);
    }

    gc_pop_roots(in, 1);
    return mv;
}

/* sntp_sync([ntp_server], [timeout_ms]): 网络对时, 同步现实时间 */
static Value f_sntp_sync(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)at;
    const char *server = (argc > 0 && argv[0].type == V_STR) ? val_get_cstr(argv[0]) : "pool.ntp.org";
    int timeout_ms     = (argc > 1 && argv[1].type == V_INT) ? (int)argv[1].as.i : 10000;

    int ret = l_hal_sntp_sync(server, timeout_ms);
    return val_bool(ret == 0);
}

/* ================= 3. 原生函数表与分组注册 ================= */

static const Native net_natives[] = {
    {"wifi_connect",      1, 3, f_wifi_connect,      "wifi_connect(ssid, [pwd], [timeout_ms])", "连接 WiFi 热点, 成功返回 true"},
    {"wifi_disconnect",   0, 0, f_wifi_disconnect,   "wifi_disconnect()",                      "断开当前 WiFi 连接"},
    {"wifi_is_connected",0, 0, f_wifi_is_connected, "wifi_is_connected()",                   "查询 WiFi 是否已连接并获取 IP"},
    {"wifi_ssid",        0, 0, f_wifi_ssid,         "wifi_ssid()",                            "获取当前连接的 WiFi 名称(SSID), 未连接返回 nil"},
    {"wifi_ip",           0, 0, f_wifi_ip,           "wifi_ip()",                              "获取当前局域网 IP, 未连接返回 nil"},
    {"wifi_mac",          0, 0, f_wifi_mac,          "wifi_mac()",                             "获取 WiFi 硬件物理 MAC 地址"},
    {"wifi_ap",           1, 2, f_wifi_ap,           "wifi_ap(ssid, [pwd])",                   "开启 SoftAP 热点模式"},
    {"wifi_scan",         0, 0, f_wifi_scan,         "wifi_scan()",                            "扫描周围 WiFi 热点, 返回字典数组"},
    {"http_get",          1, 2, f_http_get,          "http_get(url, [timeout_ms])",            "发起 HTTP/HTTPS GET 请求, 返回 {status, body, ok}"},
    {"http_post",         1, 4, f_http_post,         "http_post(url, [body], [hdr], [to])",    "发起 HTTP/HTTPS POST 请求, 返回 {status, body, ok}"},
    {"sntp_sync",         0, 2, f_sntp_sync,         "sntp_sync([ntp_server], [timeout_ms])",  "网络对时, 同步现实时间, 成功返回 true"},
};

const NativeGroup group_net = {
    "net", "网络通信与 IoT", net_natives, sizeof net_natives / sizeof net_natives[0]
};

#else
typedef int l_net_disabled_dummy_t;
#endif /* L_WITH_STDLIB_NET */
