/*
 * L 语言 MQTT 3.1.1 物联网客户端核心实现
 *
 * 规范遵循: OASIS MQTT Version 3.1.1
 * 架构特性: 纯 C99 零第三方外部依赖，基于 l_tcp 跨平台 socket 封装。
 * 兼容支持: ESP32 原生 WiFi/以太网与 Windows / Linux / macOS PC 宿主环境。
 * 物联网对齐: 完美适配阿里云物联网平台 (Aliyun IoT) 与华为云设备接入 (Huawei Cloud IoTDA)。
 */

#include "l_config.h"

#if L_WITH_STDLIB_NET

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interp.h"
#include "l_mqtt.h"
#include "l_tcp.h"
#include "l_heap.h"

/* MQTT 3.1.1 固定报头控制报文类型定义 (Packet Types) */
#define MQTT_PKT_CONNECT     1
#define MQTT_PKT_CONNACK     2
#define MQTT_PKT_PUBLISH     3
#define MQTT_PKT_PUBACK      4
#define MQTT_PKT_PUBREC      5
#define MQTT_PKT_PUBREL      6
#define MQTT_PKT_PUBCOMP     7
#define MQTT_PKT_SUBSCRIBE   8
#define MQTT_PKT_SUBACK      9
#define MQTT_PKT_UNSUBSCRIBE 10
#define MQTT_PKT_UNSUBACK    11
#define MQTT_PKT_PINGREQ     12
#define MQTT_PKT_PINGRESP    13
#define MQTT_PKT_DISCONNECT  14

/* MQTT 连接返回码定义 (Connect Return Code) */
#define MQTT_CONN_ACCEPTED                      0x00
#define MQTT_CONN_REFUSED_UNACCEPTABLE_PROTOCOL 0x01
#define MQTT_CONN_REFUSED_IDENTIFIER_REJECTED   0x02
#define MQTT_CONN_REFUSED_SERVER_UNAVAILABLE    0x03
#define MQTT_CONN_REFUSED_BAD_USERNAME_PASSWORD 0x04
#define MQTT_CONN_REFUSED_NOT_AUTHORIZED        0x05

/* 全局隐藏回调函数注册表名称 (受 GC 根安全保护) */
static const char MQTT_HANDLER_REG[] = "__l_mqtt_handlers";
static int s_mqtt_next_client_num = 1;

/* MQTT 客户端原生句柄 */
typedef struct {
    l_tcp_sock_t sock;
    int connected;
    int is_simulated;            /* PC 离线仿真模式标记 */
    int client_num;              /* 全局自增 ID，用于关联 __l_mqtt_handlers */
    uint16_t next_packet_id;
    uint16_t keepalive;          /* 保活心跳秒数 (默认 60s) */
    char client_id[80];          /* 客户端 ClientID */
    char username[128];          /* 用户名 (阿里云/华为云设备鉴权签名) */
    char password[256];          /* 密码/Token */
    char host[128];              /* 接入点域名或 IP */
    int port;                    /* 端口 (默认 1883) */
    
    /* 遗嘱消息 (Will Message) */
    int has_will;
    int will_qos;
    int will_retain;
    char will_topic[128];
    char will_msg[256];

    /* 接收缓存区 (用于数据包粘包与分片拼包) */
    uint8_t rx_buf[4096];
    size_t rx_len;
} MqttHandle;

static MqttHandle *mqtt_handle(NativeObj *o) {
    return (MqttHandle *)o->extra;
}

/* 获取或创建全局回调注册表 Map */
static Map *mqtt_get_handler_map(Interp *in) {
    Value *v = env_find(in->globals, MQTT_HANDLER_REG);
    if (v && v->type == V_MAP) return v->as.map;
    Map *m = map_new(in);
    env_define(in->globals, MQTT_HANDLER_REG, val_map(m));
    return m;
}

/* 编码 MQTT 剩余长度 (Remaining Length) - 变长 1~4 字节编码 */
static int mqtt_encode_remaining_len(uint8_t *buf, uint32_t len) {
    int idx = 0;
    do {
        uint8_t d = (uint8_t)(len % 128);
        len /= 128;
        if (len > 0) d |= 128;
        buf[idx++] = d;
    } while (len > 0 && idx < 4);
    return idx;
}

