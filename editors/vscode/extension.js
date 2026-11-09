const vscode = require('vscode');

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
}

function deactivate() {}

module.exports = {
  activate,
  deactivate
};
