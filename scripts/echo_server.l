// 测试回显专用 TCP 服务端 (用于 tests/21_tcp_socket.l)
let srv = TcpServer(23075);
while true {
    let peer = srv.accept(200);
    if peer != nil {
        while true {
            let data = peer.recv(4096, 200);
            if data == nil { break; }
            peer.send(data);
        }
        peer.close();
    }
}