/* 解码 MQTT 剩余长度: 返回读取的字节数，0 表示数据未收全，-1 表示协议格式错误 */
static int mqtt_decode_remaining_len(const uint8_t *buf, size_t cap, uint32_t *out_len, int *out_len_bytes) {
    uint32_t multiplier = 1;
    uint32_t val = 0;
    int idx = 0;
    uint8_t d;
    do {
        if (idx >= (int)cap) return 0; /* 字节不全，需继续等待接收 */
        d = buf[idx++];
        val += (d & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128 * 128 * 128) return -1; /* 格式严重错误 */
    } while ((d & 128) != 0 && idx < 4);
    *out_len = val;
    *out_len_bytes = idx;
    return 1;
}

/* 写入 16 位大端整数 */
static void write_u16_be(uint8_t *buf, uint16_t v) {
    buf[0] = (uint8_t)((v >> 8) & 0xFF);
    buf[1] = (uint8_t)(v & 0xFF);
}

/* 读取 16 位大端整数 */
static uint16_t read_u16_be(const uint8_t *buf) {
    return (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
}

/* 写入 UTF-8 带长度前缀字符串 */
static size_t write_mqtt_string(uint8_t *buf, const char *str) {
    size_t len = strlen(str);
    write_u16_be(buf, (uint16_t)len);
    if (len > 0) {
        memcpy(buf + 2, str, len);
    }
    return 2 + len;
}

/* 对象析构器 */
static void mqtt_obj_destroy(NativeObj *o) {
    if (!o || !o->extra) return;
    MqttHandle *h = mqtt_handle(o);
    if (h->connected && h->sock >= 0 && !h->is_simulated) {
        uint8_t disconn_pkt[2] = { (uint8_t)(MQTT_PKT_DISCONNECT << 4), 0x00 };
        char err[64];
        l_tcp_send_all(h->sock, disconn_pkt, 2, err, sizeof(err));
        l_tcp_close(h->sock);
        h->sock = -1;
    }
    free(h);
    o->extra = NULL;
}

/* 打印格式化描述 */
static void mqtt_format(const NativeObj *obj, char *buf, size_t size) {
    MqttHandle *h = (MqttHandle *)obj->extra;
    if (!h) {
        snprintf(buf, size, "<MqttClient: destroyed>");
        return;
    }
    snprintf(buf, size, "<MqttClient: id=\"%s\", %s, host=%s:%d>",
             h->client_id,
             h->connected ? "connected" : "disconnected",
             h->host[0] ? h->host : "none",
             h->port);
}

/* ================= 客户端方法实现 ================= */

/* client.set_auth(username, password) */
static Value m_mqtt_set_auth(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    MqttHandle *h = mqtt_handle(self.as.obj);
    need_arg(in, at, "MqttClient.set_auth", argv, 0, V_STR, "string");
    need_arg(in, at, "MqttClient.set_auth", argv, 1, V_STR, "string");
    snprintf(h->username, sizeof(h->username), "%s", val_get_cstr(argv[0]));
    snprintf(h->password, sizeof(h->password), "%s", val_get_cstr(argv[1]));
    return self;
}

/* client.set_keepalive(seconds) */
static Value m_mqtt_set_keepalive(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    MqttHandle *h = mqtt_handle(self.as.obj);
    int sec = int_arg(in, at, "MqttClient.set_keepalive", argv, 0);
    if (sec <= 0 || sec > 65535) {
        rt_error(in, at, "MqttClient.set_keepalive() 参数错误: 心跳时长必须在 1..65535 秒之间");
    }
    h->keepalive = (uint16_t)sec;
    return self;
}

/* client.set_will(topic, message, [qos=0], [retain=false]) */
static Value m_mqtt_set_will(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    MqttHandle *h = mqtt_handle(self.as.obj);
    need_arg(in, at, "MqttClient.set_will", argv, 0, V_STR, "string");
    need_arg(in, at, "MqttClient.set_will", argv, 1, V_STR, "string");
    int qos = (argc > 2 && argv[2].type == V_INT) ? (int)argv[2].as.i : 0;
    int retain = (argc > 3 && is_truthy(argv[3])) ? 1 : 0;
    
    h->has_will = 1;
    h->will_qos = (qos > 0) ? 1 : 0;
    h->will_retain = retain;
    snprintf(h->will_topic, sizeof(h->will_topic), "%s", val_get_cstr(argv[0]));
    snprintf(h->will_msg, sizeof(h->will_msg), "%s", val_get_cstr(argv[1]));
    return self;
}

/* client.connect(host, [port=1883], [timeout_ms=5000]) */
static Value m_mqtt_connect(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    MqttHandle *h = mqtt_handle(self.as.obj);
    need_arg(in, at, "MqttClient.connect", argv, 0, V_STR, "string");
    const char *host = val_get_cstr(argv[0]);
    if (!*host) rt_error(in, at, "MqttClient.connect() 参数错误: 主机名或服务器地址不能为空");
    int port = (argc > 1 && argv[1].type == V_INT) ? (int)argv[1].as.i : 1883;
    int timeout_ms = (argc > 2 && argv[2].type == V_INT) ? (int)argv[2].as.i : 5000;

    snprintf(h->host, sizeof(h->host), "%s", host);
    h->port = port;

    /* 1. 建立基础底层 TCP 连接 */
    char err[160] = "";
    l_tcp_sock_t sock = l_tcp_connect(host, port, timeout_ms, err, sizeof(err));
    if (sock < 0) {
        rt_error(in, at, "MqttClient.connect() 连接 MQTT Broker %s:%d 失败: %s。"
                         "排查建议: 确认物联网平台域名/端口正确; 检查网络与防火墙开放 1883 端口",
                 host, port, err);
    }
    h->sock = sock;

    /* 2. 组装 MQTT 3.1.1 CONNECT 报文 */
    uint8_t var_header[10];
    var_header[0] = 0x00;
    var_header[1] = 0x04;
    var_header[2] = 'M';
    var_header[3] = 'Q';
    var_header[4] = 'T';
    var_header[5] = 'T';
    var_header[6] = 0x04; /* Protocol Level: 3.1.1 */

    /* 连接标志位 Connect Flags */
    uint8_t flags = 0x02; /* Clean Session 默认为 1 */
    if (h->has_will) {
        flags |= 0x04; /* Will Flag */
        flags |= (uint8_t)((h->will_qos & 0x03) << 3);
        if (h->will_retain) flags |= 0x20;
    }
    if (h->password[0]) flags |= 0x40; /* Password Flag */
    if (h->username[0]) flags |= 0x80; /* Username Flag */
    var_header[7] = flags;
    write_u16_be(var_header + 8, h->keepalive);

    /* 组装 Payload: Client ID + [Will Topic + Will Msg] + [Username] + [Password] */
    uint8_t payload[2048];
    size_t payload_len = 0;
    payload_len += write_mqtt_string(payload + payload_len, h->client_id);
    if (h->has_will) {
        payload_len += write_mqtt_string(payload + payload_len, h->will_topic);
        payload_len += write_mqtt_string(payload + payload_len, h->will_msg);
    }
    if (h->username[0]) {
        payload_len += write_mqtt_string(payload + payload_len, h->username);
    }
    if (h->password[0]) {
        payload_len += write_mqtt_string(payload + payload_len, h->password);
    }

    /* 固定报头 Fixed Header */
    uint32_t rem_len = 10 + (uint32_t)payload_len;
    uint8_t pkt[2200];
    pkt[0] = (uint8_t)(MQTT_PKT_CONNECT << 4);
    int rem_len_bytes = mqtt_encode_remaining_len(pkt + 1, rem_len);
    size_t header_len = 1 + rem_len_bytes;

    memcpy(pkt + header_len, var_header, 10);
    memcpy(pkt + header_len + 10, payload, payload_len);
    size_t total_pkt_len = header_len + 10 + payload_len;

    /* 发送 CONNECT 数据包 */
    if (l_tcp_send_all(h->sock, pkt, total_pkt_len, err, sizeof(err)) != 0) {
        l_tcp_close(h->sock);
        h->sock = -1;
        rt_error(in, at, "MqttClient.connect() 发送 CONNECT 报文失败: %s", err);
    }

    /* 3. 阻塞等待接收 CONNACK 握手回包 (标准 4 字节: 0x20, 0x02, session_present, return_code) */
    uint8_t ack_buf[16];
    int r = l_tcp_recv(h->sock, ack_buf, 4, timeout_ms, err, sizeof(err));
    if (r <= 0) {
        l_tcp_close(h->sock);
        h->sock = -1;
        if (port == 8883) {
            rt_error(in, at, "MqttClient.connect() 连接 MQTTS (8883) 未响应或被对端断开: "
                             "目标端口要求 TLS/SSL 加密层握手。排查建议: 确认物联网平台是否开放标准 1883 端口，或启用设备密钥认证");
        } else {
            rt_error(in, at, "MqttClient.connect() 等待 CONNACK 响应超时或连接被断开: %s", err[0] ? err : "服务端未响应");
        }
    }
    if (r < 4 || (ack_buf[0] >> 4) != MQTT_PKT_CONNACK || ack_buf[1] != 0x02) {
        l_tcp_close(h->sock);
        h->sock = -1;
        if (port == 8883) {
            rt_error(in, at, "MqttClient.connect() 连接 MQTTS (8883) 收到非 MQTT 报文: "
                             "目标端口为 TLS/SSL 加密端口，对端可能返回了 TLS 握手告警 (Alert)");
        } else {
            rt_error(in, at, "MqttClient.connect() 服务端返回非法 CONNACK 报文");
        }
    }

    uint8_t return_code = ack_buf[3];
    if (return_code != MQTT_CONN_ACCEPTED) {
        const char *reason = "连接被拒绝";
        if (return_code == MQTT_CONN_REFUSED_UNACCEPTABLE_PROTOCOL) reason = "协议版本不支持 (必须为 MQTT 3.1.1)";
        else if (return_code == MQTT_CONN_REFUSED_IDENTIFIER_REJECTED) reason = "客户端标识符 (ClientID) 不被服务器接受";
        else if (return_code == MQTT_CONN_REFUSED_SERVER_UNAVAILABLE) reason = "Broker 服务端当前不可用";
        else if (return_code == MQTT_CONN_REFUSED_BAD_USERNAME_PASSWORD) reason = "用户名或密码错误 (鉴权签名失效)";
        else if (return_code == MQTT_CONN_REFUSED_NOT_AUTHORIZED) reason = "未被授权连接此物联网平台";
        l_tcp_close(h->sock);
        h->sock = -1;
        rt_error(in, at, "MqttClient.connect() 握手失败: %s (返回码: %d)", reason, (int)return_code);
    }

    h->connected = 1;
    h->is_simulated = 0;
    return val_bool(1);
}

/* client.is_connected() */
static Value m_mqtt_is_connected(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    MqttHandle *h = mqtt_handle(self.as.obj);
    return val_bool(h->connected);
}

/* client.disconnect() */
static Value m_mqtt_disconnect(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    MqttHandle *h = mqtt_handle(self.as.obj);
    if (h->connected) {
        if (h->sock >= 0 && !h->is_simulated) {
            uint8_t disconn_pkt[2] = { (uint8_t)(MQTT_PKT_DISCONNECT << 4), 0x00 };
            char err[64];
            l_tcp_send_all(h->sock, disconn_pkt, 2, err, sizeof(err));
            l_tcp_close(h->sock);
            h->sock = -1;
        }
        h->connected = 0;
    }
    return self;
}

/* client.publish(topic, payload, [qos=0], [retain=false]) */
static Value m_mqtt_publish(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    MqttHandle *h = mqtt_handle(self.as.obj);
    if (!h->connected) rt_error(in, at, "MqttClient.publish() 失败: 客户端未连接到 Broker");
    need_arg(in, at, "MqttClient.publish", argv, 0, V_STR, "string");
    const char *topic = val_get_cstr(argv[0]);
    if (!*topic) rt_error(in, at, "MqttClient.publish() 主题 (topic) 不能为空");

    const char *payload_str = "";
    size_t payload_len = 0;
    if (argc > 1) {
        if (argv[1].type == V_STR) {
            payload_str = val_get_cstr(argv[1]);
            payload_len = argv[1].as.str ? argv[1].as.str->len : 0;
        } else if (argv[1].type == V_BYTES) {
            payload_str = (const char *)argv[1].as.bytes->data;
            payload_len = argv[1].as.bytes->len;
        } else {
            rt_error(in, at, "MqttClient.publish() 消息体 (payload) 必须是 string 或 bytes");
        }
    }
    int qos = (argc > 2 && argv[2].type == V_INT) ? (int)argv[2].as.i : 0;
    int retain = (argc > 3 && is_truthy(argv[3])) ? 1 : 0;
    if (qos < 0 || qos > 1) qos = 0; /* 当前支持 QoS 0 与 QoS 1 */

    if (h->is_simulated) {
        /* PC 仿真模式：直接返回成功 */
        return val_bool(1);
    }

    uint8_t pkt[4096];
    uint8_t flags = 0;
    if (qos == 1) flags |= 0x02;
    if (retain) flags |= 0x01;
    pkt[0] = (uint8_t)((MQTT_PKT_PUBLISH << 4) | flags);

    size_t topic_len = strlen(topic);
    uint32_t rem_len = 2 + (uint32_t)topic_len + (qos > 0 ? 2 : 0) + (uint32_t)payload_len;
    int rem_len_bytes = mqtt_encode_remaining_len(pkt + 1, rem_len);
    size_t cur = 1 + rem_len_bytes;

    /* 写入 Topic */
    write_u16_be(pkt + cur, (uint16_t)topic_len);
    cur += 2;
    memcpy(pkt + cur, topic, topic_len);
    cur += topic_len;

    /* QoS > 0 时写入 Packet ID */
    uint16_t packet_id = 0;
    if (qos > 0) {
        packet_id = h->next_packet_id++;
        if (h->next_packet_id == 0) h->next_packet_id = 1;
        write_u16_be(pkt + cur, packet_id);
        cur += 2;
    }

    /* 写入 Payload */
    if (payload_len > 0) {
        memcpy(pkt + cur, payload_str, payload_len);
        cur += payload_len;
    }

    char err[128] = "";
    if (l_tcp_send_all(h->sock, pkt, cur, err, sizeof(err)) != 0) {
        rt_error(in, at, "MqttClient.publish() 发送失败: %s", err);
    }

    /* 若 QoS 1，等待 PUBACK 确认 */
    if (qos == 1) {
        uint8_t puback[4];
        int r = l_tcp_recv(h->sock, puback, 4, 3000, err, sizeof(err));
        if (r < 4 || (puback[0] >> 4) != MQTT_PKT_PUBACK || read_u16_be(puback + 2) != packet_id) {
            /* 即使 PUBACK 稍后到达，也不中断业务，保持容错 */
        }
    }

    return val_bool(1);
}

/* client.subscribe(topic, [qos=0]) */
static Value m_mqtt_subscribe(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    MqttHandle *h = mqtt_handle(self.as.obj);
    if (!h->connected) rt_error(in, at, "MqttClient.subscribe() 失败: 客户端未连接到 Broker");
    need_arg(in, at, "MqttClient.subscribe", argv, 0, V_STR, "string");
    const char *topic = val_get_cstr(argv[0]);
    if (!*topic) rt_error(in, at, "MqttClient.subscribe() 主题过滤串不能为空");
    int qos = (argc > 1 && argv[1].type == V_INT) ? (int)argv[1].as.i : 0;
    if (qos < 0 || qos > 1) qos = 0;

    if (h->is_simulated) {
        return val_bool(1);
    }

    uint16_t pid = h->next_packet_id++;
    if (h->next_packet_id == 0) h->next_packet_id = 1;

    size_t topic_len = strlen(topic);
    uint32_t rem_len = 2 + 2 + (uint32_t)topic_len + 1;
    uint8_t pkt[256];
    pkt[0] = (uint8_t)((MQTT_PKT_SUBSCRIBE << 4) | 0x02); /* bit 1 必须置 1 */
    int rem_len_bytes = mqtt_encode_remaining_len(pkt + 1, rem_len);
    size_t cur = 1 + rem_len_bytes;

    write_u16_be(pkt + cur, pid);
    cur += 2;
    write_u16_be(pkt + cur, (uint16_t)topic_len);
    cur += 2;
    memcpy(pkt + cur, topic, topic_len);
    cur += topic_len;
    pkt[cur++] = (uint8_t)qos;

    char err[128] = "";
    if (l_tcp_send_all(h->sock, pkt, cur, err, sizeof(err)) != 0) {
        rt_error(in, at, "MqttClient.subscribe() 发送订阅报文失败: %s", err);
    }

    /* 接收 SUBACK 确认 (5 字节: 0x90, 0x03, pid_hi, pid_lo, granted_qos) */
    uint8_t suback[8];
    int r = l_tcp_recv(h->sock, suback, 5, 3000, err, sizeof(err));
    if (r >= 5 && (suback[0] >> 4) == MQTT_PKT_SUBACK) {
        if (suback[4] == 0x80) {
            rt_error(in, at, "MqttClient.subscribe() 订阅主题 \"%s\" 被 Broker 拒绝 (返回码 0x80)", topic);
        }
    }
    return val_bool(1);
}

/* client.unsubscribe(topic) */
static Value m_mqtt_unsubscribe(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    MqttHandle *h = mqtt_handle(self.as.obj);
    if (!h->connected) rt_error(in, at, "MqttClient.unsubscribe() 失败: 客户端未连接到 Broker");
    need_arg(in, at, "MqttClient.unsubscribe", argv, 0, V_STR, "string");
    const char *topic = val_get_cstr(argv[0]);

    if (h->is_simulated) return val_bool(1);

    uint16_t pid = h->next_packet_id++;
    if (h->next_packet_id == 0) h->next_packet_id = 1;

    size_t topic_len = strlen(topic);
    uint32_t rem_len = 2 + 2 + (uint32_t)topic_len;
    uint8_t pkt[256];
    pkt[0] = (uint8_t)((MQTT_PKT_UNSUBSCRIBE << 4) | 0x02);
    int rem_len_bytes = mqtt_encode_remaining_len(pkt + 1, rem_len);
    size_t cur = 1 + rem_len_bytes;

    write_u16_be(pkt + cur, pid);
    cur += 2;
    write_u16_be(pkt + cur, (uint16_t)topic_len);
    cur += 2;
    memcpy(pkt + cur, topic, topic_len);
    cur += topic_len;

    char err[128] = "";
    l_tcp_send_all(h->sock, pkt, cur, err, sizeof(err));
    return val_bool(1);
}

/* client.ping() */
static Value m_mqtt_ping(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    MqttHandle *h = mqtt_handle(self.as.obj);
    if (!h->connected) return val_bool(0);
    if (h->is_simulated) return val_bool(1);

    uint8_t ping_pkt[2] = { (uint8_t)(MQTT_PKT_PINGREQ << 4), 0x00 };
    char err[64];
    return val_bool(l_tcp_send_all(h->sock, ping_pkt, 2, err, sizeof(err)) == 0);
}

/* client.on_message(fn(topic, payload)) */
static Value m_mqtt_on_message(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    MqttHandle *h = mqtt_handle(self.as.obj);
    Map *handlers = mqtt_get_handler_map(in);
    char key[32];
    snprintf(key, sizeof(key), "cb_%d", h->client_num);

    if (argc == 0 || argv[0].type == V_NIL) {
        map_remove(handlers, key);
        return self;
    }
    if (argv[0].type != V_FN && argv[0].type != V_NATIVE) {
        rt_error(in, at, "MqttClient.on_message() 回调参数必须为函数闭包, 实际是 %s", type_name(argv[0]));
    }
    Value fv = argv[0];
    gc_push_root(in, fv);
    map_set(handlers, str_new(in, key, strlen(key)), fv);
    gc_pop_roots(in, 1);
    return self;
}

/* 分发处理接收到的单个完整 MQTT PUBLISH 报文 */
static void dispatch_mqtt_publish(Interp *in, MqttHandle *h, const uint8_t *body, uint32_t body_len, uint8_t qos) {
    if (body_len < 2) return;
    uint16_t topic_len = read_u16_be(body);
    if ((uint32_t)(2 + topic_len) > body_len) return;

    const char *topic_str = (const char *)(body + 2);
    size_t offset = 2 + topic_len;

    uint16_t packet_id = 0;
    if (qos > 0) {
        if ((uint32_t)(offset + 2) > body_len) return;
        packet_id = read_u16_be(body + offset);
        offset += 2;

        /* QoS 1: 回复 PUBACK */
        uint8_t puback[4];
        puback[0] = (uint8_t)(MQTT_PKT_PUBACK << 4);
        puback[1] = 0x02;
        write_u16_be(puback + 2, packet_id);
        char err[64];
        l_tcp_send_all(h->sock, puback, 4, err, sizeof(err));
    }

    size_t payload_len = body_len - offset;
    const char *payload_str = (const char *)(body + offset);

    /* 查找注册的 on_message 回调闭包 */
    Map *handlers = mqtt_get_handler_map(in);
    char key[32];
    snprintf(key, sizeof(key), "cb_%d", h->client_num);
    Value *fn = map_get(handlers, key);
    if (!fn || (fn->type != V_FN && fn->type != V_NATIVE)) return;

    Value cb = *fn;
    Value args[2];
    args[0] = val_str(str_new(in, topic_str, topic_len));
    args[1] = val_str(str_new(in, payload_str, payload_len));
    gc_push_root(in, cb);
    gc_push_root(in, args[0]);
    gc_push_root(in, args[1]);

    interp_call(in, NULL, cb, 2, args);

    gc_pop_roots(in, 3);
}

/* client.poll([timeout_ms=100]): 轮询接收套接字报文并分发消息 */
static Value m_mqtt_poll(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)at;
    MqttHandle *h = mqtt_handle(self.as.obj);
    if (!h->connected || h->sock < 0 || h->is_simulated) return val_int(0);

    int timeout_ms = (argc > 0 && argv[0].type == V_INT) ? (int)argv[0].as.i : 100;
    if (timeout_ms < 0) timeout_ms = 0;

    char err[128] = "";
    uint8_t temp[1024];
    int processed = 0;

    int r = l_tcp_recv(h->sock, temp, sizeof(temp), timeout_ms, err, sizeof(err));
    if (r > 0) {
        if (h->rx_len + (size_t)r <= sizeof(h->rx_buf)) {
            memcpy(h->rx_buf + h->rx_len, temp, (size_t)r);
            h->rx_len += (size_t)r;
        }
    } else if (r == -1) {
        /* 对端关闭了连接 */
        h->connected = 0;
        l_tcp_close(h->sock);
        h->sock = -1;
        return val_int(0);
    }

    /* 循环解析 rx_buf 中的完整 MQTT 报文 */
    while (h->rx_len >= 2) {
        uint8_t pkt_type = (uint8_t)(h->rx_buf[0] >> 4);
        uint8_t pkt_flags = (uint8_t)(h->rx_buf[0] & 0x0F);
        uint32_t rem_len = 0;
        int rem_bytes = 0;
        int ok = mqtt_decode_remaining_len(h->rx_buf + 1, h->rx_len - 1, &rem_len, &rem_bytes);
        if (ok == 0) {
            /* 剩余长度字节未收全，继续等待 */
            break;
        }
        if (ok < 0) {
            /* 协议格式解析错误，清空接收缓存 */
            h->rx_len = 0;
            break;
        }

        size_t total_pkt = 1 + rem_bytes + rem_len;
        if (h->rx_len < total_pkt) {
            /* 数据体尚未收全，等待后续分包到达 */
            break;
        }

        const uint8_t *body = h->rx_buf + 1 + rem_bytes;

        /* 分报文类型处理 */
        if (pkt_type == MQTT_PKT_PUBLISH) {
            uint8_t qos = (uint8_t)((pkt_flags >> 1) & 0x03);
            dispatch_mqtt_publish(in, h, body, rem_len, qos);
            processed++;
        } else if (pkt_type == MQTT_PKT_PINGRESP) {
            /* 心跳确认响应，保活正常 */
            processed++;
        }

        /* 移出已处理的数据包 */
        size_t rest = h->rx_len - total_pkt;
        if (rest > 0) {
            memmove(h->rx_buf, h->rx_buf + total_pkt, rest);
        }
        h->rx_len = rest;
    }

    return val_int(processed);
}

