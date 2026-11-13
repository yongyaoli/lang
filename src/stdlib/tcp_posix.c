/* TCP Socket POSIX 实现 (Linux/macOS 宿主 + ESP-IDF lwip): 独立翻译单元 */
#include "l_tcp.h"

#ifndef _WIN32

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void tcp_set_err(char *errbuf, size_t cap, const char *why, int code) {
    if (errbuf && cap) snprintf(errbuf, cap, "%s (errno %d)", why, code);
}

l_tcp_sock_t l_tcp_connect(const char *host, int port, int timeout_ms,
                           char *errbuf, size_t cap) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(host, port_str, &hints, &res);
    if (gai != 0 || !res) {
        tcp_set_err(errbuf, cap, "DNS 解析失败: 无法解析主机名", gai);
        return -1;
    }

    int sock = -1;
    int last_err = 0;
    const char *last_why = "";
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0) { last_err = errno; last_why = "创建套接字失败"; continue; }

        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        int cr = connect(sock, ai->ai_addr, ai->ai_addrlen);
        if (cr != 0) {
            if (errno == EINPROGRESS) {
                fd_set wset, eset;
                FD_ZERO(&wset); FD_SET(sock, &wset);
                FD_ZERO(&eset); FD_SET(sock, &eset);
                struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
                int sr = select(sock + 1, NULL, &wset, &eset, &tv);
                if (sr == 0) { last_err = ETIMEDOUT; last_why = "连接超时"; close(sock); sock = -1; continue; }
                if (sr < 0)  { last_err = errno; last_why = "select 失败"; close(sock); sock = -1; continue; }
                int so_err = 0; socklen_t elen = sizeof(so_err);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_err, &elen);
                if (so_err != 0) { last_err = so_err; last_why = "连接被目标拒绝或不可达"; close(sock); sock = -1; continue; }
            } else {
                last_err = errno; last_why = "连接立即失败";
                close(sock); sock = -1; continue;
            }
        }
        int fl2 = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, fl2 & ~O_NONBLOCK);   /* 恢复阻塞, 收发用 select 控超时 */
        break;
    }
    freeaddrinfo(res);

    if (sock < 0) {
        tcp_set_err(errbuf, cap, last_why, last_err);
        return -1;
    }
    return (l_tcp_sock_t)sock;
}

int l_tcp_send_all(l_tcp_sock_t sock, const void *data, size_t len,
                   char *errbuf, size_t cap) {
    const char *p = (const char *)data;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send((int)sock, p + sent, len - sent, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            tcp_set_err(errbuf, cap, "发送中断: 连接可能已被对方关闭", errno);
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int l_tcp_recv(l_tcp_sock_t sock, void *buf, size_t cap, int timeout_ms,
               char *errbuf, size_t cap_err) {
    (void)cap_err;
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET((int)sock, &rset);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int sr = select((int)sock + 1, &rset, NULL, NULL, &tv);
    if (sr == 0) return 0;
    if (sr < 0)  { tcp_set_err(errbuf, cap, "select 失败", errno); return -2; }
    ssize_t n = recv((int)sock, buf, cap, 0);
    if (n == 0) return -1;
    if (n < 0)  {
        if (errno == EINTR) return 0;
        tcp_set_err(errbuf, cap, "接收失败", errno);
        return -2;
    }
    return (int)n;
}

void l_tcp_close(l_tcp_sock_t sock) {
    close((int)sock);
}

#endif /* !_WIN32 */
