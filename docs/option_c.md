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
- **AC97 refill needs the EP DSP** - only for digital / 5.1 output, not for stereo, which the VP mix carries.
- **Menu CPU load:** Panzer Dragoon Orta's 2D menus cost slightly more CPU on option-c than the other Orta
  recompilation does in some menu phases (gameplay and movies are clearly lower) - to be investigated.
