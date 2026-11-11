cmake_minimum_required(VERSION 3.20)
project(compat_bionic_test C)
# The Android (bionic) branches of win32_compat.c, forced on a glibc host: no pthread_cancel, memfd through the
# system call (Android before API 30). POSIX only.
if(WIN32)
    message(FATAL_ERROR "This test covers the POSIX compat layer's bionic branches")
endif()
set(CMAKE_C_STANDARD 11)
add_subdirectory(../../src/platform platform)
target_compile_definitions(platform PRIVATE XR_COMPAT_NO_PTHREAD_CANCEL=1 XR_COMPAT_MEMFD_SYSCALL=1)
add_executable(compat_bionic_test test_main.c)
target_link_libraries(compat_bionic_test PRIVATE platform)
enable_testing()
add_test(NAME compat_bionic_test COMMAND compat_bionic_test)
