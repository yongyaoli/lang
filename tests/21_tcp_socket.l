// ============================================
// 测试 21: TCP Socket 客户端 (Tcp 类)
// ============================================
// 回环回显服务端由 run_tests.ps1 自动启动 (127.0.0.1:23075)。

fn assert(cond, msg) {
    if not cond {
        print("断言失败:", msg);
        exit(1);
    }
}

// TCP 是字节流: 两次 send 可能分片到达, 循环接收直到收满期望长度
fn recv_until(t, want_len) {
    let acc = "";
    while acc.len() < want_len {
        let part = t.recv(1024, 1000);
        if part == nil { break; }
        acc = acc + part;
    }
    return acc;
}

print("--- 测试 1. 连接被拒绝: 明确错误信息 ---");
let caught = false;
try {
    // 回环地址上几乎必然无服务的高位端口
    Tcp("127.0.0.1", 1, 1000);
} catch (e) {
    caught = true;
    print("捕获连接失败诊断:", e.message);
    assert(e.message != "", "错误信息应有明确原因");
    assert(e.line > 0, "错误应精确定位到行号");
}
assert(caught == true, "连接拒绝应抛出明确错误");

print("--- 测试 2. 参数校验 ---");
let caught2 = false;
try {
    Tcp("127.0.0.1", 99999);
} catch (e) {
    caught2 = true;
    print("捕获端口参数诊断:", e.message);
}
assert(caught2 == true, "非法端口应抛出明确错误");

let caught3 = false;
try {
    Tcp("", 80);
} catch (e) {
    caught3 = true;
    print("捕获空主机诊断:", e.message);
}
assert(caught3 == true, "空主机名应抛出明确错误");

print("--- 测试 3. 环回回显: 连接 / 发送 / 接收 ---");
let t = Tcp("127.0.0.1", 23075, 3000);
assert(t.is_connected() == true, "环回连接应成功");
assert(type(t) == "Tcp", "类型名为 Tcp");

t.send("hello tcp");
let echo = recv_until(t, 9);
assert(echo == "hello tcp", "回显内容应一致, 实际: " + echo);
print("回显验证通过:", echo);

// 链式发送 + 字节流分片接收
t.send("abc").send("def");
let echo2 = recv_until(t, 6);
assert(echo2 == "abcdef", "链式发送回显应一致, 实际: " + echo2);
print("链式发送验证通过:", echo2);

t.send(bytes("bin-bytes"));
let acc = "";
while acc.len() < 9 {
    let part = t.recv_bytes(1024, 1000);
    if part == nil { break; }
    acc = acc + part.to_str();
}
assert(acc == "bin-bytes", "bytes 回显内容应一致, 实际: " + acc);
print("bytes 收发验证通过:", acc);

print("--- 测试 4. info 与关闭 ---");
let info = t.info();
assert(info.host == "127.0.0.1", "info.host 应正确");
assert(info.port == 23075, "info.port 应正确");
assert(info.connected == true, "info.connected 应为 true");

t.close();
assert(t.is_connected() == false, "close 后应显示未连接");

let caught4 = false;
try {
    t.send("x");
} catch (e) {
    caught4 = true;
    print("捕获已关闭连接诊断:", e.message);
}
assert(caught4 == true, "关闭后发送应抛出明确错误");

print("--- 测试 5. 超时返回 nil (轮询友好) ---");
let t2 = Tcp("127.0.0.1", 23075, 3000);
let nothing = t2.recv(1024, 200);
assert(nothing == nil, "无数据超时应返回 nil 而非抛错");
t2.close();

print("TCP Socket 测试全部通过！");
