cmake_minimum_required(VERSION 3.20)
project(kernel_file_posix_test C)
# NtSetInformationFile on a POSIX host (the Windows branch is covered by
# tests/kernel_file_rename and the titles' own runs).
if(WIN32)
    message(FATAL_ERROR "This regression covers the POSIX file-API path")
endif()
set(CMAKE_C_STANDARD 11)
add_subdirectory(../.. runtime)
add_executable(kernel_file_posix_test test_main.c)
target_include_directories(kernel_file_posix_test PRIVATE ../../src/kernel)
target_link_libraries(kernel_file_posix_test PRIVATE xboxrecomp)
enable_testing()
add_test(NAME kernel_file_posix_test COMMAND kernel_file_posix_test)