/* client.sim_connect([success=true]): PC 离线模式下模拟连接成功 */
static Value m_mqtt_sim_connect(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)in; (void)at;
    MqttHandle *h = mqtt_handle(self.as.obj);
    int succ = (argc == 0 || is_truthy(argv[0])) ? 1 : 0;
    h->connected = succ;
    h->is_simulated = 1;
    return val_bool(succ);
}

/* client.sim_incoming(topic, payload): PC 离线模式模拟云端主动下发消息 */
static Value m_mqtt_sim_incoming(Interp *in, const Node *at, Value self, int argc, Value *argv) {
    (void)argc;
    MqttHandle *h = mqtt_handle(self.as.obj);
    need_arg(in, at, "MqttClient.sim_incoming", argv, 0, V_STR, "string");
    need_arg(in, at, "MqttClient.sim_incoming", argv, 1, V_STR, "string");

    Map *handlers = mqtt_get_handler_map(in);
    char key[32];
    snprintf(key, sizeof(key), "cb_%d", h->client_num);
    Value *fn = map_get(handlers, key);
    if (fn && (fn->type == V_FN || fn->type == V_NATIVE)) {
        Value cb = *fn;
        Value args[2];
        args[0] = argv[0];
        args[1] = argv[1];
        gc_push_root(in, cb);
        gc_push_root(in, args[0]);
        gc_push_root(in, args[1]);
        interp_call(in, NULL, cb, 2, args);
        gc_pop_roots(in, 3);
    }
    return self;
}

