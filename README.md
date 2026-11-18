# xboxrecomp-option-c

A static recompilation toolkit for original Xbox games: it translates a title's x86 code (its XBE) into C, and
the runtime around that C - kernel, NV2A graphics on Vulkan, sound, input, storage - turns it into a native
program. No emulator and no interpreter run the game code.

This is my fork of [sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp) (MIT), branch `option-c`. It is
the toolkit of [Orta-Recompiled](https://github.com/almost-drowned-rat/Orta-Recompiled) (Panzer Dragoon Orta, Xbox
PAL), and every commit in this repository is the exact toolkit state of an Orta release.

> **AI disclosure:** AI assistance yes (Claude Code by Anthropic).

## What option-c changes

option-c started from my `vulkan-backend` fork
([xboxrecomp-vulkan](https://github.com/almost-drowned-rat/xboxrecomp-vulkan), the toolkit of
[THPS2X-Recompiled](https://github.com/almost-drowned-rat/THPS2X-Recompiled)) and changes one thing at the root:
**the code model the lifter emits.** Every guest thread has its own register set, passed to each lifted function as
a `__restrict` context pointer, with no blanket `volatile` on guest state - the model of
[TGP482/XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) (MIT). The generated C is code a compiler can
actually optimise. What changed and how each step was checked: [docs/option_c.md](docs/option_c.md).

On top of that it carries the runtime work of the ports built on it, among others:

- **Renderer:** the NV2A pushbuffer executed on Vulkan (`src/nv2a_vk`): register combiners and vertex programs as
  shaders, DXT kept compressed on the GPU, antialiasing, anisotropic filtering, render scale, 4:3 / 16:9 / 16:10 /
  21:9, a shipped pipeline list and an asynchronous shader compile against first-use hitches, frame pacing on the
  display's vblank, an optional slang shader pack.
- **Kernel and timing:** a one-CPU gate (no DPC runs while a guest thread is at DISPATCH_LEVEL, as on the console),
  a QPC-timed vblank, an asynchronous logger, input recording and replay for reproducible test runs.
- **Sound:** the MCPX APU (core from xemu, LGPL-2.1-or-later) plus its own DSP56k model and AC97, played through SDL3.
- **Releases without game data:** a launcher that translates and builds the game on the player's machine from the
  player's own disc image, with a bundled toolchain and nothing downloaded; release scripts for a Windows zip and a
  Linux AppImage (`tools/release`), the Linux glibc sysroot (`cmake/linux-sysroot.cmake`), and the Android toolchain
  parts (`tools/android`).
- **Dependencies:** C, SDL3 and Vulkan.

My priorities for every change, in this order: stability first, then resource efficiency (CPU, RAM, battery,
disk), then portability (Windows, Linux, Android), then compatibility across games and XDK builds, then development
speed. Each runtime change is checked against a frozen reference build of Orta before it lands
([docs/option_c.md](docs/option_c.md), section 1).

## Using it for a port

The toolkit is a library; the game's executable lives in its own project.

- [docs/porting_guide.md](docs/porting_guide.md) - the workflow from a disc image to a released port, with the
  checklists and the toolkit functions a port uses.
- [docs/toolkit.md](docs/toolkit.md) - the rules the toolkit follows (renderer policy and more).
- [docs/storage.md](docs/storage.md) - drives, saves and the folders a port uses.
- [docs/lessons_learned.md](docs/lessons_learned.md) - what went wrong before, so it does not again.
- [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) - the pipeline step by step (parser, disassembler, function
  identification, ABI analysis, lifter), from upstream.

Orta-Recompiled is the worked example: its repository shows a complete port on this toolkit, from the lift
configuration to the RECOMP OPTIONS menu and the release packages.

## Upstream

The pipeline, the kernel layer and much of the runtime come from sp00nz's xboxrecomp and its contributors. The
upstream README, with its pipeline overview, quick start, FAQ and changelog up to v0.13.1, is kept unchanged in
[docs/upstream_README.md](docs/upstream_README.md). The upstream community is the
[sp00nznet recomp Discord](https://discord.gg/CRpzGWZFcu).

## Licence

The toolkit is MIT ([LICENSE](LICENSE)). The MCPX APU core taken from xemu (`src/apu`, see the file headers) is
LGPL-2.1-or-later ([LICENSES/](LICENSES), [docs/licence_apu_ac97_ohci.md](docs/licence_apu_ac97_ohci.md)); a release
of a port carries those sources and their notices. Every third-party part and its licence is listed in
[NOTICE](NOTICE).

## Credits

- [sp00nz](https://github.com/sp00nznet) and the upstream contributors ([CONTRIBUTORS.md](CONTRIBUTORS.md)) - the
  toolkit this fork is built on.
- [TGP482/XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) - the register-context code model.
- The xemu authors (espes, Jannik Vogel, Matt Borgerson) - the MCPX APU core.
- Takedawn's xboxrecomp fork - the DSP56k interpreter, the GP model and the vblank-paced flip.
- The other projects named in [NOTICE](NOTICE).
