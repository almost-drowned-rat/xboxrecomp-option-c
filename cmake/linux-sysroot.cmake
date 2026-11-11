# linux-sysroot.cmake - build for a Linux RELEASE with the shipped toolchain (tools/linux/make_toolchain_bundle.sh):
# LLVM 23 clang + lld against the bundle's glibc 2.35 sysroot, C++ with LLVM's libc++ linked statically. What is
# built this way runs on every distribution with glibc >= 2.35 and needs no C++ runtime of the player's system.
#
#   cmake -S <src> -B <build> -G Ninja -DCMAKE_TOOLCHAIN_FILE=<toolkit>/cmake/linux-sysroot.cmake \
#         -DXR_LINUX_BUNDLE=<bundle out folder, the one holding llvm/> [-DCMAKE_PREFIX_PATH=<SDL3;Vulkan headers>]
#
# XR_LINUX_BUNDLE may also come from the environment. Building C++ (glslang) needs the developer bundle
# (make_toolchain_bundle.sh --cxx-headers); C alone works with the player's bundle too.
# Packages found by find_package come from CMAKE_PREFIX_PATH (headers you built or unpacked, e.g. SDL3) or the
# sysroot; libvulkan.so in the sysroot is a link stub (SONAME libvulkan.so.1), the system's loader runs the game.

if(NOT XR_LINUX_BUNDLE AND DEFINED ENV{XR_LINUX_BUNDLE})
    set(XR_LINUX_BUNDLE "$ENV{XR_LINUX_BUNDLE}")
endif()
if(NOT XR_LINUX_BUNDLE OR NOT EXISTS "${XR_LINUX_BUNDLE}/llvm/bin/clang")
    message(FATAL_ERROR "linux-sysroot.cmake: set XR_LINUX_BUNDLE to the bundle folder (with llvm/bin/clang)")
endif()
# try_compile projects get the variable too
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XR_LINUX_BUNDLE)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(_xr_llvm "${XR_LINUX_BUNDLE}/llvm")
set(CMAKE_C_COMPILER "${_xr_llvm}/bin/clang")
set(CMAKE_CXX_COMPILER "${_xr_llvm}/bin/clang++")
set(CMAKE_SYSROOT "${_xr_llvm}/sysroot")

set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")
# lld; the static libc++ / libc++abi / libunwind sit in llvm/lib/x86_64-unknown-linux-gnu (only .a there, so -lc++
# is static by itself). C targets that link C++ archives (the game exe with glslang) name them explicitly through
# XR_LINUX_CXX_STATIC_LIBS.
set(_xr_cxxlib "${_xr_llvm}/lib/x86_64-unknown-linux-gnu")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -L${_xr_cxxlib}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld -L${_xr_cxxlib}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld -L${_xr_cxxlib}")
set(XR_LINUX_CXX_STATIC_LIBS "${_xr_cxxlib}/libc++.a;${_xr_cxxlib}/libc++abi.a;${_xr_cxxlib}/libunwind.a;pthread;m"
    CACHE STRING "the static C++ runtime a C target links when it pulls in C++ archives (glslang)")

# programs (python, glslangValidator ...) come from the host; libraries, headers and packages from the sysroot AND
# the prefix path (SDL3 / Vulkan headers built outside the sysroot)
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