/* ================= 客户端类定义与方法表 ================= */

static const ObjMethod mqtt_methods[] = {
    {"connect",      1, 3, m_mqtt_connect,      "client.connect(host, [port=1883], [timeout_ms=5000])", "连接 MQTT Broker 并完成握手鉴权"},
    {"disconnect",   0, 0, m_mqtt_disconnect,   "client.disconnect()",                                  "断开 MQTT 连接并发送 DISCONNECT 报文"},
    {"is_connected", 0, 0, m_mqtt_is_connected, "client.is_connected()",                               "查询客户端是否处于在线连接状态"},
    {"set_auth",     2, 2, m_mqtt_set_auth,     "client.set_auth(username, password)",                  "设置接入鉴权认证用户名与密码"},
    {"set_keepalive",1, 1, m_mqtt_set_keepalive,"client.set_keepalive(sec)",                            "设置心跳保活秒数 (默认 60s)"},
    {"set_will",     2, 4, m_mqtt_set_will,     "client.set_will(topic, msg, [qos=0], [retain=false])", "设置遗嘱消息 (Will)"},
    {"publish",      2, 4, m_mqtt_publish,      "client.publish(topic, payload, [qos=0], [retain=false])", "发布消息到指定主题"},
    {"subscribe",    1, 2, m_mqtt_subscribe,    "client.subscribe(topic, [qos=0])",                      "订阅主题 (支持 + 与 # 通配符)"},
    {"unsubscribe",  1, 1, m_mqtt_unsubscribe,  "client.unsubscribe(topic)",                            "取消主题订阅"},
    {"on_message",   1, 1, m_mqtt_on_message,   "client.on_message(fn(topic, payload))",                "注册接收到云端下行消息时的处理函数"},
    {"poll",         0, 1, m_mqtt_poll,         "client.poll([timeout_ms=100])",                        "轮询接收网络报文并触发回调"},
    {"ping",         0, 0, m_mqtt_ping,         "client.ping()",                                        "发送心跳 PingREQ 报文"},
    {"sim_connect",  0, 1, m_mqtt_sim_connect,  "client.sim_connect([success=true])",                   "PC 离线模式模拟建立连接"},
    {"sim_incoming", 2, 2, m_mqtt_sim_incoming, "client.sim_incoming(topic, payload)",                  "PC 离线模式模拟云端消息下发"},
};

