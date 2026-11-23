cmake_minimum_required(VERSION 3.20)
project(apu_module_test C)
set(CMAKE_C_STANDARD 11)
set(XBOXRECOMP_APU_SHARED ON CACHE BOOL "" FORCE)    # the APU module (docs/apu_module.md)
add_subdirectory(../.. runtime)
add_executable(apu_module_test test_main.c)
target_include_directories(apu_module_test PRIVATE ../../src ../../src/apu)
# The whole runtime: the module's services (interrupts, logger, audio output) are the exe's.
# CMake copies the module next to this exe after its build (src/apu/CMakeLists.txt).
target_link_libraries(apu_module_test PRIVATE xboxrecomp)
enable_testing()
add_test(NAME apu_module_loaded COMMAND apu_module_test)
add_test(NAME apu_module_none COMMAND apu_module_test none)
set_tests_properties(apu_module_none PROPERTIES ENVIRONMENT "RECOMP_APU_MODULE=no_such_module_file")
