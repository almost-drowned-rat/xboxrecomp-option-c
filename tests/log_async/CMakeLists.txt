cmake_minimum_required(VERSION 3.20)
project(log_async_test C)
# The logger thread behind src/kernel/log_async.h: order per thread, nothing lost that is not counted as dropped,
# long lines cut as lines, non-console streams untouched, drain. Two arms: RECOMP_LOG_SYNC is read once per process.
set(CMAKE_C_STANDARD 11)
add_subdirectory(../.. runtime)
add_executable(log-async-test test_main.c)
target_link_libraries(log-async-test PRIVATE xbox_log)
enable_testing()
add_test(NAME log_async_queued COMMAND log-async-test)
add_test(NAME log_async_sync COMMAND log-async-test)
set_tests_properties(log_async_sync PROPERTIES ENVIRONMENT "RECOMP_LOG_SYNC=1")
