# sdk_probe - make_runtime_sdk.ps1's consumer: pulls the toolkit in the way the game library is linked ON THE DEVICE
# (SDL3 = libSDL3.so from the APK, glslang = libxr_glslang.so, the toolkit as static libraries, Vulkan = the system's
# libvulkan.so) and links one stub shared library against xboxrecomp. The script reads the compile and link commands
# CMake generated for it (build.ninja) - the exact include dirs, defines and library order.
#
#   -DXR_TOOLKIT=<toolkit root> -DXR_SDL3_SO=<libSDL3.so> -DXR_SDL3_INCLUDE=<SDL3 prefix>/include
#   -DXR_GLSLANG_SO=<libxr_glslang.so> -DXR_GLSLANG_INCLUDE=<glslang prefix>/include
cmake_minimum_required(VERSION 3.20)
project(xr_sdk_probe C CXX)

foreach(_v XR_TOOLKIT XR_SDL3_SO XR_SDL3_INCLUDE XR_GLSLANG_SO XR_GLSLANG_INCLUDE)
    if(NOT ${_v})
        message(FATAL_ERROR "sdk_probe: -D${_v}=... is required")
    endif()
endforeach()

add_library(SDL3::SDL3 SHARED IMPORTED)
set_target_properties(SDL3::SDL3 PROPERTIES IMPORTED_LOCATION "${XR_SDL3_SO}" IMPORTED_NO_SONAME OFF
    INTERFACE_INCLUDE_DIRECTORIES "${XR_SDL3_INCLUDE}")
add_library(glslang::glslang SHARED IMPORTED)
set_target_properties(glslang::glslang PROPERTIES IMPORTED_LOCATION "${XR_GLSLANG_SO}"
    INTERFACE_INCLUDE_DIRECTORIES "${XR_GLSLANG_INCLUDE}")
add_library(glslang::glslang-default-resource-limits INTERFACE IMPORTED)   # inside libxr_glslang.so

set(XBOXRECOMP_D3D11 OFF CACHE BOOL "" FORCE)
set(XBOXRECOMP_VULKAN ON CACHE BOOL "" FORCE)
add_subdirectory(${XR_TOOLKIT} toolkit)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/sdk_probe.c "/* sdk_probe: the toolkit linked like the game library */\nint xr_sdk_probe(void) { return 0; }\n")
add_library(sdk_probe SHARED ${CMAKE_CURRENT_BINARY_DIR}/sdk_probe.c)
target_link_libraries(sdk_probe PRIVATE xboxrecomp)
