cmake_minimum_required(VERSION 3.20)
# Version of the option-c toolkit line: the public toolkit (xboxrecomp-option-c) was tagged v1.1.0..v1.1.2 with the
# Orta releases; 1.2.0 = the state after the maintainer's 2026-11-15 order (the v0.x tags in this repo are upstream's).
project(xboxrecomp VERSION 1.2.0 LANGUAGES C)

# Xbox Static Recompilation Runtime Library
# Provides link-time libraries for statically recompiled Xbox games.
#
# Libraries:
#   xbox_kernel    - Xbox kernel HLE (imports -> host), memory layout, pushbuffer executor
#   xbox_nv2a_vk   - the renderer: the pushbuffer executor's Vulkan 1.3 back end (XBOXRECOMP_VULKAN)
#   xbox_apu       - MCPX APU (voice processor, DSP; LLE, optional) - xbox_dsound is an alias of it
#   xbox_usb       - MCPX OHCI host controllers (LLE, optional)
#   xbox_input     - host pads (SDL3), keyboard, pad script
#   xbox_xdk       - XDK libraries at API level (XInput, DirectSound)
# The legacy D3D11 parts (xbox_d3d8, xbox_nv2a's D3D11 PGRAPH, the Media Foundation movie player) and the old
# DirectSound mixer were removed 2026-11-16 (maintainer 2026-11-15: nv2a_vk is the one renderer of every Xbox port;
# removed after the first passed new G3: Orta's regression + timing). History: option-c before that commit.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Common definitions for all targets
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Option to build examples

# ── Runtime Libraries ─────────────────────────────────────────

# Platform: Win32/NT type vocabulary + POSIX implementation of generic
# Win32 host primitives. Carries no Xbox semantics; provides the foundation
# the kernel HLE and the graphics/audio/input layers build on.
add_subdirectory(src/platform)

# Core: Xbox kernel replacement (memory layout, file I/O, threading, sync)
add_subdirectory(src/kernel)

# XBOXRECOMP_D3D11 is gone (2026-11-16, the D3D11 libraries were removed); a port that still sets it changes nothing.
if(XBOXRECOMP_D3D11)
    message(WARNING "XBOXRECOMP_D3D11=ON: the D3D11 libraries were removed 2026-11-16; nv2a_vk is the renderer")
endif()

# LLE paths, compile-time optional (design principle A10): a title whose XDK
# libraries run at API level (src/xdk) builds without them.
option(XBOXRECOMP_LLE_APU "Build the MCPX APU emulation" ON)
option(XBOXRECOMP_LLE_USB "Build the OHCI/USB host controller emulation" ON)

# The indirect-call feedback (src/kernel/icall_feedback.c: every indirect-branch target recorded for the seeds loop)
# costs a store per indirect call in the generated code - a development aid, never in a release (maintainer
# 2026-11-16). XBOXRECOMP_ICALL_FEEDBACK=ON turns it on for the toolkit and every target linking xbox_kernel (the
# port's generated code included). A port that still defines RECOMP_ICALL_FEEDBACK itself in a Release build gets a
# warning here (Orta did until its next pin).
option(XBOXRECOMP_ICALL_FEEDBACK "Record indirect-call targets (development builds only, never a release)" OFF)
get_directory_property(_xr_defs COMPILE_DEFINITIONS)
if("RECOMP_ICALL_FEEDBACK" IN_LIST _xr_defs AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    message(WARNING "RECOMP_ICALL_FEEDBACK is defined in a ${CMAKE_BUILD_TYPE} build: a development aid (a store per "
                    "indirect call) - use XBOXRECOMP_ICALL_FEEDBACK=ON in development builds only")
endif()

if(XBOXRECOMP_LLE_APU)
    # Audio: MCPX APU hardware emulation (voice processor, DSP)
    add_subdirectory(src/apu)
    # xbox_dsound: the old DirectSound mixer's target name, now an alias of xbox_apu (ports link it by name)
    add_subdirectory(src/audio)
endif()

# USB: MCPX OHCI host controllers. A title's XAPI is statically linked and
# drives these registers directly, so a pad only exists if they do.
if(XBOXRECOMP_LLE_USB)
    add_subdirectory(src/usb)
endif()

# Input: host pads (SDL3), keyboard, pad script
add_subdirectory(src/input)

# The port executable's host layer (xr_host.h: process, layout, log, crash handler, MMIO trap routing)
add_subdirectory(src/porthost)

# The user's media folder: custom soundtracks (scan, playlists, decoders)
add_subdirectory(src/media)

# XDK library HLE layer (XInput at API level; DirectSound next)
add_subdirectory(src/xdk)

# Video helpers: framebuffer present hooks, guest surface dumps, the movie-player stubs
add_subdirectory(src/video)

# Vulkan renderer for the pushbuffer executor (nv2a_backend.h), from
# burnout3-sw. Needs glslang and SDL3 as CMake packages (CMAKE_PREFIX_PATH)
# and the Vulkan headers/loader; a game project registers it with
# nv2a_vk_install() and runs with RECOMP_PB_EXEC=1.
option(XBOXRECOMP_VULKAN "Build the Vulkan renderer (nv2a_vk)" OFF)
if(XBOXRECOMP_VULKAN)
    add_subdirectory(src/nv2a_vk)
    # The shader module's packs (media/shaders zips -> data/shaders, preset index); SDL3 like nv2a_vk.
    add_subdirectory(src/shaderpack)
    # The optional boot animation (media/boot.mpg, pl_mpeg); SDL3 like nv2a_vk.
    add_subdirectory(src/bootvideo)
endif()
# The port installer (game_setup.h); its SDL3 window (xbox_install_ui) when SDL3 is there (the Vulkan build).
add_subdirectory(src/install)
# The player's one exe (release; launcher.h): needs the installer window and the zip reader (Vulkan build).
if(XBOXRECOMP_VULKAN AND TARGET xbox_install_ui)
    add_subdirectory(src/launcher)
endif()

# ── Umbrella target ───────────────────────────────────────────

# Convenience target that links all runtime libraries
add_library(xboxrecomp INTERFACE)
target_link_libraries(xboxrecomp INTERFACE
    xbox_kernel
    xbox_input
    xbox_xdk
    xbox_video
)
if(XBOXRECOMP_LLE_APU)
    target_link_libraries(xboxrecomp INTERFACE xbox_apu)
endif()
if(XBOXRECOMP_LLE_USB)
    target_link_libraries(xboxrecomp INTERFACE xbox_usb)
endif()
if(XBOXRECOMP_VULKAN)
    target_link_libraries(xboxrecomp INTERFACE xbox_nv2a_vk)
endif()

