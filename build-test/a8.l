fn counter() {
    let n = 0;
    return fn() { n = n + 1; return n; };
}
let c1 = counter();
let c2 = counter();
print("A", c1(), c1(), c1(), c2());
print("B", c1(), c2(), c1(), c2());
let d1 = counter();
let d2 = counter();
print("C", d1(), d2());
print("D", d1(), d1(), d2(), d2());
