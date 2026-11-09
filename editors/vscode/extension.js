const vscode = require('vscode');

// L 语言内置常用对象与函数文档库
const BUILTIN_DOCS = {
  // ESP32 芯片与硬件看板
  'ESP32': {
    signature: 'ESP32 (全局单例硬件控制类)',
    doc: 'ESP32 全局硬件控制与状态看板单例。可查询芯片规格、引脚能力表、裸 Flash 状态及网络信息。\n\n常用方法:\n- `ESP32.status()`: 打印系统硬件与网络看板\n- `ESP32.info()`: 返回硬件信息字典\n- `ESP32.pinout()`: 打印全引脚能力表\n- `ESP32.pins()`: 返回可用/占用引脚字典\n- `ESP32.pin_info(pin)`: 查询指定引脚能力\n- `ESP32.free_heap()`: 查询当前可用堆内存(字节)\n- `ESP32.chip()`: 当前芯片型号 (如 "ESP32", "ESP32-S3", "ESP32-C3")\n- `ESP32.flash_write(code)`: 将脚本写入 l_script 裸分区\n- `ESP32.flash_exec()`: 零拷贝直接执行 Flash 分区中的脚本'
  },
  'status': {
    signature: 'ESP32.status(): nil',
    doc: '在控制台打印格式化的 ESP32 完整硬件规格、CPU主频/核心、堆内存、WiFi 连接名称(SSID)、局域网 IP、物理 MAC 以及裸 Flash 分区状态看板。'
  },
  'info': {
    signature: 'ESP32.info(): map',
    doc: '返回包含芯片所有硬件信息的字典，包括: `chip`, `cores`, `freq_mhz`, `sram_kb`, `free_heap`, `mac`, `wifi_connected`, `wifi_ssid`, `ip`, `is_hardware` 等。'
  },
  'pinout': {
    signature: 'ESP32.pinout(): nil',
    doc: '在控制台以表格形式格式化输出当前芯片所有引脚的有效性、输入/输出能力、上拉下拉、PWM/ADC 通道及硬件占用说明。'
  },
  'pin_info': {
    signature: 'ESP32.pin_info(pin: int): map',
    doc: '查询指定 GPIO 引脚的详细能力字典，包含 `valid`, `usable`, `in`, `out`, `pwm`, `adc`, `strapping`, `flash` 等属性。'
  },
  // GPIO 引脚类
  'Pin': {
    signature: 'Pin(pin: int, [mode: string]): Pin',
    doc: 'GPIO 硬件引脚控制类。\n\n参数:\n- `pin`: GPIO 引脚号 (如 2, 4, 15)\n- `mode`: 模式字符串, 可选 `"out"`(输出), `"in"`(浮空输入), `"in_pullup"`(带内部上拉输入), `"in_pulldown"`(带内部下拉输入)\n\n示例:\n```l\nvar led = Pin(2, "out");\nled.on();\n```'
  },
  'pin': {
    signature: 'pin(pin: int): Pin',
    doc: '快捷创建或获取指定 GPIO 引脚对象 (同 `Pin(pin)`)。\n\n示例:\n```l\npin(2).on();\n```'
  },
  'on': {
    signature: 'pin.on(): self',
    doc: '将引脚输出电平置为高电平 (1, 3.3V)。支持链式调用。'
  },
  'off': {
    signature: 'pin.off(): self',
    doc: '将引脚输出电平置为低电平 (0, 0V)。支持链式调用。'
  },
  'toggle': {
    signature: 'pin.toggle(): int',
    doc: '翻转引脚电平 (高变低, 低变高), 并返回翻转后的电平值。'
  },
  'value': {
    signature: 'pin.value([val: int]): int',
    doc: '若传入参数则写入电平值 (0 或 1)；若无参数则读取并返回当前引脚电平值。'
  },
  // WiFi 网络管理
  'wifi_connect': {
    signature: 'wifi_connect(ssid: string, [password: string], [timeout_ms: int]): bool',
    doc: '连接指定的 2.4GHz WiFi 热点。\n\n参数:\n- `ssid`: WiFi 名称\n- `password`: WiFi 密码 (开放网络可留空)\n- `timeout_ms`: 连接超时时间(毫秒, 默认 15000)\n\n返回值:\n连接成功并获取到 IP 返回 `true`, 失败返回 `false`。'
  },
  'wifi_disconnect': {
    signature: 'wifi_disconnect(): bool',
    doc: '断开当前的 WiFi 连接并释放网络接口。'
  },
  'wifi_is_connected': {
    signature: 'wifi_is_connected(): bool',
    doc: '查询当前设备是否已成功连接到 WiFi 并获取到有效 IPv4 地址。'
  },
  'wifi_ssid': {
    signature: 'wifi_ssid(): string | nil',
    doc: '获取当前已连接的 WiFi 热点名称 (SSID)。未连网时返回 `nil`。'
  },
  'wifi_ip': {
    signature: 'wifi_ip(): string | nil',
    doc: '获取当前设备在局域网中的 IPv4 地址 (如 `"192.168.1.100"`)。未连网时返回 `nil`。'
  },
  'wifi_mac': {
    signature: 'wifi_mac(): string',
    doc: '获取 WiFi 物理网卡的 MAC 地址字符串 (如 `"24:0A:C4:B8:31:AA"`)。'
  },
  'wifi_scan': {
    signature: 'wifi_scan(): array[map]',
    doc: '扫描周围可用的 WiFi 热点。返回热点字典数组，每个字典包含 `ssid`, `rssi` (信号强度), `auth` (加密类型)。'
  },
  'wifi_ap': {
    signature: 'wifi_ap(ssid: string, [password: string]): bool',
    doc: '开启 SoftAP 热点模式，使 ESP32 自身成为一个无线路由器供手机或电脑连接。'
  },
  // HTTP 网络通信
  'http_get': {
    signature: 'http_get(url: string, [timeout_ms: int]): map',
    doc: '发起 HTTP 或 HTTPS GET 请求。\n\n返回值字典:\n- `status`: HTTP 状态码 (如 200, 404)\n- `body`: 响应正文字符串\n- `ok`: 请求是否成功 (`status == 200`)\n- `error`: 错误信息 (失败时存在)'
  },
  'http_post': {
    signature: 'http_post(url: string, [body: string], [headers: string], [timeout_ms: int]): map',
    doc: '发起 HTTP 或 HTTPS POST 请求。\n\n参数:\n- `url`: 请求地址\n- `body`: 请求体正文数据\n- `headers`: 自定义请求头 (可选)\n\n返回值字典包含 `status`, `body`, `ok`, `error`。'
  },
  // SNTP 网络对时
  'sntp_sync': {
    signature: 'sntp_sync([ntp_server: string], [timeout_ms: int]): bool',
    doc: '通过网络与 NTP 服务器同步现实世界的时间，成功返回 `true`。同步后可通过 `time()` 或 `date()` 获取精确时间。'
  },
  // 外设类
  'PWM': {
    signature: 'PWM(pin: int, [freq: int = 1000], [duty: int = 0]): PWM',
    doc: '脉宽调制 (PWM / LEDC) 控制器。可用于电机调速、LED 呼吸灯及蜂鸣器音调控制。\n\n方法: `.duty(0..1023)`, `.freq(hz)`, `.duty_percent(0..100)`, `.stop()`'
  },
  'ADC': {
    signature: 'ADC(pin: int, [atten: string = "11db"]): ADC',
    doc: '模拟数字转换器 (ADC)。用于采集模拟传感器电压。\n\n方法:\n- `.read()`: 读取原始 12 位 ADC 值 (0..4095)\n- `.read_mv()`: 读取校准后的电压毫伏值 (mV)\n- `.atten(db)`: 设置衰减度'
  },
  'I2C': {
    signature: 'I2C([bus: int = 0], [sda: int], [scl: int], [freq: int = 400000]): I2C',
    doc: 'I2C 总线硬件控制器。用于连接 OLED 屏、温湿度计、陀螺仪等传感器。\n\n方法: `.scan()`, `.write(addr, data)`, `.read(addr, len)`, `.write_reg(addr, reg, data)`, `.read_reg(addr, reg, len)`'
  },
  'SPI': {
    signature: 'SPI([bus: int = 1], [sck: int], [mosi: int], [miso: int], [freq: int]): SPI',
    doc: 'SPI 高速总线硬件控制器。用于驱动 TFT 彩屏、SD 卡读写器等。\n\n方法: `.transfer(data)`, `.write(data)`, `.read(len)`'
  },
  'UART': {
    signature: 'UART([port: int = 1], [baud: int = 115200], [tx: int], [rx: int]): UART',
    doc: '通用异步收发器 (串口)。用于与外部串口模块 (如 GPS, 蓝牙, 4G 模组) 通信。\n\n方法: `.write(data)`, `.read([len])`, `.read_str()`, `.any()`'
  },
  // 核心内置函数
  'print': {
    signature: 'print(...values): nil',
    doc: '将参数格式化并输出到终端控制台，末尾自动追加换行。'
  },
  'help': {
    signature: 'help([topic]): nil',
    doc: '查看系统或模块帮助文档。支持直接传入模块名 (如 `help(sys)`, `help(esp32)`, `help(net)`)。'
  },
  'len': {
    signature: 'len(obj: string | array | map | bytes): int',
    doc: '获取字符串长度、数组元素数、字典键值对数或字节缓冲容量。'
  },
  'type': {
    signature: 'type(obj): string',
    doc: '返回对象的类型名称字符串 (如 `"int"`, `"float"`, `"string"`, `"array"`, `"map"`, `"Pin"`, `"bytes"`)。'
  },
  'ports': {
    signature: 'ports(): array[string]',
    doc: '扫描并列出本机当前所有可用的串口端口列表 (如 `["COM3", "COM4"]`)。'
  },
  'upload': {
    signature: 'upload(port: string, script_path: string): bool',
    doc: '将指定的脚本文件通过串口直接烧录写入 ESP32 的 `l_script` 裸 Flash 分区并热重载执行。'
  },
  'connect': {
    signature: 'connect(port: string, [baud: int = 115200]): nil',
    doc: '通过串口连接到 ESP32 板载交互终端 (REPL)。'
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
    if (text.startsWith('//')) {
      comments.unshift(text.replace(/^\/\/\s*/, ''));
      idx--;
    } else if (text.endsWith('*/')) {
      // 块注释
      while (idx >= 0) {
        const blkText = document.lineAt(idx).text.trim();
        comments.unshift(blkText.replace(/^\/\*+\s*|\*+\/|\s*\*\s*/g, ''));
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
 * 在文档中查找符号声明位置
 */
function findSymbolInDocument(document, word) {
  const lineCount = document.lineCount;
  // 1. 函数声明: fn <word>(...
  const fnRegex = new RegExp(`\\bfn\\s+(${word})\\s*\\(([^\\)]*)\\)`);
  // 2. 类声明: class <word>
  const classRegex = new RegExp(`\\bclass\\s+(${word})\\b`);
  // 3. 变量声明: (var|let|const) <word>
  const varRegex = new RegExp(`\\b(var|let|const)\\s+(${word})\\b`);

  for (let i = 0; i < lineCount; i++) {
    const line = document.lineAt(i);
    const text = line.text;

    let match = text.match(fnRegex);
    if (match) {
      const col = text.indexOf(match[1]);
      return {
        type: 'function',
        name: match[1],
        params: match[2],
        line: i,
        col: col,
        rawLine: text.trim(),
        doc: extractDocComment(document, i),
        uri: document.uri
      };
    }

    match = text.match(classRegex);
    if (match) {
      const col = text.indexOf(match[1]);
      return {
        type: 'class',
        name: match[1],
        line: i,
        col: col,
        rawLine: text.trim(),
        doc: extractDocComment(document, i),
        uri: document.uri
      };
    }

    match = text.match(varRegex);
    if (match) {
      const col = text.indexOf(match[2]);
      return {
        type: 'variable',
        kind: match[1],
        name: match[2],
        line: i,
        col: col,
        rawLine: text.trim(),
        doc: extractDocComment(document, i),
        uri: document.uri
      };
    }
  }
  return null;
}

/**
 * 查找类名上下文, 例如 Hook::exec 中的 Hook
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

function activate(context) {
  // 1. 注册 Hover 悬停提示
  const hoverProvider = vscode.languages.registerHoverProvider('l', {
    async provideHover(document, position, token) {
      const wordRange = document.getWordRangeAtPosition(position, /[a-zA-Z_][a-zA-Z0-9_]*/);
      if (!wordRange) return null;
      const word = document.getText(wordRange);

      // (1) 检查是否为内置库或内置函数
      if (BUILTIN_DOCS[word]) {
        const item = BUILTIN_DOCS[word];
        const md = new vscode.MarkdownString();
        md.appendCodeblock(item.signature, 'l');
        md.appendMarkdown(item.doc);
        return new vscode.Hover(md, wordRange);
      }

      // (2) 检查是否为用户定义的符号 (当前文档)
      let symbol = findSymbolInDocument(document, word);

      // (3) 若当前文档没找到，尝试在工作区其它 .l 文件查找
      if (!symbol && vscode.workspace.workspaceFolders) {
        const files = await vscode.workspace.findFiles('**/*.l', '**/node_modules/**', 10);
        for (const file of files) {
          if (file.toString() === document.uri.toString()) continue;
          try {
            const otherDoc = await vscode.workspace.openTextDocument(file);
            symbol = findSymbolInDocument(otherDoc, word);
            if (symbol) break;
          } catch (e) {}
        }
      }

      if (symbol) {
        const scope = getPrecedingScope(document, wordRange.start);
        const md = new vscode.MarkdownString();
        if (symbol.type === 'function') {
          const prefix = scope ? `${scope}::` : '';
          md.appendCodeblock(`fn ${prefix}${symbol.name}(${symbol.params})`, 'l');
        } else if (symbol.type === 'class') {
          md.appendCodeblock(`class ${symbol.name}`, 'l');
        } else if (symbol.type === 'variable') {
          md.appendCodeblock(`${symbol.kind} ${symbol.name}`, 'l');
        }
        if (symbol.doc) {
          md.appendMarkdown(`\n---\n${symbol.doc}`);
        }
        return new vscode.Hover(md, wordRange);
      }

      return null;
    }
  });

  // 2. 注册 DefinitionProvider (Ctrl+Click 跳转定义)
  const definitionProvider = vscode.languages.registerDefinitionProvider('l', {
    async provideDefinition(document, position, token) {
      const wordRange = document.getWordRangeAtPosition(position, /[a-zA-Z_][a-zA-Z0-9_]*/);
      if (!wordRange) return null;
      const word = document.getText(wordRange);

      // (1) 优先查找当前文档
      let symbol = findSymbolInDocument(document, word);

      // (2) 当前文档未找到，检索工作区其它 .l 文件
      if (!symbol && vscode.workspace.workspaceFolders) {
        const files = await vscode.workspace.findFiles('**/*.l', '**/node_modules/**', 20);
        for (const file of files) {
          if (file.toString() === document.uri.toString()) continue;
          try {
            const otherDoc = await vscode.workspace.openTextDocument(file);
            symbol = findSymbolInDocument(otherDoc, word);
            if (symbol) break;
          } catch (e) {}
        }
      }

      if (symbol) {
        const targetPos = new vscode.Position(symbol.line, symbol.col);
        return new vscode.Location(symbol.uri, targetPos);
      }

      return null;
    }
  });

  // 3. 注册 DocumentSymbolProvider (大纲与面包屑导航)
  const symbolProvider = vscode.languages.registerDocumentSymbolProvider('l', {
    provideDocumentSymbols(document, token) {
      const symbols = [];
      const lineCount = document.lineCount;
      const fnRegex = /^\s*fn\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^\\)]*)\)/;
      const classRegex = /^\s*class\s+([a-zA-Z_][a-zA-Z0-9_]*)/;

      for (let i = 0; i < lineCount; i++) {
        const line = document.lineAt(i);
        const text = line.text;

        let m = text.match(classRegex);
        if (m) {
          const sym = new vscode.DocumentSymbol(
            m[1],
            'class',
            vscode.SymbolKind.Class,
            line.range,
            line.range
          );
          symbols.push(sym);
          continue;
        }

        m = text.match(fnRegex);
        if (m) {
          const sym = new vscode.DocumentSymbol(
            m[1],
            `(${m[2]})`,
            vscode.SymbolKind.Function,
            line.range,
            line.range
          );
          symbols.push(sym);
        }
      }
      return symbols;
    }
  });

  context.subscriptions.push(hoverProvider, definitionProvider, symbolProvider);
}

function deactivate() {}

module.exports = {
  activate,
  deactivate
};
