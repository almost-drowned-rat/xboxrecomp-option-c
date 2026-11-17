# xbox_porthost - the port executable's host platform layer (xr_host.h): command line and layout folders, the log
# with repeat collapsing, the time limit, restart, atomic writes, dialogs, the crash handler and the MMIO fault
# routing to the APU / OHCI traps. Windows: host_win32.c; Linux + Android: host_linux.c.
# Moved into the toolkit 2026-11-16 from Panzer Dragoon Orta's src/platform (Orta Recompiled; the maintainer's
# order 2026-11-15, step 4). A port links it and calls host_set_port() before host_init().
# xr_vk_check.c: the Vulkan 1.3 device check with its apology message (no Vulkan headers; SDL3 loads the loader).
# xr_settings.c: saves/settings.ini with "<PREFIX>_<KEY>" environment overrides (from Orta's src/settings.c).
if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 CONFIG REQUIRED)
endif()
if(WIN32)
    add_library(xbox_porthost STATIC host_port.c host_win32.c xr_vk_check.c xr_settings.c)
    target_link_libraries(xbox_porthost PUBLIC dbghelp shell32 SDL3::SDL3)
else()
    add_library(xbox_porthost STATIC host_port.c host_linux.c xr_vk_check.c xr_settings.c)
    target_link_libraries(xbox_porthost PUBLIC SDL3::SDL3 ${CMAKE_DL_LIBS})
    if(ANDROID)
        target_link_libraries(xbox_porthost PUBLIC log)
    endif()
endif()
target_include_directories(xbox_porthost PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xbox_porthost PUBLIC xbox_kernel platform)
if(XBOXRECOMP_LLE_APU)
    target_link_libraries(xbox_porthost PUBLIC xbox_apu)
else()
    target_compile_definitions(xbox_porthost PRIVATE XR_PORTHOST_NO_APU=1)
endif()
if(XBOXRECOMP_LLE_USB)
    target_link_libraries(xbox_porthost PUBLIC xbox_usb)
else()
    target_compile_definitions(xbox_porthost PRIVATE XR_PORTHOST_NO_USB=1)
endif()
