cmake_minimum_required(VERSION 3.20)
project(launcher_port_test C CXX)
set(CMAKE_C_STANDARD 11)
# xbox_launcher_port (src/launcher/launcher_port.h): a port launcher built from the toolkit's main, setup window and
# progress UI. Builds a launcher exe like a port does and runs it with no Vulkan device (RECOMP_VK_TEST_NO_DEVICE=1,
# no dialogs): it must refuse with exit 1 before anything else, and print the apology with the port's title.
# the launcher is part of the Vulkan build (root CMakeLists.txt), as in every port
set(XBOXRECOMP_VULKAN ON CACHE BOOL "" FORCE)
add_subdirectory(../.. runtime)
if(NOT TARGET xbox_launcher_port)
    message(FATAL_ERROR "xbox_launcher_port missing (needs XBOXRECOMP_VULKAN and SDL3 / glslang)")
endif()
add_executable(launcher_port_test test_main.c)
target_link_libraries(launcher_port_test PRIVATE xbox_launcher_port)
if(MINGW)
    target_link_options(launcher_port_test PRIVATE -static)
endif()
enable_testing()
add_test(NAME launcher_port_no_vulkan COMMAND launcher_port_test --image does_not_exist.iso)
set_tests_properties(launcher_port_no_vulkan PROPERTIES
    ENVIRONMENT "RECOMP_VK_TEST_NO_DEVICE=1;RECOMP_NO_DIALOGS=1"
    PASS_REGULAR_EXPRESSION "Launcher Port Test.*Vulkan 1\\.3")
