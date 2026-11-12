#!/bin/bash
# 本地 Docker 打包 ESP32 固件, 与 .github/workflows/esp32-firmware.yml 流程一致
# 用法: docker run --rm -v <repo>:/project espressif/idf:v5.3 bash /project/_build_firmware.sh
set -e
git config --global --add safe.directory "*"
. "$IDF_PATH/export.sh" > /dev/null

cd /project/esp32
for t in esp32 esp32c3 esp32s3; do
  echo "===== set-target $t ====="
  idf.py set-target "$t" > /dev/null
  echo "===== build $t ====="
  idf.py build
  (cd build && esptool.py --chip "$t" merge_bin -o "l-$t-merged.bin" @flash_args)
  ls -lh "build/l-$t-merged.bin"
done
echo "===== ALL FIRMWARE BUILDS SUCCEEDED ====="
