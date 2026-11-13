#!/bin/bash
# 用法: run_one.sh <脚本> [--vm|--ast]
cd /mnt/d/github/lang_g || exit 1
mode="${2:---vm}"
export ASAN_OPTIONS=detect_leaks=0
./build-test/l_asan $mode "$1" 2>&1 | head -40
echo "rc=${PIPESTATUS[0]}"
