# option-c - the code-model change and the C gate

**xboxrecomp-option-c** is a branch of my xboxrecomp fork (itself a fork of
[sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp), MIT). It started on 2026-10-16 from the
`vulkan-backend` branch (the base of [THPS2X-Recompiled](https://github.com/almost-drowned-rat/THPS2X-Recompiled))
and changes one thing at the root: **the code model the lifter emits**.

- **Before (vulkan-backend):** the guest's registers are globals, and generated code accesses guest state through
  blanket `volatile`. Correct, but the compiler can keep almost nothing in host registers.
- **option-c:** the lifter emits the code model of [TGP482/XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp)
  (MIT, attribution in NOTICE): every guest thread has its own register set (`X86Ctx`), passed to each lifted function
  as a `__restrict` context pointer; no blanket `volatile`. Guest memory accesses use `may_alias`, unaligned-safe
  element types; spin-waits and lock-free flags in game code keep their ordering through `RECOMP_BACKEDGE()` at
  backward branches. Indirect calls (`RECOMP_ICALL`) dispatch without process-wide shared counters.

The result is generated C that the compiler can actually optimise. On top of that, option-c collects the runtime
work of the ports built on it (Panzer Dragoon Orta; Burnout 3 as the long-running test bench): the one-CPU gate,
the asynchronous logger, gamma ramps, the QPC-timed vblank, input recording/replay and more - see the changelog in
the README.

**Priorities, in this order:** stability is a precondition; then resource efficiency (CPU, RAM, battery, disk) >
portability (Windows, Linux, Android) > compatibility across games and XDK builds > development speed. No settings
overlay; dependencies only C, SDL3 and Vulkan.

Panzer Dragoon Orta ([Orta-Recompiled](https://github.com/almost-drowned-rat/Orta-Recompiled)) is the first released
port on option-c and doubles as its benchmark: its release notes compare CPU, GPU and RAM against another,
independently developed Orta recompilation.

*This work is done with AI assistance (Claude Code by Anthropic).*

---

## 1. Rules for every step

These rules apply to every toolkit change on option-c, not just to the code-model change itself.

- **Regression rule (G3).** A step passes as "unchanged behaviour" when, against a frozen reference base built from
  the same game code:
  1. no milestone (a file open logged by the runtime with an exact time, `RECOMP_EVENT_TS=1`) comes later than the
     reference maximum + 0.5 s;
  2. an earlier milestone is accepted only in a CPU-bound phase - an I/O- or timer-bound one that moves earlier by
     more than the reference spread + 0.5 s is investigated first;
  3. the same file-open order, the same set of untranslated instructions / indirect-call misses, no early exit or
     crash (known non-deterministic opens are listed and not counted);
  4. **frame content matches** where frame dumps exist for the window: dumps are compared by content (head vs.
     base), with masks of at most 10 % of a frame (above 5 % only with a written reason), a separate per-pixel
     tolerance, masks frozen once committed, and at most two attempts to stabilise a flaky comparison. Movies run on
     real time, so a movie window is compared on decoded movie frames taken at the next flip, against the set of all
     variants the base runs produced (`tools/frame_compare.py`, `tools/frame_variants.py`);
  5. CPU % and RAM not above the reference spread + 5 % - an increase is reported, a decrease is an improvement.

  A fail on 1, 3 or 4 stops the step. The reference is never loosened to make a step pass.
- **Timing measurements run alone.** Every timed run (lift time, regression series, micro-benchmarks) runs with
  nothing else loading the machine, on AC power, with the GPU clock logged; a run that overlapped other work is
  repeated, never used.
- **Frame rates above the original** never come from a faster game tick.
- **No blocking file or log I/O in hot paths** (standing rule, every port). Code in a kernel call, a DPC, an ISR or
  anything else that holds the one-CPU gate does no blocking file or log I/O; console output goes through the
  logger thread. Mechanism: `cmake/HotPathLogging.cmake` force-includes `src/kernel/log_async.h` into the kernel,
  USB and APU libraries, so `fprintf`/`printf`/`fputs`/`fwrite`/`fflush` on stderr/stdout there format on the
  calling thread into a bounded ring (2048 slots; a line over 1023 bytes is cut and marked `[...]`) and the logger
  thread (`src/kernel/log_async.c`) writes them; a full ring drops the line and the logger reports the count, nothing
  waits. Other streams (dump files, saves) pass through unchanged. Code that ends the process without `exit()` calls
  `xbox_log_drain()` first; `RECOMP_LOG_SYNC=1` makes every call synchronous again (to see a crash's last line).
  The lint `tools/hot_path_io.py` (pytest `tools/test_hot_path_io.py`, Windows tests and Linux CI) checks that each
  hot library uses the redirect, that no other source uses the gate/ISR/DPC entry points, and that every file open
  in a hot source is listed in `FILE_IO_ALLOWED` with its reason. A port's own hooks that run in those paths follow
  the same rule.
- **Diagnostics may read the register set** (`xbox_diag_ctx`) only read-only, tolerating stale values, and take
  state snapshots only at safe points; `tools/recomp/test_ctx_lint.py` keeps the pointer out of every
  guest-reachable path.

## 2. The one-CPU gate

`src/kernel/kernel_hal.c`, test `tests/kernel_irql_gate`; `RECOMP_DPC_GATE=0` turns it off. The Xbox has one CPU:
an ISR or DPC never runs while a guest thread holds DISPATCH_LEVEL. The recomp runs guest threads on several host
cores, so the gate restores that: a host-delivered ISR or DPC (DPC drain including timer DPCs, GPU vblank/PGRAPH and
device interrupt lines, OHCI ISR, APU ISR) runs only while no guest thread holds DISPATCH_LEVEL, and a guest thread
does not raise while one runs. Nothing is dropped: DPCs stay queued, lines stay up, edge events such as vblank are
latched and delivered once. Guests waiting more than 50 ms, DPCs held back more than 50 ms and DPCs running more
than 20 ms are reported with the holder and call site.

**Deviation from the console:** the gate is taken per DPC, not for a whole drain, so a guest waiting to raise gets
in between two DPCs (holding it for the whole drain kept guests more than 50 ms behind a long DPC). On hardware the
queue drains with no other DISPATCH code in between - if a hang comes back, holding the gate for the whole drain
again is the first thing to try.

Why it exists: in Burnout 3 a USB done-queue DPC ran during a quarter of all DPC runs while a guest thread was
raised; races hung in a USB wait or crashed in the USB driver. With the gate, every comparison run reached the race
with no hang, crash or lost input, and the DPC latency distribution stayed the same.

## 3. The C gate

The code-model change was merged only after all of these held on one commit (passed 2026-10-24):

| # | Check | Pass condition |
|---|---|---|
| **G1** | Conformance against real x86 (`tools/conformance`: cases, corpus, functions from a real XBE), 32-bit MSVC oracle | all green at `-O2` and `-O0`; no new whitelist entries; cases for fences, unaligned access, back-edge spins, the context across call / tail call / indirect call / SEH; targeted x87 cases at precision control 24 bit |
| **G2** | Lifter unit tests (pytest), Windows `ctest`, Linux CI | green |
| **G3** | A real title re-lifted on the new code model | the regression rule in §1, plus a 10-minute run without a crash and frame times not worse than the reference |
| **G4** | Micro-benchmark of real generated code against XboxXBE-Recomp's output, same compiler and flags, pinned core, median of at least 5 runs | ratio ≤ 1.10 on F1, F2, F4, F5; ≤ 1.15 on F3 (x87, 53-bit precision); ≤ 1.25 on F3' (x87, 24-bit precision); indirect calls at 4 threads ≤ 1.2x. A miss stops the work - thresholds are never loosened |
| **G5** | The shipping base (`vulkan-backend`, THPS 2X) is untouched | no commit from this work |

**G4 result on the real generated code** (median of 5 runs; "B" = XboxXBE-Recomp's code model):

| Function (lifted from real titles) | option-c (ns) | B (ns) | ratio | limit |
|---|---|---|---|---|
| F1 byte loop | 23.31 | 23.27 | 1.00 | ≤ 1.10 |
| F2 branches on globals | 6.39 | 6.49 | 0.98 | ≤ 1.10 |
| F3 x87, 53-bit precision | 1,834 | 1,695 | 1.08 | ≤ 1.15 |
| F4 SSE matrix | 23.88 | 25.21 | 0.95 | ≤ 1.10 |
| F5 copy + loops | 73.77 | 73.43 | 1.00 | ≤ 1.10 |
| F3' x87, 24-bit precision | 2,841 | 2,348 | 1.21 | ≤ 1.25 |
| indirect calls, 4 threads | 8.75 | 7.90 | 1.11x | ≤ 1.2x |

A later data point, F6 (SSE guest loads/stores of a 4x4 matrix function), runs at 0.60x of B (informational, no
limit).

Findings that shaped the runtime:

- **Register-set alignment (F5).** The speed of hot guest code depends on where `X86Ctx` sits relative to 32-byte
  boundaries: F5 alternated between ~73 and ~100 ns per call (about 35 % slower when the set sits at an odd
  multiple of 16 bytes). `X86Ctx` is therefore 64-byte aligned by its type (`RECOMP_CTX_ALIGN`), checked in
  `xbox_ctx_init` / `xbox_diag_ctx_set` and by `tools/recomp/test_x86ctx_layout.py`.
- **Indirect-call tracing.** A process-wide `volatile` trace ring written by every indirect call of every guest
  thread made dispatch about 8x slower with four threads (69 vs 8.7 ns per call); release builds now carry no trace
  at all (`templates/runtime/recomp_types.h`).

## 4. Open items

- **ARM64 memory ordering** (before the first ARM64 build): a compiler barrier does not order accesses across
  threads there. Spin-waits and lock-free flags in game code (plain accesses + `RECOMP_BACKEDGE`) need
  acquire/release semantics on ARM64.
- **AC97 refill needs the EP DSP** - only for the console's own digital (AC97) output path, not for stereo, which
  the VP mix carries, and not for the 5.1 output below, which takes the GP's six speaker feeds directly.
- **Menu CPU load:** Panzer Dragoon Orta's 2D menus cost slightly more CPU on option-c than the other Orta
  recompilation does in some menu phases (gameplay and movies are clearly lower) - to be investigated.

## 5. Changes since the v0.6.0-beta snapshot (Orta Recompiled v0.9.0)

Defaults are unchanged for a port that sets none of the switches below, except where a line says otherwise.

- **Frame pacing.** On a steady 120 / 180 / 240 Hz display the guest vblank follows the display's own vblank
  (`src/platform/host_vblank.c`; Windows only, loaded at run time) instead of a 60 Hz timer, so every game frame
  stays on screen the same number of refreshes. Variable refresh and other rates keep the timer;
  `RECOMP_FRAME_PACING=timer` forces it. `RECOMP_PACING_STATS=1` logs refreshes per presented frame.
- **Aspect ratio as a value.** `RECOMP_WIDESCREEN` takes `4:3`, `16:9` (or `1`), `16:10` and `21:9` (= 64:27);
  `xbox_video_aspect()` returns it. Unset, a title told widescreen by its console settings is shown at 16:9 as
  before. `nv2a_vk_set_hud_squash` lets a port press its screen-space HUD draws back to 4:3 width around the left
  edge, centre or right edge (only the renderer's copy of the vertices changes).
- **5.1 output.** `RECOMP_AUDIO_51=1` sends the APU's six GP speaker feeds to a device with 5.1 speakers (stereo
  otherwise, as before); the console settings then report Dolby Digital so the title mixes for six speakers.
  Diagnostics: `RECOMP_APU_SPKSTATS`, `RECOMP_XBOX_AUDIO`.
- **Rumble and late pads.** The pad's output report reaches the host controller (rumble), and the USB controller
  thread no longer stops waiting for a pad after a few seconds (a long boot video left the title without input).
- **Mouse cursor** hidden over the game window after `RECOMP_CURSOR_IDLE_MS` (default 2000, `0` = never).
- **Frame ring size.** `RECOMP_VK_RING_MB=<8..48>` (default 48, unchanged); `RECOMP_VK_RING_STATS=1` logs peak use.
- **Texture modes.** `BUMPENVMAP` / `BUMPENVMAP_LUM` (signed offsets converted per texel, then filtered) and the
  G8B8 format (swizzled 0x28, linear 0x17) are implemented; other bump / dot-product modes are still drawn as plain
  2D and named once in the log (`[PSH] ... first seen`). `RECOMP_PSH_NO_BUMP=1` restores the plain lookup for
  comparisons.
- **Pad scripts** (testing): the whole script file is read (it stopped at 64 KB), repeating steps
  `<at>/<period>-<until>`, and a pause switch - BACK held 1.5 s on the real pad, or `pause` / `resume` / `toggle`
  in `RECOMP_PAD_LIVE` - so a player can take over from a running script.
- **Guest heap.** A freed block at the top of the heap goes back to the free room above it, so one large request
  no longer fails while a free block and that room together would hold it. The out-of-memory line names live
  bytes, free bytes, the largest free block and the top room.
- **Worker thread stacks.** A worker created with `PsCreateSystemThreadEx` gets the stack size it asks for
  (page-rounded, 128 KB to 512 KB; `0` = 512 KB, as before) instead of a fixed 512 KB, and up to 128 workers can be
  alive at once (was 32). A title that kept more than 32 loader workers alive hung, because the next worker ran on
  its caller's thread; at 512 KB each the stacks had also taken 16 MB of a 64 MB title's heap.
- **Diagnostics.** `[hitch]` lines (long frames) also count driver pipeline builds, render-target images made and
  named screenshots; `RECOMP_VK_TEXBIND_LOG=1` logs each distinct depth / render-target / linear texture binding
  once with what it was bound to.

## 6. Changes since the v0.9.0 snapshot (Orta Recompiled v1.0.0)

Defaults are unchanged for a port that sets none of the switches below, except where a line says otherwise.

- **Physical addresses mean one byte.** `MmGetPhysicalAddress` of heap, pool, image or stack memory now answers
  VA + 256 MB (`XBOX_HEAP_PHYS_BIAS`), so it no longer shares a number with the contiguous window. The APU, the
  USB host controller and the pushbuffer executor resolve such numbers first (`xbox_PhysHeapVA`). Before, a
  number could mean either copy and the bus masters guessed through a per-page owner table. Panzer Dragoon Orta's
  sound engine re-created per stage then wrote into a collision mesh (crash in stage 6) and missed its own data.
  `RECOMP_PHYS_BIAS=0` restores the old answer. This changes every option-c title's bus masters: a title that ORs
  0x80000000 onto a heap physical address now faults instead of reading the wrong copy.
- **Handle table is thread-safe.** Its slots change hands by compare-and-swap. Two threads creating handles at
  the same moment could get the same token, and closing one killed the other (Orta's stage-7 cut-scene hang: a
  worker's quit event was gone). A near miss is logged once (`[BRIDGE] two threads reached handle slot ...`). A
  failed `NtResumeThread` logs the token, its slot and the thread it last held.
- **APU.**
  - A trapped front end keeps its decoded method until the service routine resumes it; a title's PIO methods
    no longer overwrite it.
  - Idle voices are reported in turn, only while still listed, so one voice that stays idle no longer starves
    the rest.
  - A voice-list handle past the 256 voices ends the list instead of indexing host memory.
  - `KeDisconnectInterrupt` removes the routine from the delivery table, so a title that re-creates DirectSound
    per stage gets its interrupts on the new object. Before, it got none, and the old object's routine ran on
    freed memory.
  - `RECOMP_APU_DIAG=1` / `RECOMP_PHYS_DIAG=1` log register writes, traps, deliveries, a once-a-second state line,
    where APU writes land, and physical pages that change hands.
- **Threads.**
  - A new thread's TIB starts at PASSIVE_LEVEL; the main TIB's IRQL byte is no longer copied. Workers that read
    it as raised took the CRT's bug check path.
  - The TIB keeps the KPCR area to 0x100 zeroed and has its own `NtTib.Self`.
  - `KeBugCheck` / `KeBugCheckEx` report the call site, stack, thread and IRQL.
- **21:9 is the display's exact ratio** (all ports), not a fixed 64:27.
- **Render surfaces sampled without alpha.** A render surface sampled through a format without alpha
  (R5G6B5 / X1R5G5B5 / X8R8G8B8) reads alpha 1.
- **Diagnostics and testing.**
  - `RECOMP_VK_SURF_LOG=<from>-<to>` logs the render surfaces' life over a flip window.
  - A live-file line `stop` drops the running pad script for good, and later live lines still press.
- **New tests:** `apu_fe_trap`, `phys_resolve`, `pad_live_stop`, `video_aspect`; `kernel_regressions` case 6 is
  the concurrent handle-table check.

## 7. Changes since the v1.0.0 snapshot (Orta Recompiled v1.1.0: Linux and Android)

The toolkit now runs on Linux (x86-64) and Android (arm64) besides Windows. Windows behaviour is unchanged unless a
line says otherwise.

- **POSIX platform layer** (`src/platform/win32_compat.c`).
  - Vectored exception handlers on signals (SIGSEGV / SIGBUS / SIGILL / SIGFPE / SIGTRAP, own signal stack), so the
    MMIO traps (APU, USB host controller, AC97) work as on Windows; x86-64 and arm64.
  - Events release the threads waiting at the moment of a `SetEvent` / `PulseEvent`, even when another thread
    resets the event before they run (generation counter for manual reset, release tokens for auto reset). Before,
    such wakeups were lost: Orta's movies stalled 0.7-3 s on Linux and Android.
  - `SuspendThread` really suspends: the thread stops at its next kernel call, as the console's suspend APC does.
  - File allocation info, directory contexts dropped on close, atomic saves (shadow file + fsync + rename).
  - Android (bionic) branches: no `pthread_cancel`, `memfd_create` through the system call before API 30.
- **arm64.** `mmio_decode_a64.h`: the AArch64 decoder for trapped device pages (loads, stores, pairs, STLR; write
  flag from the ESR record, from the instruction without one). The AC97 bus-master store is emulated on arm64
  (no single step there). Page tracking is off on hosts with pages other than 4 KB.
- **Kernel paths** (`kernel_path.c`) at the Win32 level on POSIX: partition images, `[PATH]` lines, utility drive.
- **Audio:** an SDL3 audio stream behind the XAudio2 interface on non-Windows hosts.
- **Vulkan on Android:** identity pre-transform (the compositor rotates), the surface and swapchain made again after
  the app returns from the background, `SUBOPTIMAL` ignored unless the extent changes.
- **Launcher.** Nothing is downloaded any more: the release carries the compiler (`toolchain.txt` format 2, bundled
  zip on Windows, unpacked read-only tools in an AppImage); `platform/host_paths.h` gives the writable root (the
  folder holding the `.AppImage`, else the exe's folder); "Vulkan driver missing" message; fork/exec on Android.
- **Input: pad modes** (`xinput_xbox.h`). `XBOX_PADS_MERGE` puts every host pad on player 1 (single-player titles,
  a handheld docked with an external pad); `xbox_InputSetPadPriority` ranks pads (Android: external before built-in
  or touch); `xbox_InputSwapPorts` for a menu. `RECOMP_PADS_MERGE=0/1` overrides. Without a call nothing changes,
  on every host.
- **Diagnostics:** a stall report after 10 s without a kernel call (`[HANG]`, every thread's host stack via
  `host_stack_dump.c`), `RECOMP_FLIP_STALL_MS` for single long frames.
- **Release tools:** `tools/linux/make_toolchain_bundle.sh` (clang + lld + Python for an AppImage, glibc 2.35
  sysroot, licence texts), `cmake/linux-sysroot.cmake`, `tools/linux/build_glslang.sh`, `tools/release/prune_llvm_mingw.py`
  (the Windows compiler zip without what the build never uses), `tools/android/` (runtime SDK, on-device clang +
  lld, `libxr_glslang.so`).
- **New tests:** `mmio_trap`, `mmio_decode_a64`, `kernel_file_posix`, `host_paths`, `compat_bionic`,
  `compat_events`, `host_stack_dump`.

## 8. Changes since the v1.1.0 snapshot (Orta Recompiled v1.1.1)

- **MSAA on the scene target** (`src/nv2a_vk/nv2a_vk.c`, `ms_scene`). Before, only the presented surface got the
  multisampled image. Titles that draw their 3D world into a target of their own and copy it onto the presented
  surface got no MSAA on the 3D (Panzer Dragoon Orta). Now the target with the most draws (at least 32, the most-drawn
  one for 30 flips in a row) gets it too; shadow / reflection targets stay single-sampled. `RECOMP_VK_MSAA_SCENE=0`
  = the presented surface only.
- **MSAA after a resize:** a surface made again at a new size gets its multisampled image back at the next flip
  (THPS 2X: its scene target changes its width when a level starts at 16:9 / 16:10). `RECOMP_VK_RT_STATS=<n>` logs
  every n flips the render targets with their sample count and draws.
- **BLOOM** (`nv2a_vk_set_bloom(strength)`, 0 = off, 1..10): a present pass - bright pass on luminance with a soft
  knee at a quarter of the size, a separable 9-tap Gaussian (16-bit float), added onto the frame - after SMAA,
  before the gamma ramp, FXAA and the shader chain. Render targets, readback and screenshots stay untouched.
  `RECOMP_VK_BLOOM=0..10` overrides.
- **Release tools:** `tools/linux/build_slim_llvm.sh` (a slim static x86-64 clang + lld for the AppImage, PGO), used
  by `make_toolchain_bundle.sh --slim-bin`.
