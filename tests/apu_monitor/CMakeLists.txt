cmake_minimum_required(VERSION 3.20)
project(apu_monitor_test C)
set(CMAKE_C_STANDARD 11)
add_subdirectory(../.. runtime)
add_executable(apu_monitor_test test_main.c)
target_include_directories(apu_monitor_test PRIVATE ../../src ../../src/apu)
# The whole runtime: the APU delivers its interrupt and resolves physical
# addresses through the kernel.
target_link_libraries(apu_monitor_test PRIVATE xboxrecomp)
enable_testing()
add_test(NAME apu_monitor COMMAND apu_monitor_test)
# The VP/DSP mix reaches the output only with RECOMP_APU_VP_OUT=1 (default off until its content is right).
set_tests_properties(apu_monitor PROPERTIES ENVIRONMENT "RECOMP_APU_VP_OUT=1")
