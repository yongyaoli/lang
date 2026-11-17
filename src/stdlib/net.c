/* 标准库: 网络通信与物联网 IoT (WiFi / HTTP / SNTP 网络对时)
 * 所有底层操作经由 l_hal.h 抽象层, 支持 ESP32 原生 WiFi/HTTP 与 PC 宿主环境跨平台运行。 */
#include "l_config.h"

#if L_WITH_STDLIB_NET

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interp.h"
#include "l_hal.h"

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

/* wifi_connect(ssid, [pwd], [timeout_ms]): 连接 WiFi 热点
 * 成功返回 true; 失败抛出可被 try/catch 捕获的错误, 携带明确的失败原因诊断 */
static Value f_wifi_connect(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "wifi_connect", argv, 0, V_STR, "string");
    const char *ssid = val_get_cstr(argv[0]);
    const char *pwd  = (argc > 1 && argv[1].type == V_STR) ? val_get_cstr(argv[1]) : NULL;
    int timeout_ms   = (argc > 2 && argv[2].type == V_INT) ? (int)argv[2].as.i : 15000;

    int ret = l_hal_wifi_connect(ssid, pwd, timeout_ms);
    if (ret != 0) {
        const char *err = l_hal_wifi_last_error();
        if (!err || !*err) err = "未知错误";
        rt_error(in, at,
                 "wifi_connect() 连接 WiFi \"%s\" 失败: %s (已等待 %dms)。"
                 "排查建议: 用 wifi_scan() 确认热点存在; 检查密码是否正确; 靠近路由器重试",
                 ssid, err, timeout_ms > 0 ? timeout_ms : 15000);
    }
    return val_bool(1);
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

/* wifi_ap(ssid, [pwd]): 开启 SoftAP 热点模式
 * 成功返回 true; 失败抛出带明确原因的可捕获错误 */
static Value f_wifi_ap(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "wifi_ap", argv, 0, V_STR, "string");
    const char *ssid = val_get_cstr(argv[0]);
    const char *pwd  = (argc > 1 && argv[1].type == V_STR) ? val_get_cstr(argv[1]) : NULL;

    int ret = l_hal_wifi_start_ap(ssid, pwd);
    if (ret != 0) {
        const char *err = l_hal_wifi_last_error();
        if (!err || !*err) err = "未知错误";
        rt_error(in, at, "wifi_ap() 开启热点 \"%s\" 失败: %s。"
                 "排查建议: 密码(如有)需至少 8 位; 手机端确认能扫描到该热点",
                 ssid, err);
    }
    return val_bool(1);
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

/* sntp_sync([ntp_server], [timeout_ms]): 网络对时, 同步现实时间
 * 成功返回 true; 失败抛出带明确原因的可捕获错误 (未联网/超时) */
static Value f_sntp_sync(Interp *in, const Node *at, int argc, Value *argv) {
    const char *server = (argc > 0 && argv[0].type == V_STR) ? val_get_cstr(argv[0]) : "pool.ntp.org";
    int timeout_ms     = (argc > 1 && argv[1].type == V_INT) ? (int)argv[1].as.i : 10000;

    if (!l_hal_wifi_is_connected()) {
        rt_error(in, at, "sntp_sync() 对时失败: 当前未连接 WiFi。"
                 "请先调用 wifi_connect(ssid, pwd) 联网后再对时");
    }
    int ret = l_hal_sntp_sync(server, timeout_ms);
    if (ret != 0) {
        rt_error(in, at,
                 "sntp_sync() 对时失败: %dms 内未从 NTP 服务器 \"%s\" 获取到时间。"
                 "排查建议: 确认网络可访问外网 (可用 http_get 测试); 更换 NTP 服务器如 \"ntp.aliyun.com\"",
                 timeout_ms > 0 ? timeout_ms : 10000, server);
    }
    return val_bool(1);
}

