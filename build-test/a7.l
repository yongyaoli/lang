fn counter() {
    let n = 0;
    return fn() { n = n + 1; return n; };
}
let c1 = counter();
let c2 = counter();
print("same?", c1 == c2);
print("c2 first:", c2());
print("c1 next:", c1());

fn pair() {
    let m = 0;
    let inc = fn() { m = m + 1; return m; };
    let get = fn() { return m; };
    return [inc, get];
}
let p = pair();
print(p[0](), p[0](), p[1]());
