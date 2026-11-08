// 测试三元表达式: cond ? then_expr : else_expr
let a = 10;
let b = 20;
let max_val = a > b ? a : b;
let min_val = a < b ? a : b;
print("max:", max_val, "min:", min_val);

// 嵌套与右结合测试: 1 ? 2 : 3 ? 4 : 5 应为 2
let nested1 = false ? 1 : true ? 2 : 3;
let nested2 = false ? 1 : false ? 2 : 3;
print("nested:", nested1, nested2);

// 短路求值: 不应触发未定义或报错分支
let flag = true;
let res = flag ? "ok" : 1 / 0;
print("short_circuit:", res);

// 表达式组合
fn sign(x) {
    return x > 0 ? "positive" : x < 0 ? "negative" : "zero";
}
print(sign(5), sign(-3), sign(0));