/* ================= 3. TCP Socket 客户端 (TCP 类) =================
 * Windows 走 WinSock2, Linux/macOS 宿主与 ESP-IDF (lwip) 走 BSD Socket。
 * 底层实现位于 stdlib/tcp_win.c / tcp_posix.c (独立翻译单元, 避免引入
 * windows.h 与词法 Token 枚举冲突), 本文件只使用 l_tcp.h 薄接口。
 * 遵循语言规范: 所有失败都给出明确原因的可捕获错误, 供脚本 try/catch 诊断。 */
#include "l_tcp.h"

#define TCP_MAX_CHUNK (64 * 1024)

typedef struct TcpHandle {
    l_tcp_sock_t sock;
    int connected;
    int port;
    char host[96];
} TcpHandle;

static TcpHandle *tcp_handle(NativeObj *o) { return (TcpHandle *)o->extra; }

static void tcp_close_value(Value self) {
    TcpHandle *h = tcp_handle(self.as.obj);
    if (h && h->connected) {
        l_tcp_close(h->sock);
        h->sock = -1;
        h->connected = 0;
    }
}

static void tcp_obj_destroy(NativeObj *o) {
    TcpHandle *h = tcp_handle(o);
    if (h) {
        if (h->connected) l_tcp_close(h->sock);
        free(h);
        o->extra = NULL;
    }
}

static void tcp_format(const NativeObj *obj, char *buf, size_t size) {
    TcpHandle *h = tcp_handle((NativeObj *)obj);
    snprintf(buf, size, "<Tcp %s:%d %s>",
             h ? h->host : "?", h ? h->port : 0,
             h && h->connected ? "已连接" : "已关闭");
}

/* send(数据): 发送字符串或 bytes, 返回自身以支持链式调用 */
static Value m_tcp_send(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    NativeObj *o = self.as.obj;
    TcpHandle *h = tcp_handle(o);
    if (!h || !h->connected) {
        rt_error(in, at, "Tcp.send() 失败: 连接已关闭, 无法发送。请重新 Tcp(host, port) 建立连接");
    }
    const uint8_t *data;
    size_t len;
    if (argv[0].type == V_BYTES) {
        Bytes *b = argv[0].as.bytes;
        data = b ? b->data : (const uint8_t *)"";
        len  = b ? b->len : 0;
    } else {
        need_arg(in, at, "Tcp.send", argv, 0, V_STR, "string 或 bytes");
        const char *s = val_get_cstr(argv[0]);
        data = (const uint8_t *)s;
        len  = strlen(s);
    }
    char err[160] = "";
    if (l_tcp_send_all(h->sock, data, len, err, sizeof(err)) != 0) {
        tcp_close_value(self);
        rt_error(in, at, "Tcp.send() 发送失败: %s, 连接可能已被对方关闭", err);
    }
    return self;
}

/* recv([最大字节数=1024], [超时毫秒=2000]): 收到数据返回字符串, 超时或对方关闭返回 nil */
static Value m_tcp_recv(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    int maxb = (argc > 0 && argv[0].type == V_INT) ? (int)argv[0].as.i : 1024;
    int tmo  = (argc > 1 && argv[1].type == V_INT) ? (int)argv[1].as.i : 2000;
    if (maxb < 1) maxb = 1;
    if (maxb > TCP_MAX_CHUNK) maxb = TCP_MAX_CHUNK;
    NativeObj *o = self.as.obj;
    TcpHandle *h = tcp_handle(o);
    if (!h || !h->connected) {
        rt_error(in, at, "Tcp.recv() 失败: 连接已关闭, 无法接收。请重新 Tcp(host, port) 建立连接");
    }
    char buf[TCP_MAX_CHUNK];
    char err[160] = "";
    int n = l_tcp_recv(h->sock, buf, (size_t)maxb, tmo, err, sizeof(err));
    if (n > 0) return val_str(str_new(in, buf, (size_t)n));
    if (n == 0) return val_nil();                 /* 超时: 明确返回 nil (不抛错, 便于轮询) */
    if (n == -1) {                                /* 对方关闭 */
        tcp_close_value(self);
        return val_nil();
    }
    rt_error(in, at, "Tcp.recv() 接收出错: %s", err);
    return val_nil();
}

