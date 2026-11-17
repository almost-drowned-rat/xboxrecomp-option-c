/**
 * xboxrecomp.h - Xbox Static Recompilation Runtime (umbrella header)
 *
 * Ports normally include the module headers they use (templates/game/src/platform/host_win32.c does); this header
 * pulls in the common ones at once.
 *
 * Libraries (CMakeLists.txt):
 *   xbox_kernel   - Xbox kernel HLE (memory, file I/O, threading, sync, crypto), memory layout, pushbuffer executor
 *   xbox_nv2a_vk  - the renderer: the executor's Vulkan 1.3 back end (nv2a_vk.h)
 *   xbox_apu      - MCPX APU (LLE, optional; xbox_dsound is an alias of it)
 *   xbox_usb      - MCPX OHCI host controllers (LLE, optional)
 *   xbox_input    - host pads (SDL3), keyboard, pad script
 *   xbox_xdk      - XDK libraries at API level
 *
 * The register model, guest memory macros and the lifter's runtime contract: templates/runtime/recomp_types.h
 * (functions are void sub_X(X86Ctx *ctx, uint8_t *base); docs/porting_guide.md). The game provides
 * recomp_lookup / recomp_lookup_manual (the lifter's dispatch tables plus its manual overrides).
 *
 * 2026-11-16: the D3D8 -> D3D11 layer (d3d8_xbox.h), the old DirectSound mixer (dsound_xbox.h) and the D3D11 NV2A
 * (nv2a_state.h, nv2a_regs.h) were removed; qemu_shim.h moved to src/apu.
 */

#ifndef XBOXRECOMP_H
#define XBOXRECOMP_H

#include "kernel.h"             /* Xbox kernel types and functions */
#include "xbox_memory_layout.h" /* memory layout API */
#include "xinput_xbox.h"        /* XInput compatibility */
#include "apu.h"                /* MCPX APU public API */

#endif /* XBOXRECOMP_H */
