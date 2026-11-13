let i = 0;
while i < 10000 {
    let temp = "hello_" + str(i);
    i = i + 1;
}
print("循环生成 10000 次临时对象完成, i =", i);
