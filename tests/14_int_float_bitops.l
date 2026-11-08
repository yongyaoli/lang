// 整数 / 浮点 拆分与位运算
print(type(1), type(1.5), type(1 + 1.0), type(7 / 2), type(7 / 2.0));
print(7 / 2, -7 / 2, 7 % 3, -7 % 3, 7 / 2.0, 7.5 % 2);
print(1 == 1.0, 1 < 1.5, 2.0 == 2);

// 字面量
print(0xFF, 0b1010, 0xFFFFFFFF, 2147483647, 3000000000);
print(type(3000000000));

// 32 位回绕
print(2147483647 + 1, -2147483647 - 2, 65536 * 65536);

// 位运算
print(0xF0 & 0x3C, 0xF0 | 0x0F, 0xFF ^ 0x0F, ~0, ~5);
print(1 << 4, 1 << 31, 256 >> 4, -16 >> 2);
// 优先级: 位运算高于比较, x & 1 == 0 即 (x & 1) == 0
let x = 6;
print(x & 1 == 0, x & 1 == 1, 1 | 2 << 1);

// 位掩码实战: 置位 / 清位 / 翻转 / 测试
let reg = 0;
reg = reg | (1 << 3);
reg = reg | (1 << 0);
print(reg, (reg >> 3) & 1, (reg >> 1) & 1);
reg = reg & ~(1 << 3);
print(reg, reg ^ 0xFF);

// 标准库保持 int 的地方
print(abs(-5), abs(-5.5), floor(2.7), ceil(2.1), round(2.5), type(floor(2.7)));
print(min(3, 1, 2), max(1, 2.5), type(max(1, 2)));
print(num("42"), type(num("42")), num("4.5"), type(num("4.5")), num("abc"));
print(len("abc"), len([1, 2]), range(3), range(0, 1, 0.5));
print(sort([3, 1.5, 2]), 1 + 2 * 3 - 4 / 2);
print(sqrt(16), pow(2, 10), 10 / 4 * 4, 10 / 4.0 * 4);

// 错误: 位运算只接受整数
fn bad() { return 1.5 & 1; }
bad();
