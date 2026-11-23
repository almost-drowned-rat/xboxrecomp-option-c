# MCPX APU Emulation Library
# Extracted from xemu (https://github.com/xemu-project/xemu)
#
# Voice Processor (VP): ADPCM/PCM decode, sample mixing, envelopes, filters
# Global Processor (GP): a title's effects image, modelled (apu_gp.c on the DSP56300 interpreter apu_dsp56k.c)
# Encode Processor (EP): direct mixbin passthrough
#
# XBOXRECOMP_APU_SHARED (default ON; maintainer 2026-11-17, licence option C): the emulation is its own shared
# library, the APU module - xbox_apu.dll (Windows) / libxbox_apu.so (Linux, Android) - which the exe loads at the
# port's APU start-up (apu_host.c; interface apu_module.h). A user can build it from its source and put it in place
# of the shipped one. The target xbox_apu is then the exe's side: apu_host.c (the public API, forwarded) and the
# host audio output apu_xaudio2.c. Every executable of the port's top directory that links xboxrecomp / xbox_apu
# gets the module copied next to it after its build.
# OFF: everything static in xbox_apu, as before (the unit tests under tests/ build this way: they reach into the
# emulation's internals).

option(XBOXRECOMP_APU_SHARED "Build the APU emulation as a shared library (the APU module)" ON)

# The emulation (the module's content; static: part of xbox_apu)
set(XR_APU_EMULATION
    apu_core.c
    apu_aci.c
    apu_vp.c
    apu_dsp.c
    apu_gp.c        # a title's GP effects image, modelled (Takedawn, MIT)
    apu_dsp56k.c    # the DSP56300 interpreter its reverb runs on (Takedawn, MIT)
    apu_mmio_hook.c
)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/HotPathLogging.cmake)
get_filename_component(XR_APU_SRC "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

function(xr_apu_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W3
            /wd4244  # conversion warnings (float/double/int)
            /wd4146  # unary minus on unsigned
            /wd4267  # size_t to int conversion
        )
        target_compile_definitions(${target} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _USE_MATH_DEFINES
        )
    endif()
endfunction()

if(XBOXRECOMP_APU_SHARED)
    # ── the module ────────────────────────────────────────────────────────────────────────────────────────
    add_library(xbox_apu_module SHARED ${XR_APU_EMULATION}
        apu_module.c
    )
    set_target_properties(xbox_apu_module PROPERTIES
        OUTPUT_NAME xbox_apu
        C_VISIBILITY_PRESET hidden          # exports: xr_apu_module_open only
        POSITION_INDEPENDENT_CODE ON)
    if(WIN32)
        set_target_properties(xbox_apu_module PROPERTIES PREFIX "")    # xbox_apu.dll, also under MinGW
    endif()
    target_include_directories(xbox_apu_module PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} ${XR_APU_SRC} ${XR_APU_SRC}/platform ${XR_APU_SRC}/kernel)
    # the hot-path logging header, without the logger library: the module forwards xbox_log_* to the exe
    if(MSVC)
        target_compile_options(xbox_apu_module PRIVATE "/FI${XBOXRECOMP_LOG_DIR}/log_async.h")
    else()
        target_compile_options(xbox_apu_module PRIVATE -include "${XBOXRECOMP_LOG_DIR}/log_async.h")
    endif()
    target_compile_definitions(xbox_apu_module PRIVATE XR_APU_MODULE=1)
    if(NOT WIN32)
        target_compile_definitions(xbox_apu_module PRIVATE _GNU_SOURCE)
    endif()
    # the module's build in its open line: the toolkit commit (platform's generated header)
    if(TARGET xr_build_info)
        add_dependencies(xbox_apu_module xr_build_info)
        target_include_directories(xbox_apu_module PRIVATE "$<TARGET_PROPERTY:platform,BINARY_DIR>/generated")
        target_compile_definitions(xbox_apu_module PRIVATE XR_APU_HAVE_BUILD_INFO=1)
    endif()
    xr_apu_compile_options(xbox_apu_module)
    if(WIN32)
        target_link_libraries(xbox_apu_module PRIVATE kernel32 winmm)
        if(NOT MSVC)
            # no runtime DLLs of the compiler next to it (libc++ / libunwind / compiler-rt linked in)
            target_link_options(xbox_apu_module PRIVATE -static -Wl,--no-undefined)
        endif()
    else()
        # every symbol resolved inside the module: the toolkit's services come through XrApuHost
        target_link_libraries(xbox_apu_module PRIVATE m)
        if(NOT APPLE)
            target_link_options(xbox_apu_module PRIVATE -Wl,--no-undefined)
        endif()
    endif()

    # ── the exe's side ────────────────────────────────────────────────────────────────────────────────────
    add_library(xbox_apu STATIC         # one source per line: tools/hot_path_io.py reads the list
        apu_host.c
        apu_xaudio2.c
    )
    add_dependencies(xbox_apu xbox_apu_module)
    target_compile_definitions(xbox_apu PUBLIC XBOXRECOMP_APU_SHARED=1)
    if(NOT WIN32)
        target_link_libraries(xbox_apu PRIVATE ${CMAKE_DL_LIBS})
    endif()

    # Copy the module next to every executable of the port's top directory that links the toolkit, after its
    # build (deferred to the end of the top directory: the port adds its executables after the toolkit).
    function(_xr_apu_copy_module)
        get_property(tgts DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(t IN LISTS tgts)
            get_target_property(type ${t} TYPE)
            if(type STREQUAL "EXECUTABLE")
                get_target_property(libs ${t} LINK_LIBRARIES)
                if(libs MATCHES "(^|;)(xboxrecomp|xbox_apu|xbox_dsound|xbox_porthost)(;|$)")
                    add_dependencies(${t} xbox_apu_module)
                    add_custom_command(TARGET ${t} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                $<TARGET_FILE:xbox_apu_module> $<TARGET_FILE_DIR:${t}>
                        VERBATIM)
                endif()
            endif()
        endforeach()
    endfunction()
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _xr_apu_copy_module)
else()
    add_library(xbox_apu STATIC ${XR_APU_EMULATION}
        apu_xaudio2.c
    )
endif()

target_include_directories(xbox_apu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The APU ISR, register traps and the frame thread the guest waits on: console output through the logger thread.
xboxrecomp_hot_path_logging(xbox_apu)

# qemu_shim.h (the QEMU type abstraction) lives here since 2026-11-16 (it was in src/nv2a, removed); the APU's
# include directory is PUBLIC already.

xr_apu_compile_options(xbox_apu)

if(WIN32)
    # xaudio2_8 for XAudio2Create, which apu_xaudio2.c calls directly.
    # It was missing: nothing had ever referenced this library from a linked
    # target, so the archive was built and never pulled in, and the first
    # target to actually call into the APU failed at link with an undefined
    # XAudio2Create rather than at build time here.
    target_link_libraries(xbox_apu PRIVATE platform kernel32 winmm xaudio2_8 ole32)
else()
    # Linux: platform layer (Win32 primitives on POSIX) + libm, and SDL3 for the
    # host audio output (apu_xaudio2.c's SDL3 audio-stream backend). Without
    # SDL3 the xa2_* functions stay inactive stubs (no sound).
    target_link_libraries(xbox_apu PRIVATE platform m)
    if(NOT TARGET SDL3::SDL3)
        find_package(SDL3 CONFIG QUIET)
    endif()
    if(TARGET SDL3::SDL3)
        target_link_libraries(xbox_apu PRIVATE SDL3::SDL3)
        target_compile_definitions(xbox_apu PRIVATE XBOX_APU_SDL3_AUDIO=1)
    endif()
endif()
