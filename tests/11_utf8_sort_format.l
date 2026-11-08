// 测试字符串字符级处理 (ulen, usubstr, uat, format) 与 sort 自定义比较函数
let s = "你好，世界！Hello";
print("byte len:", len(s), "char len:", ulen(s));
print("usubstr 0..2:", usubstr(s, 0, 2));
print("usubstr 3..2:", usubstr(s, 3, 2));
print("usubstr -5:", usubstr(s, -5));
print("uat chars:", uat(s, 0), uat(s, 1), uat(s, -1), uat(s, 100));

// format 模板格式化
let name = "张三";
let age = 18;
let score = 99.5;
print(format("学生 {} 年龄 {} 成绩 {}", name, age, score));
print(format("转义测试: {{}} 与普通文本 {}", "OK"));

// sort 自定义比较函数
let nums = [5, 2, 8, 1, 9];
// 降序排序: 返回 a > b ? -1 : 1
let desc = sort(nums, fn(a, b) { return b - a; });
print("desc nums:", desc);

// 按对象属性排序
let users = [
    { name: "Bob", age: 25 },
    { name: "Alice", age: 20 },
    { name: "Charlie", age: 30 }
];
let sorted_users = sort(users, fn(u1, u2) { return u1.age - u2.age; });
print("sorted users:", map(sorted_users, fn(u) { return u.name; }));
