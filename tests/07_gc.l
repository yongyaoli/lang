// 测试 GC: 产生大量临时对象并验证内存回收与闭包存活
let i = 0;
while i < 10000 {
    let temp = "hello_" + str(i);
    let arr = [temp, i];
    let m = { key: temp };
    i = i + 1;
}

// 循环后验证仍可正常分配和计算
print("循环生成 10000 次临时对象完成, i =", i);

// 验证闭包中的引用未被错误回收
fn make_holder(val) {
    return fn() { return val; };
}

let holders = [];
for let j = 0; j < 100; j = j + 1 {
    push(holders, make_holder("item_" + str(j)));
}

// 再次进行大量分配触发多次 GC
for let k = 0; k < 5000; k = k + 1 {
    let dummy = [k, "temp"];
}

// 检查闭包数据完好
print(holders[0](), holders[50](), holders[99]());
