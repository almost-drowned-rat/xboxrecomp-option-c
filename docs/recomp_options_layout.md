# RECOMP OPTIONS - the settings layout (option-c, state 2026-11-23)

The maintainer's layout for the port's settings menu, gathered in one place for every agent that builds or changes a
port's RECOMP OPTIONS (A: THPS 2X, O: Panzer Dragoon Orta, every new recomp). Sources: his layout of 2026-11-21 and his
decisions the same day, his PERFORMANCE / frame pacing order of 2026-11-22 (the same in option-p), his RENDERING
page rule of 2026-11-23 (for all ports; ZOE already has it). The toolkit
calls named here are on option-c.

General rules: every page in the game's own look (porting_guide M6, the port-menus-look-native rule); **no EXIT lines** on
any page (the game's / menu's back button leaves a page); lines a title cannot support are hidden for that port; the
F9 debug menu is separate and internal (dev builds only).

## Who takes what

| Port | Takes |
|---|---|
| **New recomps** on option-c | the complete layout below |
| **Orta** and **THPS 2X 2.0** | they keep their own menus (Orta as now, THPS as in v1.2.8) and take only: (1) the **complete DISPLAY page**, (2) the **UPSCALING lines** (BILINEAR / FSR + FSR PRESET + FSR SHARPNESS) in their post-processing part, (3) the **PERFORMANCE page change** of 2026-11-22 (FRAME RATE ▸ becomes PERFORMANCE ▸, FRAME PACING row removed, FPS COUNTER becomes three switches), (4) the **RENDERING page** of 2026-11-23 (SHADER COMPILE and, if the port has it, SHADER NOTICE move off the post-processing part onto their own RENDERING ▸ page; ANISOTROPIC FILTER stays in post processing) - whenever they next touch the menu; Orta Android follows Orta |

Orta: FIELD OF VIEW is in the pause menu today - O checks whether it moves to DISPLAY or stays in both (ask the
maintainer if unsure). THPS: 16:10 / 21:9 and FOV may be new work; the no-black-bars rule applies.

## The layout

**RECOMP OPTIONS** (top page)
1. DISPLAY ▸
2. POST PROCESSING ▸
3. PERFORMANCE ▸ (was FRAME RATE ▸ until 2026-11-22)
4. RENDERING ▸ (was SHADERS ▸ until 2026-11-23)
5. TEXTURES: OFF / pack name (only when a pack lies in the textures folder)
6. LANGUAGE ▸
7. BUTTON MAPPING ▸

**DISPLAY**
- DISPLAY MODE: WINDOWED / FULLSCREEN / BORDERLESS (hidden on Android)
- RENDER RESOLUTION: NATIVE / common sizes matching the aspect ratio
- ASPECT RATIO: 4:3 / 16:9 / 16:10 / 21:9
- STRETCH CUTSCENES: OFF / ON (`nv2a_vk_set_stretch_43`)
- FIELD OF VIEW: ORIGINAL / +5 ... +30 (per port)

**POST PROCESSING**
- ANTIALIASING: OFF / FXAA / SMAA / MSAA 2X / MSAA 4X (MSAA, not SSAA)
- ANISOTROPIC FILTER: OFF / 2X / 4X / 8X / 16X
- BLOOM: OFF / ON; BLOOM STRENGTH: 1-10
- UPSCALING: BILINEAR / FSR
  - FSR PRESET: QUALITY · BALANCED · PERFORMANCE (only with FSR; all side by side on one line)
  - FSR SHARPNESS: LOW · MEDIUM · HIGH · MAX (only with FSR; all side by side)
  - `nv2a_vk_set_upscaling(fsr, preset, sharpness)` (AMD FSR 1.0); the preset sets the internal resolution, so
    RENDER RESOLUTION is greyed while FSR is on

**PERFORMANCE** (2026-11-22)
- FRAME RATE (first; only where the title allows it - porting_guide M5: ORIGINAL, 60 only if the original is 25 / 30,
  120 only after the time / physics check. THPS has it, Orta not)
- V-SYNC: OFF / ON
- FPS: OFF / ON
- FRAME TIME: OFF / ON
- GAME SPEED: OFF / ON
- FPS / FRAME TIME / GAME SPEED are stored together as `performance_overlay` = `fps`, `frametime`, `speed` joined by
  `+`, or `all` / `off`; unset = the old `fps_counter` mapped (fps -> fps, both -> fps+frametime, frametime ->
  frametime). Toolkit: `nv2a_vk_set_perf_overlay(nv2a_vk_perf_overlay_parse(value))` (bits `NV2A_VK_PERF_FPS` /
  `_FRAMETIME` / `_SPEED`). Overlay lines e.g. `60 FPS`, `16.7 MS`, `SPEED 100 %`. Default all OFF.
- **No FRAME PACING row any more.** The settings file's `frame_pacing` stays (hand-edited only): `timer+paced`,
  `timer+immediate`, `display+paced`, `display+immediate`, `auto` (= unset, the default; old `timer` / `display` still
  read). The port hands it over as `RECOMP_FRAME_PACING` at start and never writes a default into the file. `auto` =
  display+paced (the timer where the display cannot pace) - Orta G3 2026-11-22, see porting_guide "Frame pacing".

**RENDERING** (2026-11-23; replaces the SHADERS page - the maintainer's rule for all ports, so POST PROCESSING stays
short. ANISOTROPIC FILTER stays on POST PROCESSING)
- TEXTURE FILTERING (first; only where the toolkit has it - option-p has it, option-c has no such setting yet, so the
  line is hidden on option-c ports)
- SHADER COMPILE: OFF / SKIP / WAIT / HYBRID (`nv2a_vk_set_async_mode`; HYBRID = value 3, on option-c since 268dbc0;
  default HYBRID on Windows / Linux, WAIT on Android since 04ab677 - store nothing until the player chooses)
- SHADER NOTICE: ON / OFF (`nv2a_vk_set_shader_notice`)

**LANGUAGE**
- LANGUAGE: the game's languages (a port may add game-specific lines itself - subtitles, dub)

**BUTTON MAPPING**
- the device (keyboard / controller), RESET TO DEFAULT, every input assignable on its own

## Toolkit state

On option-c 268dbc0: SHADER COMPILE HYBRID, SHADER NOTICE, stretch, aspect, AA, aniso, bloom, V-Sync, the old FPS
counter, and (2026-11-22) the PERFORMANCE overlay with GAME SPEED + the four frame pacing modes and auto, FSR with
presets / sharpness, F12 screenshots, the internal F9 debug menu. Later: texture packs.
