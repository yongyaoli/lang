# L 语言标准示例程序库 (Examples)

本目录汇集了 L 语言针对 **ESP32 嵌入式硬件、无线网络（WiFi / BLE）及物联网常用组件** 的官方标准示例程序。所有程序均纯原生实现、具备详尽的中文注释，并支持 **PC 模拟器离线演练** 与 **ESP32 开发板真实烧录** 双模执行。

---

## 📂 示例索引与功能说明

### 1. 无线网络 (WiFi)
| 示例文件 | 核心 API / 硬件 | 功能说明 | 运行命令 |
| :--- | :--- | :--- | :--- |
| [`wifi_scan.l`](wifi_scan.l) | `wifi_scan()` / `wifi_mac()` | 扫描周围 2.4GHz 无线热点，按信号等级 (RSSI) 排序并展示加密类型 | `l examples/wifi_scan.l` |
| [`wifi_sta_http.l`](wifi_sta_http.l) | `wifi_connect()` / `http_get()` / `http_post()` | STA 模式连接家用 WiFi，查询 IP/MAC，发起 HTTP GET/POST 网络通信 | `l examples/wifi_sta_http.l` |
| [`wifi_ap_config.l`](wifi_ap_config.l) | `wifi_ap()` / `TcpServer` | 开启 SoftAP 热点模式并启动 Web 配网后台，手机连入浏览器即可配置参数 | `l examples/wifi_ap_config.l` |
| [`sntp_clock.l`](sntp_clock.l) | `sntp_sync()` / `time()` | 通过 NTP 服务器同步国家标准时间，构建免 RTC 硬件的精准数字时钟 | `l examples/sntp_clock.l` |

### 2. 低功耗蓝牙 (BLE)
| 示例文件 | 核心 API / 硬件 | 功能说明 | 运行命令 |
| :--- | :--- | :--- | :--- |
| [`ble_beacon.l`](ble_beacon.l) | `BLE.init()` / `BLE.adv_start()` | BLE 广播信标，手机 App (如 nRF Connect) 可发现广播名称并监听心跳 | `l examples/ble_beacon.l` |
| [`ble_uart.l`](ble_uart.l) | `BLE.recv()` / `BLE.send()` / `Pin` | 蓝牙串口无线透传与手机远程遥控，接收手机端 "LED ON" / "LED OFF" 开关灯 | `l examples/ble_uart.l` |

### 3. 物联网与传输协议 (IoT Protocols)
| 示例文件 | 核心 API / 硬件 | 功能说明 | 运行命令 |
| :--- | :--- | :--- | :--- |
| [`tcp_client.l`](tcp_client.l) | `Tcp` 类 / Socket 客户端 | TCP 双向通信套接字，演示数据帧发送、超时接收与连接异常容错 | `l examples/tcp_client.l` |
| [`iot_sensor_json.l`](iot_sensor_json.l) | `JSON.stringify()` / `JSON.parse()` | 多传感器物理量采集，RFC 8259 规范的紧凑 JSON 序列化与云端上报模拟 | `l examples/iot_sensor_json.l` |
| [`http_server.l`](http_server.l) | `TcpServer` 类 / 80 端口 Web 服务 | 完整的嵌入式 Web 服务器，提供局域网网页控制台与 REST 接口 | `l examples/http_server.l` |

### 4. 传感器开源驱动库 ([drivers/](drivers/))
| 驱动文件 | 外设型号与接口 | 功能与特性 |
| :--- | :--- | :--- |
| [`drivers/SSD1306.l`](drivers/SSD1306.l) | 0.96 寸 I2C OLED 屏幕 | 内置 1KB 显存缓冲与 5x7 ASCII 字符点阵绘制 |
| [`drivers/AHT20.l`](drivers/AHT20.l) | I2C 高精度温湿度传感器 | 20 位原码校验与温度 (℃) / 湿度 (%RH) 浮点物理量转换 |
| [`drivers/WS2812.l`](drivers/WS2812.l) | 单总线 GPIO RGB 幻彩灯珠 | GRB 色彩缓冲与彩虹渐变色轮算法 (`wheel()`) |
| [`drivers/demo_all.l`](drivers/demo_all.l) | 多传感器联动工作站 | OLED 屏幕实时绘制温湿度数值并同步幻彩状态指示灯 |

---

## 🚀 运行方式

### ① PC 宿主环境直接运行
```bash
l examples/wifi_scan.l
l examples/iot_sensor_json.l
```

### ② 极速烧录到 ESP32 开发板运行 (0.2 秒直达)
```bash
# 1. 扫描串口获取端口号 (如 COM3)
l -p

# 2. 一键烧录到板端运行
l -u COM3 examples/wifi_scan.l
```

### ③ VS Code 中一键运行 / 烧录
- 打开任意 `.l` 文件，点击右上角 **`▷ 运行`** 或 **`⚡ 烧录`** 按钮；
- 或在编辑器中点击鼠标右键，选择 **「在 L 解释器中运行当前脚本」** 或 **「烧录脚本到 ESP32 开发板」**。
