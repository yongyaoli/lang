#!/bin/bash
# 本地 Docker 打包 ESP32 固件 (与 .github/workflows/esp32-firmware.yml 流程一致)
# 用法: docker run --rm -v <repo>:/project espressif/idf:v5.3 bash /project/_build_firmware.sh
set -e
git config --global --add safe.directory "*"
. "$IDF_PATH/export.sh" > /dev/null

mkdir -p /project/esp32/dist
cd /project/esp32
for t in esp32 esp32c3 esp32s3; do
  echo "===== set-target $t (build-$t) ====="
  idf.py -B "build-$t" set-target "$t" > /dev/null
  echo "===== build $t ====="
  idf.py -B "build-$t" build
  (cd "build-$t" && esptool.py --chip "$t" merge_bin -o "l-$t-merged.bin" @flash_args)
  cp "build-$t/l-$t-merged.bin" "dist/l-$t-merged.bin"
  ls -lh "dist/l-$t-merged.bin"
done
echo "===== ALL FIRMWARE BUILDS SUCCEEDED ====="
