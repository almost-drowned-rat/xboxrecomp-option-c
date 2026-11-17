# The player's one exe (launcher.h; porting guide M7 "Release"): folders, the installer, the payload appended to
# the exe (or, in an AppImage, the read-only tools folder), the shipped compiler unpacked once, lift / compile /
# link, then the game. Nothing is downloaded. SDL3 only; the zip reader + tinfl from src/shaderpack, the installer
# + its window from src/install, the window icon registry from src/kernel (window_icon.c - the only member it
# pulls from that library), the install root from src/platform (host_paths). A port builds its own launcher exe
# from xr_launcher_main and its XrLauncherConfig (THPS 2X: launcher/main.c).

add_library(xbox_launcher STATIC
    launcher.c
    # sha256.c is built into xbox_kernel (xbox_variant needs it there; linked publicly below)
)
target_include_directories(xbox_launcher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT TARGET SDL3::SDL3)                 # imported targets are directory-scoped (as in src/install)
    find_package(SDL3 CONFIG REQUIRED)
endif()
target_link_libraries(xbox_launcher PUBLIC xbox_install_ui xbox_install xbox_shaderpack xbox_kernel platform SDL3::SDL3)

if(MSVC)
    target_compile_options(xbox_launcher PRIVATE /W4 /O2)
else()
    target_compile_options(xbox_launcher PRIVATE -Wall -Wextra -O2)
endif()

# xbox_launcher_port (launcher_port.h, 2026-11-16, from Orta's launcher): the port launcher's main, setup window
# and progress UI in the look of my other ports' launchers. INTERFACE sources, so they are compiled INTO the port's launcher exe:
# its xr_setup_ui_* then always win over game_setup_ui.c's archive member. The Vulkan check is compiled in too
# (xr_vk_check.c + host_port.c, the port's title) - xbox_porthost itself would pull the APU / USB into the launcher.
# Use: target_link_libraries(<port>_launcher PRIVATE xbox_launcher_port) and a main calling xr_launcher_port_main.
set(_xr_porthost_dir ${CMAKE_CURRENT_SOURCE_DIR}/../porthost)   # not CMAKE_SOURCE_DIR: that is the port's root
add_library(xbox_launcher_port INTERFACE)
if(WIN32)
    target_sources(xbox_launcher_port INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/launcher_port.c
                   ${CMAKE_CURRENT_SOURCE_DIR}/port_setup_win32.c ${_xr_porthost_dir}/xr_vk_check.c
                   ${_xr_porthost_dir}/host_port.c)
    target_link_libraries(xbox_launcher_port INTERFACE comdlg32 shell32 ole32 gdi32 user32)
else()
    target_sources(xbox_launcher_port INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/launcher_port.c
                   ${CMAKE_CURRENT_SOURCE_DIR}/port_setup_ui_sdl.c ${_xr_porthost_dir}/xr_vk_check.c
                   ${_xr_porthost_dir}/host_port.c)
    target_link_libraries(xbox_launcher_port INTERFACE ${CMAKE_DL_LIBS})
endif()
target_include_directories(xbox_launcher_port INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${_xr_porthost_dir})
target_link_libraries(xbox_launcher_port INTERFACE xbox_launcher)
