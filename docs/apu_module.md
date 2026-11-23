# The APU and OHCI modules (2026-11-17)

Maintainer's order 2026-11-17: the sound emulation (MCPX APU incl. DSP56k, AC97) and the USB host controllers
(OHCI) stay - fixed exceptions next to the NV2A executor (porting_guide.md M3.5) - and are encapsulated as
modules of their own with a clear interface; with licence option C the APU core becomes a **replaceable shared
library on every platform** (docs/licence_apu_ac97_ohci.md). Model: conker-live-recompiled's `xemu_apu.dll`.

## APU module

| | |
|---|---|
| File | `xbox_apu.dll` (Windows), `libxbox_apu.so` (Linux, Android) |
| Built from | `src/apu`: apu_core.c, apu_vp.c, apu_dsp.c (xemu, LGPL-2.1-or-later), apu_gp.c, apu_dsp56k.c, apu_aci.c (AC97), apu_mmio_hook.c (register trap), apu_module.c (MIT) - CMake target `xbox_apu_module` |
| Interface | `src/apu/apu_module.h`, ABI `XR_APU_ABI` (1); one export, `xr_apu_module_open` |
| Exe side | `src/apu/apu_host.c` + the host audio output `apu_xaudio2.c` (XAudio2 / waveOut / SDL3) - CMake target `xbox_apu` |
| Switch | `XBOXRECOMP_APU_SHARED` (default ON); OFF = everything static as before (the APU unit tests build so) |

How it fits together:

- **Exe -> module** (`XR_APU_EXPORTS`): the public API of `apu.h` and `apu_gp.h`. `apu_host.c` defines the same
  names in the exe and forwards them, so a port's code does not change: `main.c`'s `mcpx_apu_init_standalone` /
  `g_apu_state`, a title's `apu_gp_*` wiring, the host's `apu_hook_handle_mmio`. The register trap goes through
  `handle_mmio` (NULL on a host without the MMIO decoder).
- **Module -> exe** (`XR_APU_IMPORTS`, on POSIX also `XR_APU_IMPORTS_POSIX`): interrupt delivery into the title
  (`recomp_lookup`, IRQL, the one-CPU gate, worker stacks, TIBs), the physical-memory helpers, AC97's NABM page and
  its write hook (`xbox_ac97_set_after_write` - a call, no longer a variable the module would write), the logger
  thread (`xbox_log_*`), the host audio output (`xa2_*`). On POSIX the Win32 thread / sync primitives come from
  the exe's win32_compat too: one object table, and the module's frame thread - which runs the title's interrupt
  routine - gets the signal stack the MMIO trap needs. `apu_module.c` defines all of them inside the module and
  forwards them, so the emulation's sources are the same in both builds. Both directions are X-macro lists in
  apu_module.h: the module table, the host table and both sets of forwarders come from the same list.
- **Guest memory**: `g_xbox_mem_offset` and the image range are copied from the exe at open and again at
  `mcpx_apu_init_standalone`.
- **Loading** (`apu_host.c`, first API call - the port's APU start-up; never from the exception handler):
  `RECOMP_APU_MODULE=<file>`, else the exe's folder (a release: `data/` next to `data/<game>`, where the launcher
  ships it since 2026-11-23 - only the launcher on top; a development build: next to the exe), else the install
  root (`xbox_host_root()`, where older releases put it - the launcher removes that copy now), else (Android) the
  bare name from the APK's lib folder. The log says which: `[APU] module <path> (ABI 1, build <commit>)`.
  None, or another ABI: one line, `mcpx_apu_init_standalone` returns NULL (`MCPX APU: FAILED`), the trap declines.
- **Build**: the module links nothing of the toolkit (`-Wl,--no-undefined`; Windows: `-static`, so no compiler
  runtime DLL; the UCRT is the system's). CMake copies it next to every executable of the port's top directory
  that links the toolkit (deferred call at the end of that directory) - no port change needed.
- **Release**: tools/release/port_release.ps1 / port_release_linux.sh put it into the payload's `runtime/`; the
  launcher copies `tools/runtime/*` next to the game exe (like `data/`: when missing or the release changed,
  `data/shipped_runtime.txt`). A player who builds the module from `licenses/source/` puts it there, or sets
  `RECOMP_APU_MODULE`.
- **Android** (Agent L): the module is `libxbox_apu.so`; the APK must carry it in its lib folder next to
  `libmain.so` (CMake builds it as its own SHARED target). `dlopen("libxbox_apu.so")` finds it there.

## OHCI module

The USB host controllers and the USB pad (`src/usb`: ohci.c, usb_gamepad.c; MIT, written from the OHCI 1.0a /
USB 2.0 specs) were already a library of their own (`xbox_usb`, static). Interface: `src/usb/ohci.h`
(`xbox_OhciInit`, `xbox_OhciOwnsAddress`, `xbox_OhciHandleMmio`, `xbox_OhciReport`); the port hosts now include
it instead of their own extern lines. It stays static: MIT, no licence reason for a shared library, and the trap
is on the pad's hot path.

## Cost

Measured in a timed series: the module's calls are an indirect call per register trap
and per service call of the frame thread.
