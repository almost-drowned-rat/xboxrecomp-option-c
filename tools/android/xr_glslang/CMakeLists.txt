# libxr_glslang.so - glslang's C interface as ONE shared library for Android (arm64-v8a), built once on the PC and
# shipped in the APK's nativeLibraryDir. The game library that is compiled and linked ON THE DEVICE (toolkit .a +
# generated C) names it as NEEDED instead of linking glslang's 57 MB static archive, so the on-device link stays
# pure C: the C++ runtime (c++_static) lives inside this library, and only the glslang_* C functions are exported
# (exports.map: "local: *" hides libc++ and glslang's C++ symbols; --exclude-libs,ALL is NOT used - it would hide
# the glslang_* functions too, since they come from an archive). nv2a_vk uses nothing else of glslang (the C interface, no
# optimizer - src/nv2a_vk/CMakeLists.txt).
#
#   cmake -S tools/android/xr_glslang -B <out> -G Ninja -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static
#         -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -Dglslang_DIR=<glslang prefix>/lib/cmake/glslang
#
# Also builds xr_glslang_link_test: a C program linked against the library (dynamic), to show the C-only link works.
cmake_minimum_required(VERSION 3.20)
project(xr_glslang C CXX)

find_package(glslang CONFIG REQUIRED)

# No sources of its own: a stub so CMake has something to compile, the archives come in whole.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/xr_glslang_stub.c "/* libxr_glslang.so: glslang C interface only */\n")
add_library(xr_glslang SHARED ${CMAKE_CURRENT_BINARY_DIR}/xr_glslang_stub.c)
set_target_properties(xr_glslang PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(xr_glslang PRIVATE
    -Wl,--whole-archive
    glslang::glslang glslang::glslang-default-resource-limits
    -Wl,--no-whole-archive)
target_link_options(xr_glslang PRIVATE
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map
    -Wl,--gc-sections
    -Wl,-z,max-page-size=16384
    -s)

add_executable(xr_glslang_link_test link_test.c)
target_link_libraries(xr_glslang_link_test PRIVATE xr_glslang)
get_target_property(_inc glslang::glslang INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(xr_glslang_link_test PRIVATE ${_inc})
