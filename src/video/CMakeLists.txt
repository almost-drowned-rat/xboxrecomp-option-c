# Video helpers: the framebuffer window (fb_present.c, plain GDI) the pushbuffer executor can use for bring-up,
# guest surface dumps, and the movie player's entry points as stubs (video_stub.c).
# The Media Foundation player (video_player.c / video_pump.c: host .wmv decode onto a D3D8 texture,
# RECOMP_FMV_HOST) was removed 2026-11-16 with the D3D11 layer it drew into (last version: option-c a0b6fb3).
add_library(xbox_video STATIC
    video_stub.c
    fb_present.c
    guest_surface_dump.c
)
target_include_directories(xbox_video PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
# fb_present.c reads guest memory through xbox_GetMemoryOffset (see
# src/kernel/CMakeLists.txt for the other direction).
target_link_libraries(xbox_video PRIVATE xbox_kernel)
