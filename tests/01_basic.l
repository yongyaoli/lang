// 基础:算术、字符串、变量、控制流、函数
print(1 + 2 * 3, (1 + 2) * 3, 10 % 3, 7 / 2);
print("hello" + " " + "world", -5, not true, !nil);
print(1 < 2, 2 <= 1, "a" < "b", 1 == 1, "x" == "x", nil == false);
print(nil or "默认值", false and 1, 0 and "零也为真");

let i = 0;
while i < 5 {
    i = i + 1;
    if i == 2 { continue; }
    if i == 4 { break; }
    print("i =", i);
}

for let j = 0; j < 3; j = j + 1 {
    if j == 1 { continue; }
    print("j =", j);
}

fn add(a, b) { return a + b; }
print(add(1, 2));

fn fib(n) {
    if n < 2 { return n; }
    return fib(n - 1) + fib(n - 2);
}
print("fib(20) =", fib(20));

fn noreturn() { }
print(noreturn());

if false { print("no"); } else if true { print("else if"); } else { print("no"); }
