# Licence review: sound emulation (APU, DSP56k, AC97) and OHCI (2026-11-17)

Maintainer's order 2026-11-17: which files of APU, AC97 and OHCI come from xemu, QEMU or other outside sources,
under which licence, and which duties follow for public releases (Orta, later THPS) - a short list with options,
**before the next Orta release**. Nothing is published or relicensed without his word. Sources: each file's
header, the toolkit's NOTICE, `git log --follow` per file. Not legal advice - the facts and the options.

## Per file

| File | Origin | Licence |
|---|---|---|
| src/apu/apu_core.c, apu_vp.c, apu_dsp.c | xemu (hw/xbox/mcpx), extracted; (c) 2012 espes, 2018-2019 Jannik Vogel, 2019-2025 Matt Borgerson | **LGPL-2.1-or-later** |
| src/apu/apu_regs.h, apu_state.h, apu_debug.h, fpconv.h, xbox_adpcm.h, apu.h | xemu, same authors | **LGPL-2.1-or-later** |
| src/apu/apu_shim.h | this project's own (hosts the xemu code) | LGPL-2.1-or-later by choice (NOTICE) |
| src/apu/qemu_shim.h | this project's own (QEMU type stand-ins, written for the NV2A extraction, now only the APU's) | MIT (toolkit) - **to confirm**: the header names no licence and no copied QEMU code; a line-by-line check against QEMU's headers is open |
| src/apu/apu_dsp56k.c/.h, apu_gp.c/.h | Takedawn's xboxrecomp fork, written from Motorola's DSP56300 manual + the title's image ("No emulator's code") | MIT ((c) 2026 sp00nz) |
| src/apu/apu_resample.h | Takedawn's fork e34e52e | MIT |
| src/apu/apu_lowpass.h | own (Chamberlin's filter, replaced xemu's GPLv2 svf.h) | MIT |
| src/apu/apu_aci.c/.h (AC97 bus master) | own (written for this toolkit) | MIT |
| src/apu/apu_xaudio2.c/.h, apu_mmio_hook.c | own / upstream sp00nz | MIT |
| src/usb/ohci.c/.h, usb_gamepad.c/.h (OHCI, USB pad) | own / upstream (sp00nz, fearkov, BearddOddity), written from the OHCI 1.0a and USB 2.0 specs | MIT |
| (outside the asked scope, for completeness) src/nv2a_gl/gl_vsh_ff.c | xemu glsl/vsh-ff.c etc. | LGPL-2.1-or-later |

So: **the LGPL part is the MCPX APU core** (core, voice processor, DSP glue, register/state headers); the DSP56k
interpreter, the GP model, AC97 and the whole OHCI / USB pad side are MIT. No GPL-only file is in the tree (xemu's
GPLv2 svf.h and DSP headers were removed earlier, NOTICE "Derived algorithms").

## How a release uses them

- Windows / Linux: the LGPL files are compiled into `libxbox_apu.a`; the release payload ships that library
  prebuilt (no APU sources), and the **player's machine links ORTA.exe** from the shipped objects (launcher).
- Android: the same library linked into the APK's `libmain.so` (L's build).
- Orta itself is GPL-3.0-or-later; LGPL-2.1-or-later code may be combined with it (via the "or later" clause).

## Duties for a public release (LGPL-2.1 section 6, statically linked)

1. **Notice + licence text** shipped: done today - `licenses\LICENSE-xemu-parts-LGPL-2.1.txt`,
   `NOTICE-toolkit.txt`, `licenses\README.txt` (Windows, AppImage); L's APK carries the same 18 texts.
2. **Corresponding source of the library**, accompanying the release or "access from the same place": done
   today by publishing the toolkit snapshot with a tag per release (xboxrecomp-option-c v1.1.0 / v1.1.1 / v1.1.2)
   and linking it from the release notes. Weak point: it is a different repository than the release download.
3. **Relinking with a modified library** must be possible: met by design - the payload holds every object the
   game exe is linked from, the player's machine does the link (replace `tools\sdk\lib\libxbox_apu.a`, run
   `--repair`). Android: not met in the same way (the APK is linked by L; a user cannot relink inside it without
   the objects) - the public source + build script is the usual answer.
4. **No restriction** of modification / reverse engineering for debugging the library: nothing in our texts
   restricts it.

## Options (his decision)

| | What | Effort | Effect |
|---|---|---|---|
| **A** | Keep today's way: licence texts in the release + public toolkit tag per release, linked in the notes | none | duties 1-4 met as today; relies on the tag being published with every release (Orta, THPS) |
| **B** | A + ship the LGPL APU sources inside every release (`licenses\source\apu\`, ~250 KB) | small (release scripts) | duty 2 met by the release itself, independent of the other repository (section 6a) |
| **C** | B + the encapsulation he ordered: the APU (and OHCI) as their own module, on Windows / Linux as a shared library (`xbox_apu.dll` / `.so`, model conker-live-recompiled `xemu_apu.dll`), with a THIRD_PARTY_NOTICES file | the encapsulation task | the LGPL code is a separable, replaceable part (section 6b) on PC; Android keeps it static or ships the `.so` in the APK |

Recommendation: **B now** (before the next Orta release; no change of any licence), **C with the
encapsulation**. Open for him: A, B or C; and whether qemu_shim.h should get an explicit MIT header after the
line check.
