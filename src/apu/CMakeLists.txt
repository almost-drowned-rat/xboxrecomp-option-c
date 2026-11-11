# MCPX APU Emulation Library
# Extracted from xemu (https://github.com/xemu-project/xemu)
#
# Voice Processor (VP): ADPCM/PCM decode, sample mixing, envelopes, filters
# Global Processor (GP): Stub - effects bypass
# Encode Processor (EP): Stub - direct mixbin passthrough

add_library(xbox_apu STATIC
    apu_core.c
    apu_aci.c
    apu_vp.c
    apu_dsp.c
    apu_gp.c        # a title's GP effects image, modelled (Takedawn, MIT)
    apu_dsp56k.c    # the DSP56300 interpreter its reverb runs on (Takedawn, MIT)
    apu_mmio_hook.c
    apu_xaudio2.c
)

target_include_directories(xbox_apu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The APU ISR, register traps and the frame thread the guest waits on: console output through the logger thread.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/HotPathLogging.cmake)
xboxrecomp_hot_path_logging(xbox_apu)

# qemu_shim.h lives in nv2a/ - shared QEMU type abstraction
target_include_directories(xbox_apu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../nv2a
)

if(MSVC)
    target_compile_options(xbox_apu PRIVATE
        /W3
        /wd4244  # conversion warnings (float/double/int)
        /wd4146  # unary minus on unsigned
        /wd4267  # size_t to int conversion
    )
    target_compile_definitions(xbox_apu PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _USE_MATH_DEFINES
    )
endif()

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
