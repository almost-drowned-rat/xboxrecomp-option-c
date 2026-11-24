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
# The launcher exe carries no debug sections (maintainer 2026-11-24, antivirus heuristics: a plain, stripped exe
# with a VERSIONINFO + manifest - templates/game/assets/game.rc). INTERFACE: it reaches every exe linking the launcher.
if(MINGW)
    target_link_options(xbox_launcher INTERFACE -s)
endif()

# xbox_launcher_port (launcher_port.h, 2026-11-16, from Orta's launcher): the port launcher's main, setup window
# and progress UI in the look of my other ports' launchers. INTERFACE sources, so they are compiled INTO the port's launcher exe:
# its xr_setup_ui_* then always win over game_setup_ui.c's archive member. The Vulkan check is compiled in too
# (xr_vk_check.c + host_port.c, the port's title) - xbox_porthost itself would pull the APU / USB into the launcher.
# Use: target_link_libraries(<port>_launcher PRIVATE xbox_launcher_port) and a main calling xr_launcher_port_main.
set(_xr_porthost_dir ${CMAKE_CURRENT_SOURCE_DIR}/../porthost)   # not CMAKE_SOURCE_DIR: that is the port's root
add_library(xbox_launcher_port INTERFACE)
if(WIN32)
    # Windows (the maintainer 2026-11-23: every project in Option P's designs): option-p's launcher window
    # (port_setup_ui_win95.c - Windows 95's look, plain GDI, the first page and the build's progress in one window)
    target_sources(xbox_launcher_port INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/launcher_port.c
                   ${CMAKE_CURRENT_SOURCE_DIR}/port_setup_ui_win95.c ${_xr_porthost_dir}/xr_vk_check.c
                   ${_xr_porthost_dir}/host_port.c)
    target_link_libraries(xbox_launcher_port INTERFACE comdlg32 shell32 ole32 gdi32 user32)
elseif(ANDROID)
    target_sources(xbox_launcher_port INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/launcher_port.c
                   ${CMAKE_CURRENT_SOURCE_DIR}/port_setup_ui_sdl.c ${_xr_porthost_dir}/xr_vk_check.c
                   ${_xr_porthost_dir}/host_port.c)
    target_link_libraries(xbox_launcher_port INTERFACE ${CMAKE_DL_LIBS})
else()
    # Linux (the maintainer 2026-11-23): option-p's launcher window (port_setup_ui_win2000_sdl.c - Windows 2000's look,
    # the first page with the disc image and the install folder) with its font Liberation Sans Regular + Bold
    # (third_party/liberation, SIL OFL 1.1; see NOTICE) built in as bytes (made here at configure time) and rasterised
    # by stb_truetype (src/nv2a_vk/third_party/stb, MIT / public domain; header only).
    set(_xr_launcher_gen ${CMAKE_CURRENT_BINARY_DIR}/launcher_gen)
    file(MAKE_DIRECTORY ${_xr_launcher_gen})
    foreach(_face Regular Bold)
        string(TOLOWER ${_face} _face_lower)
        set(_font_file ${CMAKE_CURRENT_SOURCE_DIR}/third_party/liberation/LiberationSans-${_face}.ttf)
        set(_font_inc ${_xr_launcher_gen}/liberation_sans_${_face_lower}.inc)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_font_file})
        if(NOT EXISTS ${_font_inc} OR ${_font_file} IS_NEWER_THAN ${_font_inc})
            file(READ ${_font_file} _font_hex HEX)
            string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _font_bytes "${_font_hex}")
            file(WRITE ${_font_inc} "/* generated from src/launcher/third_party/liberation/LiberationSans-${_face}.ttf */\n${_font_bytes}\n")
        endif()
    endforeach()
    target_sources(xbox_launcher_port INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/launcher_port.c
                   ${CMAKE_CURRENT_SOURCE_DIR}/port_setup_ui_win2000_sdl.c ${_xr_porthost_dir}/xr_vk_check.c
                   ${_xr_porthost_dir}/host_port.c)
    target_include_directories(xbox_launcher_port INTERFACE ${_xr_launcher_gen}
                               ${CMAKE_CURRENT_SOURCE_DIR}/../nv2a_vk/third_party/stb)
    target_link_libraries(xbox_launcher_port INTERFACE m ${CMAKE_DL_LIBS})
endif()
target_include_directories(xbox_launcher_port INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${_xr_porthost_dir})
target_link_libraries(xbox_launcher_port INTERFACE xbox_launcher)
