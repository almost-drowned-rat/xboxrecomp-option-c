# SPDX-License-Identifier: MIT
# Native part of the option-c Android APK template (from Panzer Dragoon Orta Recomp's packaging/android,
# 2026-11-11; template since 2026-11-19). Proven on the Galaxy S23 and the AYN Odin 2 Portal (Adreno 740); the Galaxy
# Tab S8 (Adreno 730, Vulkan 1.1) runs through Turnip. The game code is never linked into the APK: the launcher builds
# the game library on the device. The port's names come from ${PORT_DIR}/port_android.h.
cmake_minimum_required(VERSION 3.22)
project(xr_android C)

if(NOT SDL3_SRC)
    message(FATAL_ERROR "-DSDL3_SRC=<SDL3 source> missing (app/build.gradle passes it)")
endif()
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${SDL3_SRC} sdl3 EXCLUDE_FROM_ALL)

# The port's folder: port_android.h (names), port_icon.h (the launcher icon as a BMP array), setup_ui_sdl.c (the
# launcher's progress UI in the port's look)
if(NOT PORT_DIR OR NOT EXISTS "${PORT_DIR}/port_android.h")
    message(FATAL_ERROR "-DPORT_DIR=<folder with port_android.h, port_icon.h, setup_ui_sdl.c> missing (-PportDir)")
endif()

add_library(main SHARED loader/loader.c loader/pylift.c loader/launcher_android.c loader/touchpad.c loader/driver_check.c smoke/vk_smoke.c)
target_link_libraries(main PRIVATE SDL3::SDL3 log dl)
target_include_directories(main PRIVATE ${PORT_DIR})

# The first-start launcher = the toolkit's (option-c src/launcher + its installer, zip reader, icon registry, install
# root), compiled from the pinned toolkit worktree into libmain.so - only these files, never the runtime (that one
# is in the game library, built on the phone).
if(NOT XR_TOOLKIT)
    message(FATAL_ERROR "-DXR_TOOLKIT=<option-c toolkit worktree> missing (-Ptoolkit)")
endif()
set(TK ${XR_TOOLKIT}/src)
target_sources(main PRIVATE ${TK}/launcher/launcher.c ${TK}/launcher/sha256.c ${TK}/install/game_setup.c
               ${PORT_DIR}/setup_ui_sdl.c ${TK}/install/xdvdfs.c ${TK}/shaderpack/zip_read.c
               ${TK}/shaderpack/third_party/miniz/miniz_tinfl.c ${TK}/kernel/window_icon.c ${TK}/platform/host_paths.c)
target_include_directories(main PRIVATE ${TK}/launcher ${TK}/install ${TK}/shaderpack ${TK}/shaderpack/third_party/miniz
                           ${TK}/kernel ${TK} ${CMAKE_CURRENT_BINARY_DIR}/miniz)