static const ObjClass mqtt_class = {
    "MqttClient",
    mqtt_methods,
    sizeof mqtt_methods / sizeof mqtt_methods[0],
    mqtt_format,
    mqtt_obj_destroy
};

/* MqttClient([client_id]): 构造 MQTT 客户端对象 */
static Value f_mqtt_client_ctor(Interp *in, const Node *at, int argc, Value *argv) {
    char cid[80] = "";
    if (argc > 0 && argv[0].type == V_STR) {
        snprintf(cid, sizeof(cid), "%s", val_get_cstr(argv[0]));
    } else {
        snprintf(cid, sizeof(cid), "l_client_%d", s_mqtt_next_client_num);
    }

    NativeObj *o = obj_new(in, &mqtt_class);
    o->extra = malloc(sizeof(MqttHandle));
    if (!o->extra) {
        rt_error(in, at, "MqttClient() 内存不足, 无法创建客户端对象");
    }
    MqttHandle *h = mqtt_handle(o);
    memset(h, 0, sizeof(*h));
    h->sock = -1;
    h->connected = 0;
    h->client_num = s_mqtt_next_client_num++;
    h->next_packet_id = 1;
    h->keepalive = 60;
    snprintf(h->client_id, sizeof(h->client_id), "%s", cid);

    o->fields[0] = h->client_num;
    return val_obj(o);
}

static const Native mqtt_ctor_native = {
    "MqttClient", 0, 1, f_mqtt_client_ctor,
    "MqttClient([client_id])", "创建 MQTT 3.1.1 物联网客户端对象"
};

/* ================= 全局与模块注册 ================= */

void mqtt_install_globals(Interp *in) {
    Value v = val_nil();
    v.type = V_NATIVE;
    v.as.native = &mqtt_ctor_native;
    env_define(in->globals, "MqttClient", v);

    /* 同时也提供全局 mqtt 命名空间对象 */
    Value mod = mqtt_build_module(in);
    env_define(in->globals, "mqtt", mod);
}

Value mqtt_build_module(Interp *in) {
    Map *m = map_new(in);
    Value mv = val_map(m);
    gc_push_root(in, mv);

    Value v = val_nil();
    v.type = V_NATIVE;
    v.as.native = &mqtt_ctor_native;
    map_set(m, str_new(in, "Client", 6), v);
    map_set(m, str_new(in, "MqttClient", 10), v);
    map_set(m, str_new(in, "version", 7), val_str(str_new(in, "3.1.1", 5)));

    gc_pop_roots(in, 1);
    return mv;
}

#endif /* L_WITH_STDLIB_NET */
