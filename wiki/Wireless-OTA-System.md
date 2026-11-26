# OTA 无线空中升级与 Web OTA 控制台

L 语言提供了一套极速、安全、免插线、带沙箱防护的空中升级（OTA）方案。底包终身不变，只升级顶层业务脚本文本。

---

## 1. 两种主流升级模式

| 模式 | 运行机制 | 适用场景 |
| :--- | :--- | :--- |
| **局域网 Web OTA 控制台** | ESP32 启动轻量 8080 Web 控制台，手机/电脑浏览器打开网页一键推送 | 现场免拆壳无线调试、小白无代码门槛 |
| **远程 HTTP 云端静默拉取** | 设备定期向公司云端服务器检查版本，自动拉取、CRC 校验并热重载 | 工业设备批量量产、野外设备无人值守 |

---

## 2. 模式一：局域网 Web OTA 控制台（一秒上手）

项目中自带了工业级的 Web OTA 服务端程序：[`examples/web_ota_server.l`](file:///d:/github/langgit/examples/web_ota_server.l)。

### 使用步骤：
1. 打开脚本，填入您局域网的 WiFi 名称和密码：
   ```l
   let wifi_ssid = "你的WiFi";
   let wifi_pass = "你的密码";
   ```
2. 首次通过串口烧录到 ESP32：
   ```powershell
   l -u COM3 examples/web_ota_server.l
   ```
3. 查看串口日志获取板端 IP，例如 `192.168.10.14`；
4. **拔掉电脑 USB 线，无需再插线！** 电脑或手机浏览器打开：
   👉 `http://192.168.10.14:8080/`
5. 在深蓝色的控制台网页中直接输入您的新 L 语言代码，点击 **“🚀 一键无线 OTA 烧录到 ESP32”**，0.2 秒开发板即可热重载运行！

### 🛡️ 防崩溃沙箱保护机制
* 哪怕您在网页上写错了语法、除以 0、遗漏变量，**Web 服务与 8080 端口绝对不崩溃**；
* 错误信息会直接在浏览器上以**红色高亮**精准提示，修改后随时再点一次即可生效！

---

## 3. 模式二：远程 HTTP 云端静默升级

设备日常运行中，自动访问您的云端 CDN 或 Web 服务器拉取最新脚本：

```l
import "ota";

let FIRMWARE_URL = "http://my-iot-cdn.com/firmware/v2.1.0/sensor_app.l";
let EXPECTED_CRC = 0xA1B2C3D4; // 预期的 CRC32 (防篡改/防丢包)

print("正在检查云端新版本...");
try {
    // 参数: (url, expected_crc, auto_exec)
    let res = OTA.update_script(FIRMWARE_URL, EXPECTED_CRC, true);
    print("OTA 升级成功! 写入", res.bytes, "字节, 新 CRC:", res.crc32);
} catch (e) {
    print("OTA 升级中断 (原程序保持安全运行):", e);
}
```
