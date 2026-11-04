cmake_minimum_required(VERSION 3.20)
project(kernel_thread_object_test C)
# Windows only: spawned guest threads are Win32 threads.
set(CMAKE_C_STANDARD 11)
add_subdirectory(../.. runtime)
add_executable(kernel_thread_object_test test_main.c)
target_include_directories(kernel_thread_object_test PRIVATE ../../src/kernel)
target_link_libraries(kernel_thread_object_test PRIVATE xboxrecomp)
enable_testing()

set(XBE ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/conformance/test.xbe)
add_test(NAME kernel_thread_object_test COMMAND kernel_thread_object_test ${XBE})