/* recv_bytes([最大字节数=1024], [超时毫秒=2000]): 二进制安全接收, 语义同 recv() */
static Value m_tcp_recv_bytes(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    int maxb = (argc > 0 && argv[0].type == V_INT) ? (int)argv[0].as.i : 1024;
    int tmo  = (argc > 1 && argv[1].type == V_INT) ? (int)argv[1].as.i : 2000;
    if (maxb < 1) maxb = 1;
    if (maxb > TCP_MAX_CHUNK) maxb = TCP_MAX_CHUNK;
    NativeObj *o = self.as.obj;
    TcpHandle *h = tcp_handle(o);
    if (!h || !h->connected) {
        rt_error(in, at, "Tcp.recv_bytes() 失败: 连接已关闭, 无法接收。请重新 Tcp(host, port) 建立连接");
    }
    uint8_t buf[TCP_MAX_CHUNK];
    char err[160] = "";
    int n = l_tcp_recv(h->sock, buf, (size_t)maxb, tmo, err, sizeof(err));
    if (n > 0) return val_bytes(bytes_new(in, (size_t)n, buf));
    if (n == 0 || n == -1) {
        if (n == -1) tcp_close_value(self);
        return val_nil();
    }
    rt_error(in, at, "Tcp.recv_bytes() 接收出错: %s", err);
    return val_nil();
}

static Value m_tcp_is_connected(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    TcpHandle *h = tcp_handle(self.as.obj);
    return val_bool(h && h->connected);
}

/* close(): 主动关闭连接 (对象被 GC 回收时也会自动关闭) */
static Value m_tcp_close(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)argc; (void)argv;
    tcp_close_value(self);
    (void)at;
    return self;
}

static Value m_tcp_info(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc; (void)argv;
    (void)at;
    TcpHandle *h = tcp_handle(self.as.obj);
    Map *m = map_new(in);
    map_put_cstr(in, m, "host", h ? h->host : "");
    map_put_int(in, m, "port", h ? h->port : 0);
    map_put_bool(in, m, "connected", h && h->connected);
    return val_map(m);
}

static const ObjMethod tcp_methods[] = {
    {"send",         1, 1, m_tcp_send,         "tcp.send(数据)",                     "发送字符串或 bytes (自动分包直到发完), 返回自身支持链式"},
    {"recv",         0, 2, m_tcp_recv,         "tcp.recv([最大字节=1024], [超时=2000])", "接收文本, 超时或对方关闭返回 nil (便于轮询), 出错抛明确错误"},
    {"recv_bytes",   0, 2, m_tcp_recv_bytes,   "tcp.recv_bytes([最大字节], [超时])",   "二进制安全接收 bytes, 语义同 recv()"},
    {"is_connected", 0, 0, m_tcp_is_connected, "tcp.is_connected()",                 "查询连接是否仍然有效"},
    {"close",        0, 0, m_tcp_close,        "tcp.close()",                        "主动关闭连接, 返回自身; GC 回收对象时也会自动关闭"},
    {"info",         0, 0, m_tcp_info,         "tcp.info()",                         "返回 {host, port, connected} 字典"},
};

static const ObjClass tcp_class = {
    "Tcp",
    tcp_methods,
    sizeof tcp_methods / sizeof tcp_methods[0],
    tcp_format,
    tcp_obj_destroy
};

/* Tcp(host, port, [timeout_ms=5000]): 创建 TCP 客户端并立即连接
 * 失败抛出带明确原因的可捕获错误 (DNS 解析失败/连接被拒绝/超时) */
