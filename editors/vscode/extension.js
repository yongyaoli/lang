const vscode = require('vscode');
const cp = require('child_process');

// L 语言内置常用对象与函数文档库
const BUILTIN_DOCS = {
  // ESP32 芯片与硬件看板
  'ESP32': {
    signature: 'class ESP32',
    doc: 'ESP32 全局硬件控制与状态看板单例。可查询芯片规格、引脚能力表、裸 Flash 状态及网络信息。\n\n常用方法:\n- `ESP32.status()`: 打印系统硬件与网络看板\n- `ESP32.info()`: 返回硬件信息字典\n- `ESP32.pinout()`: 打印全引脚能力表\n- `ESP32.pins()`: 返回可用/占用引脚字典\n- `ESP32.pin_info(pin)`: 查询指定引脚能力\n- `ESP32.free_heap()`: 查询当前可用堆内存(字节)\n- `ESP32.chip()`: 当前芯片型号 (如 "ESP32", "ESP32-S3", "ESP32-C3")\n- `ESP32.flash_write(code)`: 将脚本写入 l_script 裸分区\n- `ESP32.flash_exec()`: 零拷贝直接执行 Flash 分区中的脚本'
  },
  'status': {
    signature: 'static function ESP32::status(): void',
    doc: '在控制台打印格式化的 ESP32 完整硬件规格、CPU主频/核心、堆内存、WiFi 连接名称(SSID)、局域网 IP、物理 MAC 以及裸 Flash 分区状态看板。'
  },
  'info': {
    signature: 'static function ESP32::info(): map',
    doc: '返回包含芯片所有硬件信息的字典，包括: `chip`, `cores`, `freq_mhz`, `sram_kb`, `free_heap`, `mac`, `wifi_connected`, `wifi_ssid`, `ip`, `is_hardware` 等。'
  },
  'pinout': {
    signature: 'static function ESP32::pinout(): void',
    doc: '在控制台以表格形式格式化输出当前芯片所有引脚的有效性、输入/输出能力、上拉下拉、PWM/ADC 通道及硬件占用说明。'
  },
  'pin_info': {
    signature: 'static function ESP32::pin_info(pin: int): map',
    doc: '查询指定 GPIO 引脚的详细能力字典，包含 `valid`, `usable`, `in`, `out`, `pwm`, `adc`, `strapping`, `flash` 等属性。'
  },
  // GPIO 引脚类
  'Pin': {
    signature: 'class Pin(pin: int, [mode: string = "out"])',
    doc: 'GPIO 硬件引脚控制类。\n\n参数:\n- `pin`: GPIO 引脚号 (如 2, 4, 15)\n- `mode`: 模式字符串, 可选 `"out"`(输出), `"in"`(浮空输入), `"in_pullup"`(内部上拉输入), `"in_pulldown"`(内部下拉输入)\n\n示例:\n```l\nlet led = Pin(2, "out");\nled.on();\n```'
  },
  'pin': {
    signature: 'function pin(pin: int): Pin',
    doc: '快捷创建或获取指定 GPIO 引脚对象 (同 `Pin(pin)`)。\n\n示例:\n```l\npin(2).on();\n```'
  },
  'on': {
    signature: 'function Pin::on(): Pin',
    doc: '将引脚输出电平置为高电平 (1, 3.3V)。支持链式调用。'
  },
  'off': {
    signature: 'function Pin::off(): Pin',
    doc: '将引脚输出电平置为低电平 (0, 0V)。支持链式调用。'
  },
  'toggle': {
    signature: 'function Pin::toggle(): int',
    doc: '翻转引脚电平 (高变低, 低变高), 并返回翻转后的电平值。'
  },
  'value': {
    signature: 'function Pin::value([val: int]): int',
    doc: '若传入参数则写入电平值 (0 或 1)；若无参数则读取并返回当前引脚电平值。'
  },
  // WiFi 网络管理
  'wifi_connect': {
    signature: 'function wifi_connect(ssid: string, [password: string], [timeout_ms: int = 15000]): bool',
    doc: '连接指定的 2.4GHz WiFi 热点。\n\n参数:\n- `ssid`: WiFi 名称\n- `password`: WiFi 密码 (开放网络可留空)\n- `timeout_ms`: 超时时间(毫秒, 默认 15000)\n\n返回值:\n连接成功并获取到 IP 返回 `true`, 失败返回 `false`。'
  },
  'wifi_disconnect': {
    signature: 'function wifi_disconnect(): bool',
    doc: '断开当前的 WiFi 连接并释放网络接口。'
  },
  'wifi_is_connected': {
    signature: 'function wifi_is_connected(): bool',
    doc: '查询当前设备是否已成功连接到 WiFi 并获取到有效 IPv4 地址。'
  },
  'wifi_ssid': {
    signature: 'function wifi_ssid(): string | nil',
    doc: '获取当前已连接的 WiFi 热点名称 (SSID)。未连网时返回 `nil`。'
  },
  'wifi_ip': {
    signature: 'function wifi_ip(): string | nil',
    doc: '获取当前设备在局域网中的 IPv4 地址 (如 `"192.168.1.100"`)。未连网时返回 `nil`。'
  },
  'wifi_mac': {
    signature: 'function wifi_mac(): string',
    doc: '获取 WiFi 物理网卡的 MAC 地址字符串 (如 `"24:0A:C4:B8:31:AA"`)。'
  },
  'wifi_scan': {
    signature: 'function wifi_scan(): array[map]',
    doc: '扫描周围可用的 WiFi 热点。返回热点字典数组，每个字典包含 `ssid`, `rssi` (信号强度), `auth` (加密类型)。'
  },
  'wifi_ap': {
    signature: 'function wifi_ap(ssid: string, [password: string]): bool',
    doc: '开启 SoftAP 热点模式，使 ESP32 自身成为一个无线路由器供手机或电脑连接。'
  },
  // HTTP 网络通信
  'http_get': {
    signature: 'function http_get(url: string, [timeout_ms: int = 10000]): map',
    doc: '发起 HTTP 或 HTTPS GET 请求。\n\n返回值字典:\n- `status`: HTTP 状态码 (如 200, 404)\n- `body`: 响应正文字符串\n- `ok`: 请求是否成功 (`status == 200`)\n- `error`: 错误信息 (失败时存在)'
  },
  'http_post': {
    signature: 'function http_post(url: string, [body: string], [headers: string], [timeout_ms: int = 10000]): map',
    doc: '发起 HTTP 或 HTTPS POST 请求。\n\n参数:\n- `url`: 请求地址\n- `body`: 请求体正文数据\n- `headers`: 自定义请求头 (可选)\n\n返回值字典包含 `status`, `body`, `ok`, `error`。'
  },
  // SNTP 网络对时
  'sntp_sync': {
    signature: 'function sntp_sync([ntp_server: string = "pool.ntp.org"], [timeout_ms: int = 10000]): bool',
    doc: '通过网络与 NTP 服务器同步现实世界的时间，成功返回 `true`。同步后可通过 `time()` 或 `date()` 获取精确时间。'
  },
  // 外设类
  'PWM': {
    signature: 'class PWM(pin: int, [freq: int = 1000], [duty: int = 0])',
    doc: '脉宽调制 (PWM / LEDC) 控制器。可用于电机调速、LED 呼吸灯及蜂鸣器音调控制。\n\n方法: `.duty(0..1023)`, `.freq(hz)`, `.duty_percent(0..100)`, `.stop()`'
  },
  'ADC': {
    signature: 'class ADC(pin: int, [atten: string = "11db"])',
    doc: '模拟数字转换器 (ADC)。用于采集模拟传感器电压。\n\n方法:\n- `.read()`: 读取原始 12 位 ADC 值 (0..4095)\n- `.read_mv()`: 读取校准后的电压毫伏值 (mV)\n- `.atten(db)`: 设置衰减度'
  },
  'I2C': {
    signature: 'class I2C([bus: int = 0], [sda: int], [scl: int], [freq: int = 400000])',
    doc: 'I2C 总线硬件控制器。用于连接 OLED 屏、温湿度计、陀螺仪等传感器。\n\n方法: `.scan()`, `.write(addr, data)`, `.read(addr, len)`, `.write_reg(addr, reg, data)`, `.read_reg(addr, reg, len)`'
  },
  'SPI': {
    signature: 'class SPI([bus: int = 1], [sck: int], [mosi: int], [miso: int], [freq: int])',
    doc: 'SPI 高速总线硬件控制器。用于驱动 TFT 彩屏、SD 卡读写器等。\n\n方法: `.transfer(data)`, `.write(data)`, `.read(len)`'
  },
  'UART': {
    signature: 'class UART([port: int = 1], [baud: int = 115200], [tx: int], [rx: int])',
    doc: '通用异步收发器 (串口)。用于与外部串口模块 (如 GPS, 蓝牙, 4G 模组) 通信。\n\n方法: `.write(data)`, `.read([len])`, `.read_str()`, `.any()`'
  },
  // 核心内置函数
  'print': {
    signature: 'function print(...values): void',
    doc: '将参数格式化并输出到终端控制台，末尾自动追加换行。'
  },
  'echo': {
    signature: 'function echo(...values): void',
    doc: '输出文本到终端或响应流中。'
  },
  'help': {
    signature: 'function help([topic]): void',
    doc: '查看系统或模块帮助文档。支持直接传入模块名 (如 `help(sys)`, `help(esp32)`, `help(net)`)。'
  },
  'len': {
    signature: 'function len(obj: string | array | map | bytes): int',
    doc: '获取字符串长度、数组元素数、字典键值对数或字节缓冲容量。'
  },
  'type': {
    signature: 'function type(obj): string',
    doc: '返回对象的类型名称字符串 (如 `"int"`, `"float"`, `"string"`, `"array"`, `"map"`, `"Pin"`, `"bytes"`)。'
  },
  'ports': {
    signature: 'function ports(): array[string]',
    doc: '扫描并列出本机当前所有可用的串口端口列表 (如 `["COM3", "COM4"]`)。'
  },
  'upload': {
    signature: 'function upload(port: string, script_path: string): bool',
    doc: '将指定的脚本文件通过串口直接烧录写入 ESP32 的 `l_script` 裸 Flash 分区并热重载执行。'
  },
  'connect': {
    signature: 'function connect(port: string, [baud: int = 115200]): void',
    doc: '通过串口连接到 ESP32 板载交互终端 (REPL)。'
  },
  // JSON 编解码器
  'JSON': {
    signature: 'singleton class JSON',
    doc: 'RFC 8259 流式 JSON 编解码器全局对象。纯 C99 零外部依赖单趟解析，具备深度防御与精确定位。\n\n方法:\n- `JSON.stringify(val, [pretty=false])`: 序列化原生 Map/Array/值 为 JSON 字符串 (pretty 为 true 时缩进 2 空格)\n- `JSON.parse(str)`: 将 JSON 字符串反序列化为原生 Map/Array 结构，支持行列精确定位报错'
  },
  'json': {
    signature: 'singleton class json (alias for JSON)',
    doc: '全局 JSON 对象的小写别名。参见 `JSON`。'
  },
  'json_parse': {
    signature: 'function json_parse(str: string): any',
    doc: '将 JSON 格式字符串解析为 L 原生数据类型 (Map/Array/值)。'
  },
  'json_stringify': {
    signature: 'function json_stringify(val: any, [pretty: bool = false]): string',
    doc: '将 L 原生数据类型序列化为 JSON 字符串。'
  },
  // TCP 客户端与轻量服务端
  'Tcp': {
    signature: 'class Tcp(host: string, port: int, [timeout_ms: int = 5000])',
    doc: 'TCP 客户端套接字连接对象。\n\n方法:\n- `.send(data)`: 发送字符串或 bytes\n- `.recv([max_bytes=1024], [timeout_ms=2000])`: 接收文本\n- `.recv_bytes([max_bytes], [timeout_ms])`: 接收二进制 bytes\n- `.is_connected()`: 查询连接有效性\n- `.close()`: 关闭套接字\n- `.info()`: 返回 `{host, port, connected}`'
  },
  'TcpServer': {
    signature: 'class TcpServer(port: int, [host: string = "0.0.0.0"], [backlog: int = 5])',
    doc: '轻量级 TCP / Web 服务端对象。用于在 ESP32 或 PC 上搭建局域网 Web 控制后台与配网接口。\n\n方法:\n- `.accept([timeout_ms=1000])`: 等待并接入新客户端, 超时返回 `nil` (轮询友好, 不卡死心跳), 成功返回 `Tcp` 客户端对象\n- `.is_listening()`: 查询是否处于监听中\n- `.close()`: 主动关闭服务端\n- `.info()`: 返回 `{host, port, listening}`'
  },
  // 低功耗蓝牙 BLE
  'BLE': {
    signature: 'singleton class BLE',
    doc: '全局低功耗蓝牙 (BLE GATT Server) 控制对象。支持蓝牙广播、手机 App 连接与无线数据透传。\n\n方法:\n- `BLE.init([device_name="L-Lang-BLE"])`: 初始化协议栈并设置广播名\n- `BLE.adv_start([adv_name])`: 开启蓝牙广播\n- `BLE.adv_stop()`: 停止蓝牙广播\n- `BLE.is_connected()`: 查询是否有客户端连接\n- `BLE.send(data)`: 向客户端发送文本或 bytes\n- `BLE.recv([max_len=512], [timeout_ms=0])`: 接收客户端数据\n- `BLE.recv_bytes([max_len], [timeout_ms])`: 二进制安全接收\n- `BLE.status()`: 返回 `{initialized, advertising, connected, ...}`\n- `BLE.deinit()`: 释放蓝牙资源'
  },
  // 开源传感器与外设驱动
  'SSD1306': {
    signature: 'class SSD1306(i2c_bus, [addr=0x3C], [width=128], [height=64])',
    doc: '0.96寸 I2C OLED 屏幕驱动 (来自 examples/drivers/SSD1306.l)。\n\n方法:\n- `.clear()`: 清空显存缓冲\n- `.pixel(x, y, [color=1])`: 绘制单点\n- `.text(s, x, y)`: 打印 5x7 ASCII 字符文本\n- `.show()`: 将显存缓冲一次性刷新到 OLED 屏幕'
  },
  'ILI9341': {
    signature: 'class ILI9341(spi, cs_pin, dc_pin, [rst_pin], [bl_pin])',
    doc: '2.4/2.8/3.2寸 SPI 彩色 TFT LCD 显示屏驱动 (来自 examples/drivers/ILI9341.l)。分辨率 240x320，RGB565 65K 色。\n\n方法:\n- `.clear([color])` / `.fill(color)`: 全屏纯色填充\n- `.pixel(x, y, color)`: 绘制单个彩色像素点\n- `.line(x0, y0, x1, y1, color)`: Bresenham 算法画线\n- `.rect(x, y, w, h, color)` / `.fill_rect(...)`: 绘制空心/实心矩形\n- `.circle(x, y, r, color)` / `.fill_circle(...)`: 绘制空心/实心圆形\n- `.text(s, x, y, [color], [bg], [scale=1])`: 打印带缩放的 ASCII 文本\n- `.set_rotation(0~3)`: 切换 0度/90度/180度/270度 旋转模式\n- `.backlight(true/false)`: 控制背光使能'
  },
  'AHT20': {
    signature: 'class AHT20(i2c_bus, [addr=0x38])',
    doc: '高精度 I2C 温湿度传感器驱动 (来自 examples/drivers/AHT20.l)。\n\n方法:\n- `.read()`: 触发采样并返回 `{temp, humi}` (温度℃与湿度%RH)'
  },
  'WS2812': {
    signature: 'class WS2812(pin, count)',
    doc: 'RGB 全彩幻彩灯珠驱动 (来自 examples/drivers/WS2812.l)。\n\n方法:\n- `.set_rgb(i, r, g, b)`: 设置单个灯珠颜色\n- `.wheel(pos)`: 彩虹色轮算法\n- `.show()`: 发送脉冲刷新灯珠'
  }
};

