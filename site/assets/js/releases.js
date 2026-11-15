/* ============================================================
   版本与下载清单 —— 全站唯一的版本数据源
   发版时只需改 version / released / assets[*].file
   published: true  → 指向 releases/latest/download/<file>
   published: false → 指向 Releases 列表页（文件尚未由 CI 发布时）
   产物由 .github/workflows/release-pc.yml 与 esp32-firmware.yml 共同生成
   ============================================================ */
window.L_RELEASES = {
  version: "v0.1.9",
  released: "2026-11-15",
  repo: "https://github.com/yongyaoli/lang",
  assets: {
    winSetup: { file: "lang-setup-windows-x86_64.exe", published: true, note: "Windows 10/11 · x64 · 自动写入 PATH · 内置 VS Code/CodeBuddy 扩展与 .l 文件关联" },
    winPortable: { file: "lang-windows-x86_64.zip", published: true, note: "免安装便携版 · 解压即用" },
    linuxX64: { file: "lang-linux-x86_64.tar.gz", published: true, note: "Linux x86_64 · 预编译二进制" },
    macArm64: { file: "lang-macos-arm64.tar.gz", published: true, note: "Apple Silicon（M 系列）· 预编译二进制" },
    macX64: { file: "lang-macos-x86_64.tar.gz", published: true, note: "Intel Mac · 预编译二进制" },
    unix: { file: "lang-unix.zip", published: true, note: "任意平台 · 源码 + install.sh 自动编译" },
    fwEsp32: { file: "l-esp32-merged.bin", published: true, note: "经典 ESP32 · 单文件合并固件 · 刷写地址 0x0" },
    fwEsp32c3: { file: "l-esp32c3-merged.bin", published: true, note: "ESP32-C3 · 单文件合并固件 · 刷写地址 0x0" },
    fwEsp32s3: { file: "l-esp32s3-merged.bin", published: true, note: "ESP32-S3 · 单文件合并固件 · 刷写地址 0x0" }
  }
};
