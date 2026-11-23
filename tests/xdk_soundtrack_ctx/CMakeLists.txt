cmake_minimum_required(VERSION 3.20)
project(xdk_soundtrack_ctx_test C)
# The soundtrack callbacks get the guest register context (2026-11-18, THPS 2X 2.0).
set(CMAKE_C_STANDARD 11)
add_subdirectory(../.. runtime)
add_executable(xdk_soundtrack_ctx_test test_main.c)
target_include_directories(xdk_soundtrack_ctx_test PRIVATE ../../src/kernel ../../src/xdk ../../src/media)
target_link_libraries(xdk_soundtrack_ctx_test PRIVATE xboxrecomp)
enable_testing()

set(XBE ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/conformance/test.xbe)
add_test(NAME xdk_soundtrack_ctx_test COMMAND xdk_soundtrack_ctx_test ${XBE})
