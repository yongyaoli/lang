let i = 0;
while i < 10000 {
    let temp = "hello_" + str(i);
    let arr = [temp, i];
    let m = { key: temp };
    i = i + 1;
}
fn make_holder(val) {
    return fn() { return val; };
}
let holders = [];
for let j = 0; j < 100; j = j + 1 {
    push(holders, make_holder("item_" + str(j)));
}
print(holders[0](), holders[50](), holders[99]());
