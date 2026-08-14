cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(zmk_ble_passkey)

zephyr_include_directories(include)
zephyr_include_directories(${CMAKE_SOURCE_DIR}/include)
add_subdirectory(src)