/**
 * 提取指定行前面的文档注释 (如 /** ... * / 或 // ...)
 */
function extractDocComment(document, lineIndex) {
  let comments = [];
  let idx = lineIndex - 1;
  while (idx >= 0) {
    const text = document.lineAt(idx).text.trim();
    if (text === '') {
      idx--;
      continue;
    }
    if (text.startsWith('//')) {
      comments.unshift(text.replace(/^\/\/\s*/, ''));
      idx--;
    } else if (text.endsWith('*/')) {
      // 块注释
      while (idx >= 0) {
        const blkText = document.lineAt(idx).text.trim();
        const clean = blkText.replace(/^\/\*+\s*|\*+\/|^\s*\*\s?/g, '');
        if (clean) comments.unshift(clean);
        if (blkText.startsWith('/*')) break;
        idx--;
      }
      break;
    } else {
      break;
    }
  }
  return comments.join('\n').trim();
}

/**
 * 全面解析文档中的类、方法、函数和变量符号
 */
function parseDocumentSymbols(document) {
  const lineCount = document.lineCount;
  const classes = [];
  const functions = [];
  const variables = [];

  let currentClass = null;

  // 正则匹配
  const classRegex = /^\s*class\s+([a-zA-Z_][a-zA-Z0-9_]*)/;
  // 方法/函数匹配: 支持修饰符 (public/protected/private/static) 以及 fn / function
  const funcRegex = /^\s*(?:(public|protected|private|static)\s+)*(?:(public|protected|private|static)\s+)?(fn|function)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)/;
  // 变量匹配
  const varRegex = /^\s*(?:var|let|const|\$)\s*([a-zA-Z_][a-zA-Z0-9_]*)/;

  for (let i = 0; i < lineCount; i++) {
    const text = document.lineAt(i).text;

    // 检查类定义
    const classMatch = text.match(classRegex);
    if (classMatch) {
      currentClass = {
        name: classMatch[1],
        line: i,
        col: text.indexOf(classMatch[1]),
        doc: extractDocComment(document, i),
        uri: document.uri
      };
      classes.push(currentClass);
      continue;
    }

    // 检查方法或独立函数定义
    const funcMatch = text.match(funcRegex);
    if (funcMatch) {
      // 提取修饰符
      const rawPrefix = text.substring(0, text.indexOf(funcMatch[3])).trim();
      const fnName = funcMatch[4];
      const params = funcMatch[5].trim();
      const col = text.indexOf(fnName);

      const fnSymbol = {
        name: fnName,
        className: currentClass ? currentClass.name : null,
        modifiers: rawPrefix, // 如 "public static" 或 "static"
        keyword: funcMatch[3], // "fn" 或 "function"
        params: params,
        line: i,
        col: col,
        rawLine: text.trim(),
        doc: extractDocComment(document, i),
        uri: document.uri
      };
      functions.push(fnSymbol);
      continue;
    }

    // 类闭合检测 (简化按大括号缩进或遇到下一个 class)
    if (currentClass && /^\s*\}\s*$/.test(text)) {
      currentClass = null;
    }
  }

  return { classes, functions, variables };
}

