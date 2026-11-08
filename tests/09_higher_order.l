// 测试高阶函数: map, filter, reduce
let nums = [1, 2, 3, 4, 5];

// 1. map
let doubled = map(nums, fn(x) { return x * 2; });
print("doubled:", doubled);

// 2. filter
let evens = filter(nums, fn(x) { return x % 2 == 0; });
print("evens:", evens);

// 3. reduce 带初始值与不带初始值
let sum = reduce(nums, fn(acc, x) { return acc + x; });
let sum_init = reduce(nums, fn(acc, x) { return acc + x; }, 100);
let product = reduce(nums, fn(acc, x) { return acc * x; }, 1);
print("sum:", sum, "sum_init:", sum_init, "product:", product);

// 4. 组合管道
let result = reduce(
    map(
        filter(range(1, 11), fn(x) { return x % 2 != 0; }),
        fn(x) { return x * x; }
    ),
    fn(acc, x) { return acc + x; },
    0
);
// 奇数 1, 3, 5, 7, 9 -> 平方 1, 9, 25, 49, 81 -> 和 165
print("pipeline:", result);
