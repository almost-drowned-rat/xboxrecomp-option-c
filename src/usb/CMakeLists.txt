# USB: MCPX OHCI host controllers, so a title's own XAPI can find a pad

add_library(xbox_usb STATIC
    ohci.c
    usb_gamepad.c
)

target_include_directories(xbox_usb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xbox_usb PUBLIC platform xbox_input)

# The OHCI ISR and register traps: console output through the logger thread.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/HotPathLogging.cmake)
xboxrecomp_hot_path_logging(xbox_usb)

if(MSVC)
    target_compile_options(xbox_usb PRIVATE /W4 /O2)
else()
    target_compile_options(xbox_usb PRIVATE -Wall -Wextra -O2)
endif()
