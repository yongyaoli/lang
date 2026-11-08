// 数组、map、内置函数
let xs = [1, 2, 3];
push(xs, 4);
xs[0] = 100;
print(xs, len(xs), xs[3]);
print(pop(xs), xs);

let p = { name: "L", "ver": 0.1 };
p.author = "me";
p["year"] = 2026;
print(p.name, p.ver, p.author, p.year, p.missing);
print(p);
print(keys(p), len(p));

let nested = { list: [1, [2, 3]], m: { a: 1 } };
print(nested.list[1][0], nested.m.a);
nested.m.a = 99;
print(nested);

print(type(1), type("s"), type(true), type(nil), type(xs), type(p), type(print), type(fn() {}));
print(str(3.5) + "!", num("42") + 1, num("abc"), len("你好"));
print("abc"[1]);
print(xs == xs, [1] == [1]);
