# L 语言 ESP32 固件顶层 CMakeLists.txt (基于 ESP-IDF v5.x)
cmake_minimum_required(VERSION 3.16)

# 将仓库根目录注册为外部组件目录, 确保 CMake/Ninja 能够绝对寻址父目录所有源码
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(l_esp32_firmware)