/**
 * 查找光标前面的类名或作用域上下文 (例如 Hook::exec 中的 Hook, 或 obj.method 中的 obj)
 */
function getPrecedingScope(document, position) {
  const line = document.lineAt(position.line).text;
  const beforePos = line.substring(0, position.character);
  const matchScope = beforePos.match(/([a-zA-Z_][a-zA-Z0-9_]*)(::|\.)$/);
  if (matchScope) {
    return matchScope[1];
  }
  return null;
}

/**
 * 统计工作区中某个单词的引用次数
 */
async function countReferences(document, word) {
  let count = 0;
  const wordRegex = new RegExp(`\\b${word}\\b`, 'g');

  // 当前文档
  const text = document.getText();
  const matches = text.match(wordRegex);
  if (matches) {
    count += matches.length;
  }

  // 跨文件检索 (最多搜索 10 个 .l 文件，避免大项目卡顿)
  if (vscode.workspace.workspaceFolders) {
    const files = await vscode.workspace.findFiles('**/*.l', '**/node_modules/**', 10);
    for (const file of files) {
      if (file.toString() === document.uri.toString()) continue;
      try {
        const otherDoc = await vscode.workspace.openTextDocument(file);
        const otherText = otherDoc.getText();
        const otherMatches = otherText.match(wordRegex);
        if (otherMatches) {
          count += otherMatches.length;
        }
      } catch (e) {}
    }
  }
  // 减去自身的声明 1 次，得到被引用的次数 (至少显示 1 次引用体验)
  return Math.max(1, count - 1);
}

