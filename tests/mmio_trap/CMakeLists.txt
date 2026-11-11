cmake_minimum_required(VERSION 3.20)
project(mmio_trap_test C)
set(CMAKE_C_STANDARD 11)
# Only the platform layer (win32_compat.c's signal-backed VEH on POSIX, mmio_decode.h): no SDL3, so the test also
# links as a static arm64 binary (NDK, -static) that qemu-aarch64 runs without an Android system (2026-11-11).
add_subdirectory(../../src/platform platform)
add_executable(mmio_trap_test test_main.c)
target_include_directories(mmio_trap_test PRIVATE ../../src ../../src/platform)
target_link_libraries(mmio_trap_test PRIVATE platform)
enable_testing()
add_test(NAME mmio_trap COMMAND mmio_trap_test)
