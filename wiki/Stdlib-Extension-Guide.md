# 标准库扩展开发指南

如果您希望为 L 语言贡献或增加全新的 C 语言原生内置函数、外设驱动或模块，请遵循本开发指南。

---

## 1. 编写原生 C 函数

所有内置函数的签名必须符合统一的原生函数规范：
```c
static Value f_my_func(Interp *in, const Node *at, int argc, Value *argv) {
    // 1. 参数合法性检查
    need_arg(in, at, "my_func", argv, 0, V_STR, "string");
    
    // 2. 提取参数
    const char *text = val_get_cstr(argv[0]);
    
    // 3. 执行您的核心业务逻辑
    printf("[Custom C] 收到文本: %s\n", text);
    
    // 4. 返回值封装 (val_nil(), val_int(), val_str(), val_bool() 等)
    return val_bool(1);
}
```

---

## 2. 注册到函数分发表

在相应的模块表（如 `src/core/builtins.c` 或新建的 `stdlib/*.c`）中添加注册项：
```c
static const Native custom_natives[] = {
    // 函数名, 最少参数, 最多参数 (-1表示不限), 函数指针, 用法说明, 详细帮助
    {"my_func", 1, 1, f_my_func, "my_func(text)", "我的自定义原生函数"},
};
```

---

## 3. 验证与单元测试

1. 在 `tests/` 目录下新增一个测试脚本 `tests/27_custom.l`；
2. 运行测试套件：
   ```powershell
   gcc -O2 -Isrc/include src/core/*.c src/stdlib/*.c -lws2_32 -lwininet -o l.exe
   ./l.exe tests/27_custom.l
   ```
3. 确保现有 26 项回归测试 100% PASS！
