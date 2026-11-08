// 测试模块 import
import "./helpers";
import "./helpers" as h;

// 1. 默认模块名访问
print("author:", helpers.author);
print("helpers add:", helpers.add(10, 20));
print("helpers mul:", helpers.mul(6, 7));

// 2. 别名访问与缓存单例验证 (相同模块路径不会重复执行)
print("alias add:", h.add(100, 200));
print("same module:", helpers == h);

// 3. 动态修改导出字典属性
h.extra = 999;
print("extra prop:", helpers.extra);
