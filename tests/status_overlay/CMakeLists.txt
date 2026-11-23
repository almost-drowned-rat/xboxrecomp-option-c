cmake_minimum_required(VERSION 3.20)
project(status_overlay_test C)
# The status overlay panels (src/nv2a_vk/status_overlay.c, 2026-11-19): layout, font, cache. Standalone: only
# status_overlay.c and the generated font header (made here as in src/nv2a_vk/CMakeLists.txt), no Vulkan.
set(CMAKE_C_STANDARD 11)
set(_nv ${CMAKE_CURRENT_SOURCE_DIR}/../../src/nv2a_vk)
set(_font_src ${_nv}/third_party/dejavu/DejaVuSansMono-Bold.ttf)
file(READ ${_font_src} _font_hex HEX)
file(SIZE ${_font_src} _font_len)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _font_hex "${_font_hex}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dejavu_mono_bold.h
     "static const unsigned char s_font_ttf[] = { ${_font_hex} };\n"
     "static const unsigned int s_font_ttf_len = ${_font_len}u;\n")
add_executable(status_overlay_test test_main.c ${_nv}/status_overlay.c)
target_include_directories(status_overlay_test PRIVATE ${_nv} ${CMAKE_CURRENT_BINARY_DIR})
if(NOT MSVC)
    target_link_libraries(status_overlay_test PRIVATE m)
endif()
enable_testing()
add_test(NAME status_overlay_test COMMAND status_overlay_test ${CMAKE_CURRENT_BINARY_DIR})
