// 阶段 M6 综合测试: 栈式虚拟机 VM、字节码编译器与预编译执行
print("=== 1. 基础算术与变量 ===");
let a = 15;
let b = 4;
print("a + b =", a + b);
print("a - b =", a - b);
print("a * b =", a * b);
print("a / b =", a / b);
print("a % b =", a % b);

print("=== 2. 位运算 ===");
print("12 & 5 =", 12 & 5);
print("12 | 5 =", 12 | 5);
print("12 ^ 5 =", 12 ^ 5);
print("1 << 4 =", 1 << 4);
print("32 >> 2 =", 32 >> 2);

print("=== 3. 函数调用与斐波那契递归 ===");
fn fib(n) {
    if n < 2 { return n; }
    return fib(n - 1) + fib(n - 2);
}
print("fib(10) =", fib(10));

print("=== 4. 循环与 break / continue ===");
let count = 0;
let sum = 0;
while count < 10 {
    count = count + 1;
    if count == 3 { continue; }
    if count > 6 { break; }
    sum = sum + count;
}
print("sum =", sum);

print("=== 5. 闭包与 Upvalue 捕获 ===");
fn make_counter(start) {
    let c = start;
    fn inner() {
        c = c + 1;
        return c;
    }
    return inner;
}
let counter = make_counter(100);
print("counter() =", counter());
print("counter() =", counter());

print("=== 6. 容器对象与方法调用 ===");
let list = [10, 20, 30];
list.push(40);
print("list len:", len(list), "index 1:", list[1]);

let map_data = {"key": "value", "id": 42};
print("map id:", map_data["id"]);

print("=== 7. ESP32 状态看板 (VM 模式派发) ===");
let info = ESP32.info();
print("VM 查询芯片型号:", info["chip"], "核心数:", info["cores"]);

print("=== 8. try / catch 异常捕获 ===");
try {
    throw "VM 保护块异常拦截成功";
} catch (err) {
    print("捕获到异常:", err);
}

print("=== M6 VM 测试全部通过 ===");
