# src/audio - only the `xbox_dsound` target name

The old DirectSound software mixer (`dsound_device.c`, `dsound_xbox.h`) and its Media Foundation WMA decoder
(`wma_decoder.c`) were removed on 2026-11-16 (maintainer's order 2026-11-15, step 4): no port called them. A port's
DirectSound is either the title's own lifted XDK code on the emulated MCPX APU (`src/apu`, every port so far) or the
API-level DirectSound in `src/xdk/xdk_dsound.c`.

`xbox_dsound` stays as an INTERFACE alias of `xbox_apu`, because ports name it in their link lists. The last
version with the mixer is option-c `a0b6fb3`.
