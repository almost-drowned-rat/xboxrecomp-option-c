cmake_minimum_required(VERSION 3.20)
project(mmx_sse2_test C)
set(CMAKE_C_STANDARD 11)
add_executable(mmx_sse2_test test_main.c)
enable_testing()
add_test(NAME mmx_sse2_test COMMAND mmx_sse2_test)
# Negative control: with the SSE2 forms switched off an x86 build must fail,
# so the comparison above can never pass by comparing the portable forms with
# themselves.
add_executable(mmx_sse2_test_off test_main.c)
target_compile_definitions(mmx_sse2_test_off PRIVATE RECOMP_MMX_SCALAR)
add_test(NAME mmx_sse2_test_off COMMAND mmx_sse2_test_off)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|i.86|x86")
    set_tests_properties(mmx_sse2_test_off PROPERTIES WILL_FAIL TRUE)
endif()
