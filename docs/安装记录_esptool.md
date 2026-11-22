# 软件与依赖安装记录: esptool

- **安装时间**: 2026-11-22 10:33
- **安装环境**: Windows 11 (Python 3.13)
- **工具名称**: `esptool` (乐鑫官方 ESP 芯片串口烧录与诊断工具)
- **安装版本**: 5.6.0
- **安装命令**: `C:\Python\Python313\python.exe -m pip install esptool`
- **主要依赖**:
  - `esp-pylib` 1.2.1
  - `pyserial` 3.5
  - `reedsolo` 1.7.0
  - `intelhex` 2.3.0
  - `rich_click` 1.9.9
- **用途说明**:
  用于在本地直接连接板载 COM3 串口，执行 ESP32-C3 芯片探测（`chip_id`）、Flash 状态读取（`flash_id`）、全片擦除与固件烧录诊断。
