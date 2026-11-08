// 标准库:字符串与数组
print(split("a,b,,c", ","), split("你好", ""), split("abc", "x"));
print(join(["a", 1, true, nil], "-"), join([1, 2, 3]), "[" + join([], ",") + "]");
print(substr("hello world", 6), substr("hello world", 0, 5), substr("hello", -3), "[" + substr("hi", 5) + "]", substr("hello", 1, 100));
print(upper("abC"), lower("ABc"), "[" + trim("  hi \n") + "]");
print(contains("hello", "ell"), contains([1, 2, 3], 2), contains({ a: 1 }, "a"), contains("x", "y"));
print(find("hello", "l"), find("hello", "z"), find([5, 6, 7], 7));
print(replace("a-b-c", "-", "+"), starts_with("hello", "he"), ends_with("hello", "lo"), repeat("ab", 3));
print(chr(65), chr(20320), ord("A"), ord("你"), ord(""));
print(range(5), range(2, 5), range(10, 0, -3));
print(slice([1, 2, 3, 4, 5], 1, 3), slice([1, 2, 3], -2), reverse([1, 2, 3]), sort([3, 1, 2]), sort(["b", "a"]));

// 常见用法组合
let words = split("the quick brown fox", " ");
print(len(words), upper(words[1]), join(reverse(words), " "));
let total = 0;
for let i = 0; i < len(range(1, 101)); i = i + 1 { total = total + range(1, 101)[i]; }
print(total);
