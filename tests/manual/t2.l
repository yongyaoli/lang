let i = 0;
while i < 10000 {
    let temp = "hello_" + str(i);
    let arr = [temp, i];
    let m = { key: temp };
    i = i + 1;
}
print("ok", i);
