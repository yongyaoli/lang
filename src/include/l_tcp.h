#ifndef L_TCP_H
#define L_TCP_H

#include <stddef.h>

/* 跨平台 TCP Socket 薄封装 (Windows WinSock2 / POSIX BSD / ESP-IDF lwip)。
 * 独立翻译单元实现, 避免引入 windows.h 与解释器词法 Token 枚举冲突。
 * 错误诊断约定: 失败时向 errbuf 写入 "中文原因描述 (错误码 N)"。 */

#ifdef _WIN32
typedef long long l_tcp_sock_t;   /* SOCKET */
#else
typedef int l_tcp_sock_t;
#endif

/* 带超时的阻塞式 TCP 连接 (内部使用非阻塞 connect + select)。
 * 成功返回套接字; 失败返回 -1 并在 errbuf 填充明确原因。 */
l_tcp_sock_t l_tcp_connect(const char *host, int port, int timeout_ms,
                           char *errbuf, size_t cap);

/* 发送全部字节。成功返回 0; 失败返回 -1 并填充原因。 */
int l_tcp_send_all(l_tcp_sock_t sock, const void *data, size_t len,
                   char *errbuf, size_t cap);

/* 接收: 返回收到的字节数; 0=超时; -1=对方关闭; -2=出错(原因在 errbuf)。 */
int l_tcp_recv(l_tcp_sock_t sock, void *buf, size_t cap, int timeout_ms,
               char *errbuf, size_t cap_err);

/* 关闭套接字 */
void l_tcp_close(l_tcp_sock_t sock);

#endif /* L_TCP_H */