function activate(context) {
  // 1. 注册 Hover 悬停提示 (完美对齐 PHP 体验)
  const hoverProvider = vscode.languages.registerHoverProvider('l', {
    async provideHover(document, position, token) {
      const wordRange = document.getWordRangeAtPosition(position, /[\$a-zA-Z_][a-zA-Z0-9_]*/);
      if (!wordRange) return null;
      let rawWord = document.getText(wordRange);
      let word = rawWord.startsWith('$') ? rawWord.substring(1) : rawWord;

      // 提取前缀作用域 (例如 Hook::exec -> scope: 'Hook')
      const scope = getPrecedingScope(document, wordRange.start);

      // (1) 检查是否为内置库或内置函数 (优先检查 scope.word)
      if (scope && BUILTIN_DOCS[word]) {
        const item = BUILTIN_DOCS[word];
        const md = new vscode.MarkdownString();
        md.appendCodeblock(item.signature, 'l');
        if (item.doc) md.appendMarkdown(`\n---\n${item.doc}`);
        return new vscode.Hover(md, wordRange);
      }
      if (!scope && BUILTIN_DOCS[word]) {
        const item = BUILTIN_DOCS[word];
        const md = new vscode.MarkdownString();
        md.appendCodeblock(item.signature, 'l');
        if (item.doc) md.appendMarkdown(`\n---\n${item.doc}`);
        return new vscode.Hover(md, wordRange);
      }

      // (2) 解析当前文档中的符号
      let { classes, functions } = parseDocumentSymbols(document);

      // (3) 如果带有作用域 (如 Hook::exec)，优先在该类的方法中查找
      if (scope) {
        let method = functions.find(f => f.name === word && f.className === scope);
        if (!method) {
          // 在其他文件查找该类的该方法
          if (vscode.workspace.workspaceFolders) {
            const files = await vscode.workspace.findFiles('**/*.l', '**/node_modules/**', 10);
            for (const file of files) {
              if (file.toString() === document.uri.toString()) continue;
              try {
                const otherDoc = await vscode.workspace.openTextDocument(file);
                const otherParsed = parseDocumentSymbols(otherDoc);
                method = otherParsed.functions.find(f => f.name === word && f.className === scope);
                if (method) break;
              } catch (e) {}
            }
          }
        }

        if (method) {
          const md = new vscode.MarkdownString();
          const mod = method.modifiers ? `${method.modifiers} ` : 'static ';
          const kw = method.keyword || 'function';
          // 生成类似: static function Hook::exec($fun, $class = ""): void
          md.appendCodeblock(`${mod}${kw} ${scope}::${method.name}(${method.params}): void`, 'l');
          if (method.doc) {
            md.appendMarkdown(`\n---\n${method.doc}`);
          } else {
            md.appendMarkdown(`\n---\n*${method.name}*`);
          }
          return new vscode.Hover(md, wordRange);
        }
      }

      // (4) 查找是否为类名 (如 class Core / class Hook)
      let cls = classes.find(c => c.name === word);
      if (!cls && vscode.workspace.workspaceFolders) {
        const files = await vscode.workspace.findFiles('**/*.l', '**/node_modules/**', 10);
        for (const file of files) {
          if (file.toString() === document.uri.toString()) continue;
          try {
            const otherDoc = await vscode.workspace.openTextDocument(file);
            const otherParsed = parseDocumentSymbols(otherDoc);
            cls = otherParsed.classes.find(c => c.name === word);
            if (cls) break;
          } catch (e) {}
        }
      }
      if (cls) {
        const md = new vscode.MarkdownString();
        md.appendCodeblock(`class ${cls.name}`, 'l');
        if (cls.doc) {
          md.appendMarkdown(`\n---\n${cls.doc}`);
        }
        return new vscode.Hover(md, wordRange);
      }

      // (5) 查找普通方法/函数
      let fn = functions.find(f => f.name === word);
      if (!fn && vscode.workspace.workspaceFolders) {
        const files = await vscode.workspace.findFiles('**/*.l', '**/node_modules/**', 10);
        for (const file of files) {
          if (file.toString() === document.uri.toString()) continue;
          try {
            const otherDoc = await vscode.workspace.openTextDocument(file);
            const otherParsed = parseDocumentSymbols(otherDoc);
            fn = otherParsed.functions.find(f => f.name === word);
            if (fn) break;
          } catch (e) {}
        }
      }
      if (fn) {
        const md = new vscode.MarkdownString();
        const mod = fn.modifiers ? `${fn.modifiers} ` : '';
        const kw = fn.keyword || 'function';
        const owner = fn.className ? `${fn.className}::` : '';
        md.appendCodeblock(`${mod}${kw} ${owner}${fn.name}(${fn.params}): void`, 'l');
        if (fn.doc) {
          md.appendMarkdown(`\n---\n${fn.doc}`);
        } else {
          md.appendMarkdown(`\n---\n*${fn.name}*`);
        }
        return new vscode.Hover(md, wordRange);
      }

      return null;
    }
  });

  // 2. 注册 DefinitionProvider (Ctrl+Click 精准跳转定义)
  const definitionProvider = vscode.languages.registerDefinitionProvider('l', {
    async provideDefinition(document, position, token) {
      const wordRange = document.getWordRangeAtPosition(position, /[\$a-zA-Z_][a-zA-Z0-9_]*/);
      if (!wordRange) return null;
      let rawWord = document.getText(wordRange);
      let word = rawWord.startsWith('$') ? rawWord.substring(1) : rawWord;
      const scope = getPrecedingScope(document, wordRange.start);

      // (1) 解析当前文档符号
      let { classes, functions } = parseDocumentSymbols(document);

      // (2) 如果有作用域 (如 Hook::exec)，优先查找类方法
      if (scope) {
        let method = functions.find(f => f.name === word && f.className === scope);
        if (method) {
          return new vscode.Location(method.uri, new vscode.Position(method.line, method.col));
        }
        // 跨文件检索
        if (vscode.workspace.workspaceFolders) {
          const files = await vscode.workspace.findFiles('**/*.l', '**/node_modules/**', 10);
          for (const file of files) {
            if (file.toString() === document.uri.toString()) continue;
            try {
              const otherDoc = await vscode.workspace.openTextDocument(file);
              const otherParsed = parseDocumentSymbols(otherDoc);
              method = otherParsed.functions.find(f => f.name === word && f.className === scope);
              if (method) {
                return new vscode.Location(method.uri, new vscode.Position(method.line, method.col));
              }
            } catch (e) {}
          }
        }
      }

      // (3) 查找类定义
      let cls = classes.find(c => c.name === word);
      if (cls) {
        return new vscode.Location(cls.uri, new vscode.Position(cls.line, cls.col));
      }

      // (4) 查找方法或普通函数
      let fn = functions.find(f => f.name === word);
      if (fn) {
        return new vscode.Location(fn.uri, new vscode.Position(fn.line, fn.col));
      }

      // (5) 跨文件全局查找类或函数
      if (vscode.workspace.workspaceFolders) {
        const files = await vscode.workspace.findFiles('**/*.l', '**/node_modules/**', 15);
        for (const file of files) {
          if (file.toString() === document.uri.toString()) continue;
          try {
            const otherDoc = await vscode.workspace.openTextDocument(file);
            const otherParsed = parseDocumentSymbols(otherDoc);
            let target = otherParsed.classes.find(c => c.name === word) || otherParsed.functions.find(f => f.name === word);
            if (target) {
              return new vscode.Location(target.uri, new vscode.Position(target.line, target.col));
            }
          } catch (e) {}
        }
      }

      return null;
    }
  });

  // 3. 注册 CodeLensProvider (呈现截图中强大的 "1 reference | 0 implementations" 体验)
  const codeLensProvider = vscode.languages.registerCodeLensProvider('l', {
    async provideCodeLenses(document, token) {
      const codeLenses = [];
      const { classes, functions } = parseDocumentSymbols(document);

      // 为每个类增加引用计数镜头
      for (const cls of classes) {
        const count = await countReferences(document, cls.name);
        const refText = count === 1 ? '1 reference' : `${count} references`;
        const range = new vscode.Range(cls.line, 0, cls.line, 0);

        codeLenses.push(new vscode.CodeLens(range, {
          title: `${refText} | 0 implementations`,
          command: 'editor.action.findReferences',
          arguments: [document.uri, new vscode.Position(cls.line, cls.col)]
        }));
      }

      // 为每个方法或函数增加引用计数镜头
      for (const fn of functions) {
        const count = await countReferences(document, fn.name);
        const refText = count === 1 ? '1 reference' : `${count} references`;
        const range = new vscode.Range(fn.line, 0, fn.line, 0);

        codeLenses.push(new vscode.CodeLens(range, {
          title: `${refText} | 0 overrides`,
          command: 'editor.action.findReferences',
          arguments: [document.uri, new vscode.Position(fn.line, fn.col)]
        }));
      }

      return codeLenses;
    }
  });

  // 4. 注册 DocumentSymbolProvider (面包屑和函数大纲视图)
  const symbolProvider = vscode.languages.registerDocumentSymbolProvider('l', {
    provideDocumentSymbols(document, token) {
      const symbols = [];
      const { classes, functions } = parseDocumentSymbols(document);

      for (const c of classes) {
        const line = document.lineAt(c.line);
        symbols.push(new vscode.DocumentSymbol(
          c.name,
          'class',
          vscode.SymbolKind.Class,
          line.range,
          line.range
        ));
      }

      for (const f of functions) {
        const line = document.lineAt(f.line);
        const detail = f.className ? `${f.className}::(${f.params})` : `(${f.params})`;
        symbols.push(new vscode.DocumentSymbol(
          f.name,
          detail,
          vscode.SymbolKind.Function,
          line.range,
          line.range
        ));
      }

      return symbols;
    }
  });

  context.subscriptions.push(hoverProvider, definitionProvider, codeLensProvider, symbolProvider);

  // ---------------- 5. 串口检测、终端控制与右键菜单指令 ----------------

  // 状态栏: 串口选择器
  portStatusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
  portStatusBarItem.command = 'l.selectSerialPort';
  updatePortStatusBar(getSelectedPort());
  portStatusBarItem.show();

  // 状态栏: 串口监视器快捷启动
  monitorStatusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 99);
  monitorStatusBarItem.command = 'l.openSerialMonitor';
  monitorStatusBarItem.text = '$(terminal) 串口监视';
  monitorStatusBarItem.tooltip = '打开 ESP32 串口交互终端监视器';
  monitorStatusBarItem.show();

  // 自动检测可用串口 (若只有一个串口且未配置，则自动智能选中)
  listSerialPorts().then(ports => {
    if (ports && ports.length > 0) {
      const cur = getSelectedPort();
      if (!cur || !ports.includes(cur)) {
        if (ports.length === 1) {
          setSelectedPort(ports[0]);
          vscode.window.setStatusBarMessage(`[L 语言] 自动检测并选定串口: ${ports[0]}`, 4000);
        }
      }
    }
  }).catch(() => {});

  // 注册命令
  const cmdRun = vscode.commands.registerCommand('l.runScript', runScriptCommand);
  const cmdRunVM = vscode.commands.registerCommand('l.runScriptVM', runScriptVMCommand);
  const cmdUpload = vscode.commands.registerCommand('l.uploadToBoard', uploadToBoardCommand);
  const cmdSelectPort = vscode.commands.registerCommand('l.selectSerialPort', selectSerialPortCommand);
  const cmdMonitor = vscode.commands.registerCommand('l.openSerialMonitor', openSerialMonitorCommand);
  const cmdCompile = vscode.commands.registerCommand('l.compileBytecode', compileBytecodeCommand);
  const cmdRepl = vscode.commands.registerCommand('l.startRepl', startReplCommand);

  context.subscriptions.push(
    portStatusBarItem,
    monitorStatusBarItem,
    cmdRun,
    cmdRunVM,
    cmdUpload,
    cmdSelectPort,
    cmdMonitor,
    cmdCompile,
    cmdRepl
  );
}