static Value f_tcp(Interp *in, const Node *at, int argc, Value *argv) {
    need_arg(in, at, "Tcp", argv, 0, V_STR, "string");
    int port = int_arg(in, at, "Tcp", argv, 1);
    if (port < 1 || port > 65535) {
        rt_error(in, at, "Tcp() 参数错误: 端口必须在 1..65535 之间, 实际是 %d", port);
    }
    int timeout_ms = (argc > 2 && argv[2].type == V_INT) ? (int)argv[2].as.i : 5000;
    const char *host = val_get_cstr(argv[0]);
    if (!*host) {
        rt_error(in, at, "Tcp() 参数错误: 主机名不能为空");
    }

    char err[160] = "";
    l_tcp_sock_t sock = l_tcp_connect(host, port, timeout_ms, err, sizeof(err));
    if (sock < 0) {
        rt_error(in, at,
                 "Tcp() 连接 %s:%d 失败: %s。"
                 "排查建议: 确认服务端已在目标端口监听; 检查防火墙; 确认已联网",
                 host, port, err);
    }

    NativeObj *o = obj_new(in, &tcp_class);
    o->extra = malloc(sizeof(TcpHandle));
    if (!o->extra) {
        l_tcp_close(sock);
        rt_error(in, at, "Tcp() 内存不足, 无法创建连接对象");
    }
    TcpHandle *h = tcp_handle(o);
    memset(h, 0, sizeof(*h));
    h->sock = sock;
    h->connected = 1;
    h->port = port;
    snprintf(h->host, sizeof(h->host), "%s", host);
    return val_obj(o);
}

/* ================= 3.2 TcpServer 服务端实现 ================= */

typedef struct {
    l_tcp_sock_t sock;
    int listening;
    int port;
    char host[64];
} TcpServerHandle;

static TcpServerHandle *tcpserver_handle(NativeObj *o) {
    return (TcpServerHandle *)o->extra;
}

static void tcpserver_obj_destroy(NativeObj *o) {
    if (!o || !o->extra) return;
    TcpServerHandle *h = tcpserver_handle(o);
    if (h->listening && h->sock >= 0) {
        l_tcp_close(h->sock);
        h->sock = -1;
        h->listening = 0;
    }
    free(h);
    o->extra = NULL;
}

static void tcpserver_format(const NativeObj *obj, char *buf, size_t size) {
    TcpServerHandle *h = (TcpServerHandle *)obj->extra;
    if (h && h->listening) {
        snprintf(buf, size, "<TcpServer %s:%d 监听中>", h->host, h->port);
    } else {
        snprintf(buf, size, "<TcpServer 已关闭>");
    }
}

/* 为 accept 到的客户端套接字创建 Tcp 对象 */
static Value tcpserver_make_client_obj(Interp *in, l_tcp_sock_t sock, const char *client_ip, int client_port) {
    NativeObj *o = obj_new(in, &tcp_class);
    o->extra = malloc(sizeof(TcpHandle));
    if (!o->extra) {
        l_tcp_close(sock);
        return val_nil();
    }
    TcpHandle *h = tcp_handle(o);
    memset(h, 0, sizeof(*h));
    h->sock = sock;
    h->connected = 1;
    h->port = client_port;
    snprintf(h->host, sizeof(h->host), "%s", client_ip ? client_ip : "unknown");
    return val_obj(o);
}

/* accept([超时毫秒=1000]): 等待并接受新客户端连接, 超时返回 nil (便于主循环与心跳共存), 成功返回 Tcp 客户端对象 */
static Value m_tcpserver_accept(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    int timeout_ms = (argc > 0 && argv[0].type == V_INT) ? (int)argv[0].as.i : 1000;
    NativeObj *o = self.as.obj;
    TcpServerHandle *h = tcpserver_handle(o);
    if (!h || !h->listening) {
        rt_error(in, at, "TcpServer.accept() 失败: 服务端已关闭或未在监听");
    }

    char client_ip[64] = "";
    int client_port = 0;
    char err[160] = "";

    l_tcp_sock_t client_sock = l_tcp_accept(h->sock, timeout_ms, client_ip, sizeof(client_ip), &client_port, err, sizeof(err));
    if (client_sock > 0) {
        Value client_val = tcpserver_make_client_obj(in, client_sock, client_ip, client_port);
        if (client_val.type == V_NIL) {
            rt_error(in, at, "TcpServer.accept() 内存不足, 无法分配客户端连接对象");
        }
        return client_val;
    } else if (client_sock == 0) {
        return val_nil(); /* 超时, 无连接进站 */
    } else {
        rt_error(in, at, "TcpServer.accept() 监听接收失败: %s", err);
        return val_nil();
    }
}

