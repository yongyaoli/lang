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
| [`mqtt_aliyun_iot.l`](mqtt_aliyun_iot.l) | `MqttClient` / 阿里云 Alink 物模型 | 阿里云物联网平台设备连接、属性上报 (Alink JSON) 与反向命令控制响应 | `l examples/mqtt_aliyun_iot.l` |
| [`mqtt_huaweicloud_iot.l`](mqtt_huaweicloud_iot.l) | `MqttClient` / 华为云 IoTDA | 华为云设备接入，多服务属性采集上报与云端控制指令下发回调 | `l examples/mqtt_huaweicloud_iot.l` |
| [`mqtts_huaweicloud_iot.l`](mqtts_huaweicloud_iot.l) | `MqttClient` / 华为云 MQTTS (8883) | 华为云 MQTTS 加密端点接入与高保真仿真演练，支持设备密钥认证与反向控制 | `l examples/mqtts_huaweicloud_iot.l` |
| [`tcp_client.l`](tcp_client.l) | `Tcp` 类 / Socket 客户端 | TCP 双向通信套接字，演示数据帧发送、超时接收与连接异常容错 | `l examples/tcp_client.l` |
| [`iot_sensor_json.l`](iot_sensor_json.l) | `JSON.stringify()` / `JSON.parse()` | 多传感器物理量采集，RFC 8259 规范的紧凑 JSON 序列化与云端上报模拟 | `l examples/iot_sensor_json.l` |
| [`http_server.l`](http_server.l) | `TcpServer` 类 / 80 端口 Web 服务 | 完整的嵌入式 Web 服务器，提供局域网网页控制台与 REST 接口 | `l examples/http_server.l` |
| [`ili9341_demo.l`](ili9341_demo.l) | `ILI9341` 驱动 / 彩色 TFT UI | 彩色液晶屏综合展示：顶栏看板、指标卡、同心圆状态灯与彩虹色块 | `l examples/ili9341_demo.l` |
| [`display_chinese_demo.l`](display_chinese_demo.l) | SSD1306 / ILI9341 / 中文字库 | 双屏幕 16x16 汉字与 ASCII 中英文无缝混排及动态字模扩展实测 | `l examples/display_chinese_demo.l` |
| [`servo_radar_demo.l`](servo_radar_demo.l) | `HC_SR04` / `Servo` | 超声波倒车雷达与舵机全景雷达扫描仪 (障碍物扫描与 ASCII 可视化) | `l examples/servo_radar_demo.l` |
| [`vfs_file_demo.l`](vfs_file_demo.l) | `FS` / `File` 统一虚拟文件系统 | 配置文件读写、大文件流式时序日志存储与动态多模块 import | `l examples/vfs_file_demo.l` |

### 4. 传感器开源驱动与中文字库 ([drivers/](drivers/))
| 驱动文件 | 外设型号与接口 | 功能与特性 |
| :--- | :--- | :--- |
| [`drivers/font_cn.l`](drivers/font_cn.l) | 16x16 常用点阵中文字库 | 纯 L 语言实现，内置常用工业环境物联网汉字字模，支持运行时动态注册扩展 |
| [`drivers/SSD1306.l`](drivers/SSD1306.l) | 0.96 寸 I2C OLED 屏幕 | 内置 1KB 显存缓冲，支持 5x7 ASCII 与 16x16 中文字符垂直混排 |
| [`drivers/ILI9341.l`](drivers/ILI9341.l) | 2.4/2.8/3.2寸 SPI 彩色 TFT 屏 | 240x320 分辨率，RGB565 65K 色，支持旋转、几何图形与中英文混排多倍缩放 (演示见 `ili9341_demo.l`) |
| [`drivers/AHT20.l`](drivers/AHT20.l) | I2C 高精度温湿度传感器 | 20 位原码校验与温度 (℃) / 湿度 (%RH) 浮点物理量转换 |
| [`drivers/HC_SR04.l`](drivers/HC_SR04.l) | GPIO 超声波测距传感器 | 微秒级脉宽测距、环境温度声速动态补偿与平滑中位数滤波 |
| [`drivers/Servo.l`](drivers/Servo.l) | PWM 180°/360° 舵机角度控制器 | 20ms 标准周期控制、微秒脉冲写入、平滑步进旋转与 360° 连续调速 |
| [`drivers/WS2812.l`](drivers/WS2812.l) | 单总线 GPIO RGB 幻彩灯珠 | GRB 色彩缓冲与彩虹渐变色轮算法 (`wheel()`) |
| [`drivers/demo_all.l`](drivers/demo_all.l) | 多传感器联动工作站 | OLED 屏幕实时绘制温湿度数值并同步幻彩状态指示灯 |

### 5. 无线空中升级与系统运维诊断 (OTA & Diagnostics)
| 示例文件 | 核心 API / 硬件 | 功能说明 | 运行命令 |
| :--- | :--- | :--- | :--- |
| [`board_diag.l`](board_diag.l) | `ESP32` / `I2C` / `Pin` / `WiFi` | **板子全方位深度体检诊断**：芯片型号、SRAM/PSRAM/Flash、WiFi/IP/MAC、GPIO 安全与电平检测、I2C 外设指纹智能识别扫描 | `l examples/board_diag.l` |
| [`ota_remote_update.l`](ota_remote_update.l) | `OTA.update_script()` | 远程 HTTP(S) 空中拉取最新应用脚本，IEEE 802.3 CRC32 防篡改校验并热重载 | `l examples/ota_remote_update.l` |
| [`web_ota_server.l`](web_ota_server.l) | `OTA.update_from_string()` / `TcpServer` | 局域网 Web OTA 控制台后台，手机/电脑浏览器打开即在线热更新代码 | `l examples/web_ota_server.l` |

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