// 终端与状态栏全局句柄
let lTerminal = null;
let serialTerminal = null;
let portStatusBarItem = null;
let monitorStatusBarItem = null;

// 获取用户配置的 L 解释器可执行文件命令
function getLCommand() {
  const cfg = vscode.workspace.getConfiguration('l');
  const exe = cfg.get('executablePath');
  return (exe && exe.trim()) ? exe.trim() : 'l';
}

// 获取当前配置或选中的串口
function getSelectedPort() {
  const cfg = vscode.workspace.getConfiguration('l');
  return cfg.get('serialPort') || '';
}

// 设置选中的串口并更新持久化配置与状态栏
async function setSelectedPort(port) {
  const cfg = vscode.workspace.getConfiguration('l');
  await cfg.update('serialPort', port, vscode.ConfigurationTarget.Global);
  updatePortStatusBar(port);
}

// 更新状态栏显示
function updatePortStatusBar(port) {
  if (!portStatusBarItem) return;
  if (port) {
    portStatusBarItem.text = `$(circuit-board) ${port}`;
    portStatusBarItem.tooltip = `已选 ESP32 串口: ${port} (点击切换)`;
  } else {
    portStatusBarItem.text = `$(plug) L: 选择串口`;
    portStatusBarItem.tooltip = `点击选择 ESP32 目标串口`;
  }
}

