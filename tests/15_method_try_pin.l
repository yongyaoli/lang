// 测试 M2: 方法调用语法、Pin 硬件对象、try/catch/throw 与精确错误定位

// 1. Pin / pin 对象与链式方法调用
let led = Pin(2, "out").on();
print("type:", type(led), "pin:", led.pin(), "mode:", led.mode(), "val:", led.value());
print("str:", led);
led.off();
print("after off:", led.value());
led.toggle();
print("after toggle:", led.value());
led.value(0).mode("in_pullup");
print("chained:", led.mode(), led.value(), led.info());

// 小写 pin(4).on() 直接调用(与底层 GPIO 状态表同步)
pin(4).on();
print("pin(4) level:", Pin(4, "in").value());
pin(4).off();
print("pin(4) off:", Pin(4, "in").value());

// 2. 字符串、数组、Map 内置方法链
print("str chain:", "  hello, esp32  ".trim().upper().split(", "));
print("arr chain:", [3, 1, 4, 2].sort().reverse().join("-"));
let xs = [1, 2];
xs.push(3);
print("xs map:", xs.map(fn(x) { return x * 10; }), "len:", xs.len(), "pop:", xs.pop());

let m = { host: "esp32", port: 80 };
print("map methods:", m.has("host"), m.len(), m.remove("port"), m.has("port"), m.keys());

// 3. 自定义对象方法 (隐式 self 与 显式 fn(self, ...))
let counter = {
    val: 0,
    inc: fn(step) {
        self.val = self.val + step;
        return self;
    },
    add_explicit: fn(self, a, b) {
        self.val = self.val + a + b;
        return self.val;
    }
};
print("counter:", counter.inc(3).inc(4).val, counter.add_explicit(10, 20));

// 4. try / catch / throw 与精确错误定位 (line / col / fn / source / stack)
fn sensor_read(addr) {
    if (addr < 0) {
        throw { code: 404, message: "无效传感器地址" };
    }
    return 100 / addr;
}

fn app_step(addr) {
    return sensor_read(addr);
}

try {
    app_step(0);
} catch (e) {
    print("caught div0:", e.type, "|", e.message);
    print("location:", io_basename(e.file), e.line, e.col, "in", e.fn);
    print("source:", e.source);
    print("stack depth:", e.stack.len());
}

try {
    app_step(-1);
} catch (e) {
    print("caught throw map:", e.type, e.code, e.message, "line:", e.line, "fn:", e.fn);
}

// 5. 捕获硬件参数错误
try {
    Pin(99, "out").on();
} catch (e) {
    print("caught pin err:", e.message, "line:", e.line, "col:", e.col);
}

// 6. 未捕获异常(验证 stderr 输出的文件名:行:列、源码行指示 ^ 与调用栈回溯)
app_step(0);

