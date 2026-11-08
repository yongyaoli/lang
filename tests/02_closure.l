// 闭包与作用域
fn counter() {
    let n = 0;
    return fn() { n = n + 1; return n; };
}
let c1 = counter();
let c2 = counter();
print(c1(), c1(), c1(), c2());

let x = "global";
{
    let x = "inner";
    print(x);
}
print(x);

fn make_adder(k) { return fn(v) { return v + k; }; }
let add5 = make_adder(5);
print(add5(10));

// 循环里创建闭包:共享同一个 for 作用域
let fns = [];
for let k = 0; k < 3; k = k + 1 {
    let captured = k * 10;
    push(fns, fn() { return captured; });
}
print(fns[0](), fns[1](), fns[2]());

// 高阶函数
fn apply(f, v) { return f(v); }
print(apply(fn(z) { return z * z; }, 7));