// 扫描系统可用串口列表 (优先调用 l -p, 备选 PowerShell)
function listSerialPorts() {
  return new Promise((resolve) => {
    const lExe = getLCommand();
    cp.execFile(lExe, ['-p'], { timeout: 3000 }, (err, stdout) => {
      if (!err && stdout) {
        const matches = stdout.match(/(COM\d+|\/dev\/tty[A-Za-z0-9_-]+|\/dev\/cu\.[A-Za-z0-9_-]+)/g);
        if (matches && matches.length > 0) {
          const unique = Array.from(new Set(matches));
          return resolve(unique);
        }
      }

      // Windows 备选方案: PowerShell 查询串口
      if (process.platform === 'win32') {
        cp.exec('powershell -NoProfile -Command "[System.IO.Ports.SerialPort]::GetPortNames()"', { timeout: 3000 }, (err2, stdout2) => {
          if (!err2 && stdout2) {
            const lines = stdout2.split(/\r?\n/).map(s => s.trim()).filter(s => /^COM\d+$/i.test(s));
            if (lines.length > 0) {
              return resolve(Array.from(new Set(lines)));
            }
          }
          resolve([]);
        });
      } else {
        resolve([]);
      }
    });
  });
}

// 获取待执行的目标文件路径 (支持从右键菜单 uri 或当前激活编辑器提取，自动保存未存文件)
async function getTargetFilePath(uri) {
  if (uri && uri.fsPath) {
    return uri.fsPath;
  }
  const editor = vscode.window.activeTextEditor;
  if (editor && editor.document) {
    if (editor.document.isDirty) {
      await editor.document.save();
    }
    return editor.document.uri.fsPath;
  }
  return null;
}

