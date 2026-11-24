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
| src/apu/qemu_shim.h | upstream toolkit (sp00nz, 7a71ed2 2026-03-10; QEMU type stand-ins, written for the NV2A extraction, now only the APU's) | **MIT (toolkit) - checked 2026-11-17, see below** |
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

### qemu_shim.h: line check against QEMU (2026-11-17)

Compared with xemu's QEMU headers (xemu's `include/qemu/`: queue.h, bitmap.h, bitops.h,
thread / timer / memory / pci declarations). Result: **no QEMU code copied, no GPL / LGPL text**:
- The file keeps QEMU's *names* (QemuMutex, qemu_mutex_lock, QTAILQ_*, set_bit, error_report, g_malloc0, ...) so
  that xemu's APU code compiles - an interface, implemented anew: threading on Win32 CRITICAL_SECTION /
  CONDITION_VARIABLE, timers on QueryPerformanceCounter, memory / PCI / VGA / VMState as empty stubs.
- QTAILQ_*: the classic BSD layout (`tqe_next` / `tqe_prev`), not QEMU's union / `tqe_circ` version; QEMU's
  queue.h is itself BSD-3 (from NetBSD) - either way no copyleft.
- set_bit / clear_bit / test_bit / bitmap_zero: one-line standard idioms, written differently from QEMU's
  LGPL bitops.h / bitmap.h (no BIT_MASK / BIT_WORD, no small_nbits path).
- ctz32, muldiv64, ROUND_UP, container_of, KiB / MiB, ldl_le_p: trivial one-liners on compiler builtins.
- Author: the upstream toolkit (sp00nz, 2026-03-10 / 05-27 / 10-02), under the toolkit's MIT licence.

So it stays MIT; nothing to replace. An explicit MIT header line would only state what holds already - left
for the maintainer (no licence text changed without his word).

## Maintainer's decision 2026-11-17: option C

- From now on **every release carries the LGPL sources** of the APU core (incl. this project's changes) and a
  **THIRD_PARTY_NOTICES** file (every MIT / BSD / LGPL notice) - built into the generic release scripts
  (tools/release/port_release.ps1, port_release_linux.sh).
- For the existing **Orta v1.1.2**, the same sources + notices are prepared as a release attachment, uploaded
  only on the maintainer's word.
- qemu_shim.h checked before the next release: MIT (above).
- With the encapsulation the APU core becomes a **replaceable shared library on every platform** (Windows,
  Linux, Android). **Done (2026-11-17, docs/apu_module.md):** the APU module `xbox_apu.dll` /
  `libxbox_apu.so` holds the whole emulation (the LGPL xemu core and the MIT DSP / GP / AC97 / trap files); the
  exe loads it through `apu_module.h` (one export, ABI-checked), the release ships it in `runtime/` and the
  launcher puts it next to the game exe. A user replaces it there (or sets `RECOMP_APU_MODULE`) with one built
  from `licenses/source/` - LGPL-2.1 section 6(b) ("a suitable shared library mechanism") on Windows, Linux and
  Android, without relinking the game. The exe no longer contains LGPL code from the APU (the static build,
  `XBOXRECOMP_APU_SHARED=OFF`, is kept for the unit tests). `src/nv2a_gl/gl_vsh_ff.c` (xemu, LGPL) stays in the
  renderer: it is linked statically as before and covered by the source bundle and the relinkable objects.
  OHCI stays a static module (MIT).

## How a release uses them

- Windows / Linux: the LGPL files are compiled into `libxbox_apu.a`; the release payload ships that library
  prebuilt (no APU sources), and the **player's machine links ORTA.exe** from the shipped objects (launcher).
- Android: the same library linked into the APK's `libmain.so` (the Android build).
- Orta itself is GPL-3.0-or-later; LGPL-2.1-or-later code may be combined with it (via the "or later" clause).

## Duties for a public release (LGPL-2.1 section 6, statically linked)

1. **Notice + licence text** shipped: done today - `licenses\LICENSE-xemu-parts-LGPL-2.1.txt`,
   `NOTICE-toolkit.txt`, `licenses\README.txt` (Windows, AppImage); the Android APK carries the same 18 texts.
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

The recommendation was B now, C with the encapsulation; the maintainer chose **C** (2026-11-17, above).
