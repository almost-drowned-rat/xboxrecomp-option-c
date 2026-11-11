cmake_minimum_required(VERSION 3.20)
project(compat_events_test C)
# Lost-wakeup check of the POSIX compat layer's events (2026-11-11): SetEvent + an
# immediate ResetEvent and PulseEvent must release a waiting thread, manual- and auto-reset. POSIX only.
if(WIN32)
    message(FATAL_ERROR "This test covers the POSIX compat layer's events")
endif()
set(CMAKE_C_STANDARD 11)
add_subdirectory(../../src/platform platform)
add_executable(compat_events_test evtest.c)
target_link_libraries(compat_events_test PRIVATE platform)
enable_testing()
add_test(NAME compat_events_test COMMAND compat_events_test)
