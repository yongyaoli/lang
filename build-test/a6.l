fn counter() {
    let n = 0;
    return fn() { n = n + 1; return n; };
}
let c1 = counter();
let c2 = counter();
print(c1(), c1(), c1(), c2(), c2());
print("---");
let x = "global";
{
    let x = "inner";
    print(x);
}
print(x);
fn make_adder(k) { return fn(v) { return v + k; }; }
let add5 = make_adder(5);
let add7 = make_adder(7);
print(add5(10), add7(10));
let fns = [];
for let k = 0; k < 3; k = k + 1 {
    let captured = k * 10;
    push(fns, fn() { return captured; });
}
print(fns[0](), fns[1](), fns[2]());
fn apply(f, v) { return f(v); }
print(apply(fn(z) { return z * z; }, 7));