/* is_listening(): 查询服务端是否处于监听中 */
static Value m_tcpserver_is_listening(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    TcpServerHandle *h = tcpserver_handle(self.as.obj);
    return val_bool(h && h->listening);
}

/* close(): 主动关闭监听端口并释放资源 */
static Value m_tcpserver_close(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    TcpServerHandle *h = tcpserver_handle(self.as.obj);
    if (h && h->listening && h->sock >= 0) {
        l_tcp_close(h->sock);
        h->sock = -1;
        h->listening = 0;
    }
    return self;
}

/* info(): 返回 {host, port, listening} 状态字典 */
static Value m_tcpserver_info(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc; (void)argv; (void)at;
    TcpServerHandle *h = tcpserver_handle(self.as.obj);
    Map *m = map_new(in);
    map_put_cstr(in, m, "host", h ? h->host : "");
    map_put_int(in, m, "port", h ? h->port : 0);
    map_put_bool(in, m, "listening", h && h->listening);
    return val_map(m);
}

static const ObjMethod tcpserver_methods[] = {
    {"accept",       0, 1, m_tcpserver_accept,       "tcpserver.accept([超时毫秒=1000])", "等待并接入新客户端, 超时返回 nil 便于轮询, 成功返回 Tcp 实例"},
    {"is_listening", 0, 0, m_tcpserver_is_listening, "tcpserver.is_listening()",          "查询服务端是否处于监听中"},
    {"close",        0, 0, m_tcpserver_close,        "tcpserver.close()",                 "主动关闭监听套接字"},
    {"info",         0, 0, m_tcpserver_info,         "tcpserver.info()",                  "返回 {host, port, listening} 字典"},
};

static const ObjClass tcpserver_class = {
    "TcpServer",
    tcpserver_methods,
    sizeof tcpserver_methods / sizeof tcpserver_methods[0],
    tcpserver_format,
    tcpserver_obj_destroy
};

/* TcpServer(port, [host="0.0.0.0"], [backlog=5]) 或 TcpServer(host, port, [backlog=5])
 * 创建 TCP 服务端并立即绑定监听 */
static Value f_tcpserver(Interp *in, const Node *at, int argc, Value *argv) {
    if (argc < 1) {
        rt_error(in, at, "TcpServer() 缺少参数: 至少需要指定监听端口");
    }

    int port = 0;
    const char *host = "0.0.0.0";
    int backlog = 5;

    if (argv[0].type == V_INT) {
        port = (int)argv[0].as.i;
        if (argc > 1 && argv[1].type == V_STR) {
            host = val_get_cstr(argv[1]);
        }
        if (argc > 2 && argv[2].type == V_INT) {
            backlog = (int)argv[2].as.i;
        }
    } else if (argv[0].type == V_STR) {
        host = val_get_cstr(argv[0]);
        if (argc < 2 || argv[1].type != V_INT) {
            rt_error(in, at, "TcpServer() 参数错误: 传入 host 时第二参数必须为端口 int");
        }
        port = (int)argv[1].as.i;
        if (argc > 2 && argv[2].type == V_INT) {
            backlog = (int)argv[2].as.i;
        }
    } else {
        rt_error(in, at, "TcpServer() 参数错误: 第一参数必须为端口数字或主机名字符串");
    }

    if (port < 1 || port > 65535) {
        rt_error(in, at, "TcpServer() 端口必须在 1..65535 之间, 实际为 %d", port);
    }
    if (backlog <= 0) backlog = 5;

    char err[160] = "";
    l_tcp_sock_t sock = l_tcp_listen(host, port, backlog, err, sizeof(err));
    if (sock < 0) {
        rt_error(in, at, "TcpServer() 绑定监听 %s:%d 失败: %s。排查建议: 确认端口未被其他程序占用", host, port, err);
    }

    NativeObj *o = obj_new(in, &tcpserver_class);
    o->extra = malloc(sizeof(TcpServerHandle));
    if (!o->extra) {
        l_tcp_close(sock);
        rt_error(in, at, "TcpServer() 内存不足, 无法创建服务端对象");
    }
    TcpServerHandle *h = tcpserver_handle(o);
    memset(h, 0, sizeof(*h));
    h->sock = sock;
    h->listening = 1;
    h->port = port;
    snprintf(h->host, sizeof(h->host), "%s", host);
    return val_obj(o);
}

