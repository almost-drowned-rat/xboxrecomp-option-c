# Xbox Input: host pads (SDL3, every OS), keyboard and pad script

add_library(xbox_input STATIC
    xinput_bind.c
    xinput_device.c
    xinput_script.c
    xinput_record.c
    movie_skip.c
)

target_include_directories(xbox_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# SDL3 on every host (A10: one code path; replaces Windows XInput and SDL2). A consumer that builds SDL3 itself
# (add_subdirectory, e.g. the Android APK's jni) has the target already: then no package lookup.
if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 CONFIG REQUIRED)
endif()
target_link_libraries(xbox_input PUBLIC platform SDL3::SDL3)
# xinput_script.c stamps pad presses with xbox_event_ms (kernel_path.c). Said
# here so a linker that reads static libraries in order (GNU ld) sees
# xbox_kernel after xbox_input.
target_link_libraries(xbox_input PRIVATE xbox_kernel)

if(MSVC)
    target_compile_options(xbox_input PRIVATE /W4 /O2)
else()
    target_compile_options(xbox_input PRIVATE -Wall -Wextra -O2)
endif()