# as the toolkit's src/shaderpack/CMakeLists.txt: miniz_export.h empty (static), only tinfl
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/miniz/miniz_export.h "#define MINIZ_EXPORT\n")
target_compile_definitions(main PRIVATE MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_DEFLATE_APIS
                           MINIZ_NO_ARCHIVE_APIS MINIZ_NO_ZLIB_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

# The embedded CPython for the on-device lift (python.org's official Android package, python-3.15.x-android;
# libpython3.15.so goes into the APK's jniLibs, the stdlib into its assets).
if(PYTHON_PREFIX)
    add_library(xr_python SHARED IMPORTED)
    set_target_properties(xr_python PROPERTIES IMPORTED_LOCATION "${PYTHON_PREFIX}/lib/libpython3.15.so"
                          INTERFACE_INCLUDE_DIRECTORIES "${PYTHON_PREFIX}/include/python3.15")
    target_link_libraries(main PRIVATE xr_python)
    # `python` for the launcher's lift steps (processes; exec only from nativeLibraryDir -> lib*.so)
    add_executable(python_exe loader/python_exe.c)
    set_target_properties(python_exe PROPERTIES OUTPUT_NAME "libpython_exe" SUFFIX ".so"
                          RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
    target_link_libraries(python_exe PRIVATE xr_python)
    target_link_options(python_exe PRIVATE "-Wl,-z,max-page-size=16384")
    add_dependencies(main python_exe)
else()
    message(FATAL_ERROR "-DPYTHON_PREFIX=<python.org Android package prefix> missing (-PpythonPrefix)")
endif()

# Route probes (the riskiest assumptions of "everything on the phone", 2026-11-11):
# probe_dl = a library dlopen'ed from internal storage (the game library's way), probe_exe = an executable shipped
# as lib*.so in nativeLibraryDir (the on-device compiler's way).
add_library(probe_dl SHARED smoke/probe_dl.c)
target_link_options(probe_dl PRIVATE "-Wl,-z,max-page-size=16384")
add_executable(probe_exe smoke/probe_exe.c)
set_target_properties(probe_exe PROPERTIES OUTPUT_NAME "libprobe_exe" SUFFIX ".so"
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
target_link_options(probe_exe PRIVATE "-Wl,-z,max-page-size=16384")
add_dependencies(main probe_dl probe_exe)
# libvulkan_port.so: the game's Vulkan library - the system driver, or Turnip through libadrenotools on Adreno GPUs
# without Vulkan 1.3 (vkshim/vulkan_port.c). libadrenotools (BSD-2, THIRD_PARTY_NOTICES.txt) from its source tree; its
# hook libraries (main_hook, hook_impl, file_redirect_hook, gsl_alloc_hook) land in nativeLibraryDir with it.
if(NOT ADRENOTOOLS_SRC)
    message(FATAL_ERROR "-DADRENOTOOLS_SRC=<libadrenotools source> missing (-PadrenotoolsSrc)")
endif()
add_subdirectory(${ADRENOTOOLS_SRC} adrenotools EXCLUDE_FROM_ALL)
enable_language(ASM)
enable_language(CXX)
add_library(vulkan_port SHARED vkshim/vulkan_port.c vkshim/vk_trampolines.S)
set_target_properties(vulkan_port PROPERTIES LINKER_LANGUAGE CXX)   # libadrenotools is C++ (static libc++)
target_link_libraries(vulkan_port PRIVATE adrenotools linkernsbypass log dl)
target_include_directories(vulkan_port PRIVATE ${ADRENOTOOLS_SRC}/lib/linkernsbypass)
# --exclude-libs: linkernsbypass defines function-pointer VARIABLES named like the linker's own functions
# (android_get_exported_namespace ...); exported from here, libvndksupport bound its call to that data and jumped
# into it (Tab S8 2026-11-11: SEGV "trying to execute non-executable memory"). Only vk* + xr_vk_driver leave.
target_link_options(vulkan_port PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,-soname,libvulkan_port.so"
                    "-Wl,--exclude-libs,ALL")
add_dependencies(main vulkan_port main_hook hook_impl file_redirect_hook gsl_alloc_hook)
add_executable(vkshim_test vkshim/vkshim_test.c)     # dev aid: which driver the shim picks, outside the game
set_target_properties(vkshim_test PROPERTIES OUTPUT_NAME "libvkshim_test_exe" SUFFIX ".so"
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
target_link_libraries(vkshim_test PRIVATE dl)
target_link_options(vkshim_test PRIVATE "-Wl,-z,max-page-size=16384")
add_dependencies(main vkshim_test)

# the linker clang calls on the device: exec()s the toolkit's lld as "ld.lld" (loader/ld_exe.c)
add_executable(ld_exe loader/ld_exe.c)
set_target_properties(ld_exe PROPERTIES OUTPUT_NAME "libld_exe" SUFFIX ".so"
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
target_link_options(ld_exe PRIVATE "-Wl,-z,max-page-size=16384")
add_dependencies(main ld_exe)

# No toolkit runtime in libmain.so: it is linked into the game library on the device (PORT_GAME_LIB, dlopen'ed by
# loader.c) - a second copy here would compete with it.
target_compile_options(main PRIVATE -Wall -Wextra)
target_link_options(main PRIVATE "-Wl,-z,max-page-size=16384")
