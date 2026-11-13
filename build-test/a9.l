fn mk1() { let a = 1; let b = 2; return fn() { return a * 10 + b; }; }
print("mk1:", mk1()());

fn mk2(n) { return fn() { n = n + 1; return n; }; }
let p1 = mk2(0);
let p2 = mk2(0);
print("param:", p1(), p1(), p2());

fn mk3() { let n = 0; return fn() { return n; }; }
let r1 = mk3();
print("readonly:", r1());

fn mk4() { let n = 0; let f = fn() { n = n + 1; return n; }; return [f, n]; }
let q = mk4();
print("mk4:", q[0](), q[1]);
try { print("global n =", type(n)); } catch (err) { print("no global n"); }
