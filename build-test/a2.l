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
print("ok2");