// 获取或创建普通运行终端
function getLTerminal() {
  if (!lTerminal || lTerminal.exitStatus !== undefined) {
    lTerminal = vscode.window.createTerminal({ name: 'L Terminal' });
  }
  return lTerminal;
}

// 格式化终端执行命令 (兼容 Windows PowerShell、cmd、Git Bash、macOS 与 Linux)
function formatShellCommand(exe, args = []) {
  let cmd = (exe && exe.trim()) ? exe.trim() : 'l';
  // 如果可执行文件路径中包含空格，在 PowerShell 下必须加 & 运算符以正确调用，避免被当成字符串字面量
  if (/\s/.test(cmd)) {
    cmd = `& "${cmd}"`;
  }
  const formattedArgs = args.map(arg => {
    if (arg === undefined || arg === null) return '';
    const str = String(arg);
    // 包含空格或路径分隔符的参数添加双引号保护
    if (str.includes(' ') || str.includes('\\') || str.includes('/')) {
      return `"${str}"`;
    }
    return str;
  }).filter(s => s.length > 0);

  return [cmd, ...formattedArgs].join(' ');
}

// 1. 运行当前脚本 (AST 解释器模式)
async function runScriptCommand(uri) {
  const filePath = await getTargetFilePath(uri);
  if (!filePath) {
    vscode.window.showErrorMessage('请先打开或选择一个要运行的 .l 脚本文件');
    return;
  }
  const term = getLTerminal();
  term.show();
  const exe = getLCommand();
  term.sendText(formatShellCommand(exe, [filePath]));
}

