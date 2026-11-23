cmake_minimum_required(VERSION 3.20)
project(mmx_neon_test C)
set(CMAKE_C_STANDARD 11)
add_executable(mmx_neon_test test_main.c)
# The NEON forms are opt-in until this test has passed on a device.
target_compile_definitions(mmx_neon_test PRIVATE RECOMP_MMX_USE_NEON)
enable_testing()
add_test(NAME mmx_neon_test COMMAND mmx_neon_test)
# Negative control: with the NEON forms switched off an ARM64 build must fail,
# so the comparison above can never pass by comparing the portable forms with
# themselves. Elsewhere both skip.
add_executable(mmx_neon_test_off test_main.c)
target_compile_definitions(mmx_neon_test_off PRIVATE RECOMP_MMX_USE_NEON RECOMP_MMX_SCALAR)
add_test(NAME mmx_neon_test_off COMMAND mmx_neon_test_off)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set_tests_properties(mmx_neon_test_off PROPERTIES WILL_FAIL TRUE)
endif()