/* ================= 4. 原生函数表与分组注册 ================= */

static const Native net_natives[] = {
    {"wifi_connect",      1, 3, f_wifi_connect,      "wifi_connect(ssid, [pwd], [timeout_ms])", "连接 WiFi 热点, 成功返回 true, 失败抛出带原因的可捕获错误"},
    {"wifi_disconnect",   0, 0, f_wifi_disconnect,   "wifi_disconnect()",                      "断开当前 WiFi 连接"},
    {"wifi_is_connected",0, 0, f_wifi_is_connected, "wifi_is_connected()",                   "查询 WiFi 是否已连接并获取 IP"},
    {"wifi_ssid",        0, 0, f_wifi_ssid,         "wifi_ssid()",                            "获取当前连接的 WiFi 名称(SSID), 未连接返回 nil"},
    {"wifi_ip",           0, 0, f_wifi_ip,           "wifi_ip()",                              "获取当前局域网 IP, 未连接返回 nil"},
    {"wifi_mac",          0, 0, f_wifi_mac,          "wifi_mac()",                             "获取 WiFi 硬件物理 MAC 地址"},
    {"wifi_ap",           1, 2, f_wifi_ap,           "wifi_ap(ssid, [pwd])",                   "开启 SoftAP 热点模式, 失败抛出带原因的可捕获错误"},
    {"wifi_scan",         0, 0, f_wifi_scan,         "wifi_scan()",                            "扫描周围 WiFi 热点, 返回字典数组"},
    {"http_get",          1, 2, f_http_get,          "http_get(url, [timeout_ms])",            "发起 HTTP/HTTPS GET 请求, 返回 {status, body, ok, error}"},
    {"http_post",         1, 4, f_http_post,         "http_post(url, [body], [hdr], [to])",    "发起 HTTP/HTTPS POST 请求, 返回 {status, body, ok, error}"},
    {"sntp_sync",         0, 2, f_sntp_sync,         "sntp_sync([ntp_server], [timeout_ms])",  "网络对时, 成功返回 true, 失败抛出带原因的可捕获错误"},
    {"Tcp",               2, 3, f_tcp,               "Tcp(host, port, [timeout_ms=5000])",     "创建 TCP 客户端并连接, 失败抛出带原因的可捕获错误; 方法: send/recv/recv_bytes/close/info"},
    {"TcpServer",         1, 3, f_tcpserver,         "TcpServer(port, [host=\"0.0.0.0\"], [backlog=5])", "创建 TCP 服务端并监听端口; 方法: accept/is_listening/close/info"},
};

const NativeGroup group_net = {
    "net", "网络通信与 IoT", net_natives, sizeof net_natives / sizeof net_natives[0]
};

#else
typedef int l_net_disabled_dummy_t;
#endif /* L_WITH_STDLIB_NET */
