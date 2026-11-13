// 嵌套 try/catch 与字节码函数内错误
fn inner(x) {
    if x > 0 { throw "inner error"; }
    return len(5);
}
try {
    try {
        inner(1);
    } catch (e) {
        print("inner caught:", e.message, "line:", e.line);
        throw { code: 7, message: "rethrow" };
    }
} catch (e2) {
    print("outer caught:", e2.type, e2.code, e2.message, "line:", e2.line);
}

// 原生函数内部错误被 try 捕获
try {
    let xs = [1, 2];
    print(xs[9]);
} catch (e) {
    print("index error caught at line", e.line, ":", e.message);
}

// 嵌套循环 continue/break
let total = 0;
for let i = 0; i < 5; i = i + 1 {
    if i == 1 { continue; }
    for let j = 0; j < 5; j = j + 1 {
        if j == 2 { continue; }
        if j == 4 { break; }
        total = total + i * 10 + j;
    }
}
print("total:", total);

// try 中正常结束
try {
    print("no error here");
} catch (e) {
    print("unexpected");
}
print("done");
