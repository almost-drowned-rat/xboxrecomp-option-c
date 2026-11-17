# xbox_dsound - the old DirectSound mixer's target name, kept as an alias of xbox_apu.
#
# The mixer itself (dsound_device.c: IDirectSound8 on a software mixer, plus a Media Foundation WMA decoder) was
# removed 2026-11-16 (maintainer 2026-11-15, step 4 of the option-c order): no port called it - their DirectSound
# is the title's own lifted XDK code on the emulated MCPX APU (src/apu), and the API-level DirectSound is
# src/xdk/xdk_dsound.c. Ports still name xbox_dsound in their link lists (Orta, the game template), so the name
# stays and pulls in xbox_apu. Last version with the mixer: option-c a0b6fb3.
add_library(xbox_dsound INTERFACE)
target_link_libraries(xbox_dsound INTERFACE xbox_apu)