// 2. 运行当前脚本 (VM 虚拟机模式)
async function runScriptVMCommand(uri) {
  const filePath = await getTargetFilePath(uri);
  if (!filePath) {
    vscode.window.showErrorMessage('请先打开或选择一个要运行的 .l 脚本文件');
    return;
  }
  const term = getLTerminal();
  term.show();
  const exe = getLCommand();
  term.sendText(formatShellCommand(exe, ['--vm', filePath]));
}

// 3. 编译为二进制字节码 (.lc)
async function compileBytecodeCommand(uri) {
  const filePath = await getTargetFilePath(uri);
  if (!filePath) {
    vscode.window.showErrorMessage('请先打开或选择一个要编译的 .l 脚本文件');
    return;
  }
  const exe = getLCommand();
  cp.execFile(exe, ['-c', filePath], (err, stdout, stderr) => {
    if (err) {
      vscode.window.showErrorMessage(`字节码编译失败: ${stderr || stdout || err.message}`);
    } else {
      const lcPath = filePath.replace(/\.l$/i, '.lc');
      vscode.window.showInformationMessage(`编译成功，已生成二进制字节码: ${lcPath}`);
    }
  });
}

// 4. 选择串口交互 QuickPick
async function selectSerialPortCommand() {
  const ports = await listSerialPorts();
  const curPort = getSelectedPort();

  const items = ports.map(p => ({
    label: `$(circuit-board) ${p}`,
    description: p === curPort ? '当前已选' : '',
    port: p
  }));

  items.push({ label: '$(refresh) 重新扫描可用串口...', port: '__refresh__' });
  items.push({ label: '$(edit) 手动输入串口名称...', port: '__manual__' });

  const picked = await vscode.window.showQuickPick(items, {
    placeHolder: ports.length > 0 ? '选择用于烧录或监控的目标串口' : '未检测到可用串口，可手动输入或重新插拔后扫描'
  });

  if (!picked) return curPort;

  if (picked.port === '__refresh__') {
    return selectSerialPortCommand();
  }
  if (picked.port === '__manual__') {
    const input = await vscode.window.showInputBox({
      prompt: '请输入串口名称 (如 COM3 或 /dev/ttyUSB0)',
      value: curPort
    });
    if (input && input.trim()) {
      await setSelectedPort(input.trim());
      vscode.window.showInformationMessage(`已设置目标串口: ${input.trim()}`);
      return input.trim();
    }
    return curPort;
  }

  await setSelectedPort(picked.port);
  vscode.window.showInformationMessage(`已切换目标串口: ${picked.port}`);
  return picked.port;
}

// 5. 烧录脚本到 ESP32 开发板
async function uploadToBoardCommand(uri) {
  const filePath = await getTargetFilePath(uri);
  if (!filePath) {
    vscode.window.showErrorMessage('请先打开或选择一个要烧录的 .l 脚本文件');
    return;
  }
  let port = getSelectedPort();
  if (!port) {
    port = await selectSerialPortCommand();
    if (!port) return;
  }

  const term = getLTerminal();
  term.show();
  const exe = getLCommand();
  term.sendText(formatShellCommand(exe, ['-u', port, filePath]));
}

// 6. 打开串口监视器 / 交互终端
async function openSerialMonitorCommand() {
  let port = getSelectedPort();
  if (!port) {
    port = await selectSerialPortCommand();
    if (!port) return;
  }
  const cfg = vscode.workspace.getConfiguration('l');
  const baud = cfg.get('baudRate') || 115200;
  const exe = getLCommand();

  if (serialTerminal && serialTerminal.exitStatus === undefined) {
    serialTerminal.dispose();
  }
  serialTerminal = vscode.window.createTerminal({
    name: `L 串口监视器 (${port})`
  });
  serialTerminal.show();
  serialTerminal.sendText(formatShellCommand(exe, ['-s', port, baud]));
}

// 7. 打开本地交互式 REPL 终端
function startReplCommand() {
  const term = getLTerminal();
  term.show();
  const exe = getLCommand();
  term.sendText(formatShellCommand(exe, []));
}

function deactivate() {}

module.exports = {
  activate,
  deactivate
};
