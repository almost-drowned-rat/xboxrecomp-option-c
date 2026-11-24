# RECOMP OPTIONS - the settings layout (option-c, state 2026-11-24)

**The maintainer 2026-11-24: the RECOMP OPTIONS layout of every recomp follows option-p's** - "from now on the
recomp menu layout follows option-p's ... so every recomp has the same recomp menu layout". This page is that layout for option-c ports, taken from option-p's reference: ZOE's in-game RECOMP OPTIONS
(`games/zoe_us/port/overrides.cpp`, PageLines, 2026-11-23) and the engine's rows (option-p `host/src/pc_menu.cpp`,
b42fb84: labels, values, pages). **Only the layout is shared** - each port still builds its menu itself, in its game's
own look (the maintainer 2026-11-24: the layout document, no toolkit menu module). When option-p's layout changes, this
page follows.

## Who takes what

| Port | Takes |
|---|---|
| **Every port** on option-c (new recomps, **Panzer Dragoon Orta**, **THPS 2X 2.0**) | the **complete layout below** - no port keeps an own order any more. A port's **extra lines** go into the page they belong to, after that page's standard lines of the same kind; a port's **own pages** go last on the top page. THPS 2X's placement: section "THPS 2X" below (THPS 2.0.0 waits for it, the maintainer 2026-11-24). Orta places its extras the same way at its next menu change. Android (BACK menu / touch): the same layout in the Android port's menu |

## Rules (as option-p / ZOE)

- Every page in the **game's own look** (font or word images, colours, frame, cursor - the port-menus-look-native
  rule). The engine's plain overlay is for the internal F9 debug menu only (dev builds).
- **No EXIT lines** on any page: the menu's back button leaves a page. A small hint in a corner (bottom right in ZOE)
  names the buttons: select, back, and - where the page has defaults - default (Xbox: **A** SELECT, **B** BACK,
  **Y** DEFAULT; PlayStation: ○ / × / △ as ZOE).
- **DEFAULT** (Y) on a line puts that line back to its default.
- A line with **two values** shows both side by side (as the game's own ON / OFF lines); **FSR PRESET** (three) and
  **FSR SHARPNESS** (four) show all their values side by side too; left / right moves between them. Other lines show
  the chosen value.
- Lines a title or the platform cannot use are **hidden** (not greyed): DISPLAY MODE on Android, ASPECT RATIO /
  STRETCH CUTSCENES / FIELD OF VIEW without a widescreen path, TEXTURES without a pack, LANGUAGE without a second
  language, FSR PRESET / FSR SHARPNESS without FSR.
- Lines that act only after a restart: on leaving the menu a **RESTART NEEDED** page - RESTART NOW / LATER - each time
  the start-only lines differ from the running ones (not once per change).
- The menu is entered from the game's own OPTIONS menu (line **RECOMP OPTIONS**).

## The layout

**RECOMP OPTIONS** (top page, in this order)
1. DISPLAY ▸
2. POST PROCESSING ▸
3. PERFORMANCE ▸
4. RENDERING ▸
5. TEXTURES: OFF / a pack's name (only when a pack lies in the textures folder; option-c has no texture packs yet)
6. LANGUAGE ▸
7. BUTTON MAPPING ▸
8. (a port's own pages)

**DISPLAY**
- DISPLAY MODE: WINDOWED / FULLSCREEN / BORDERLESS (hidden on Android)
- RENDER RESOLUTION: the resolution itself (e.g. 1920X1440), stepped through the sizes for the aspect ratio
- ASPECT RATIO: 4:3 / 16:9 / 16:10 / 21:9 (never stretched in gameplay - the no-black-bars rule)
- STRETCH CUTSCENES: OFF / ON (only with a wide aspect; movies stretched or 4:3; gameplay keeps the chosen shape -
  `nv2a_vk_set_stretch_43`)
- FIELD OF VIEW: ORIGINAL / +5 / +10 / +15 / +20 / +25 / +30

**POST PROCESSING**
- ANTIALIASING: OFF / FXAA / SMAA / MSAA 2X / MSAA 4X (option-c: MSAA, not SSAA)
- ANISOTROPIC FILTER: OFF / 2X / 4X / 8X / 16X
- BLOOM: OFF / ON; BLOOM STRENGTH: 1-10 (where the port uses the toolkit's bloom; a game's own glow has no line)
- UPSCALING: BILINEAR / FSR
  - FSR PRESET: QUALITY · BALANCED · PERFORMANCE (only with FSR)
  - FSR SHARPNESS: LOW · MEDIUM · HIGH · MAX (only with FSR)
  - `nv2a_vk_set_upscaling(fsr, preset, sharpness)` (AMD FSR 1.0); the preset sets the internal resolution, so
    RENDER RESOLUTION is greyed while FSR is on

**PERFORMANCE**
- (FRAME RATE first, only where the title allows a choice - porting_guide M5; THPS has it, Orta not)
- V-SYNC: OFF / ON
- FPS: OFF / ON
- FRAME TIME: OFF / ON (the longest frame of the last half second)
- GAME SPEED: OFF / ON (no averages - porting_guide "GAME SPEED")
- FPS / FRAME TIME / GAME SPEED stored together as `performance_overlay` = `fps`, `frametime`, `speed` joined by `+`,
  or `all` / `off`; toolkit `nv2a_vk_set_perf_overlay(nv2a_vk_perf_overlay_parse(value))`. Default all OFF.
- No FRAME PACING row: the settings file's `frame_pacing` only (`auto` = unset, the default; porting_guide
  "Frame pacing").

**RENDERING**
- TEXTURE FILTERING: ACCURATE / FAST / AUTO (option-p has it; option-c has no such setting yet - hidden)
- SHADER COMPILE: OFF / SKIP / WAIT / HYBRID (`nv2a_vk_set_async_mode`; default HYBRID on Windows / Linux, WAIT on
  Android - store nothing until the player chooses)
- SHADER NOTICE: ON / OFF (`nv2a_vk_set_shader_notice`)

**LANGUAGE** (ZOE's order; a port shows the lines its game has)
- SUBTITLES CUTSCENES: ON / OFF
- SUBTITLES GAMEPLAY: ON / OFF
- VOICES: the dubs the port can play (only with a second one)
- SUBTITLE LANGUAGE: the game's languages (only with more than one)

**BUTTON MAPPING**
- the device (keyboard / controller), RESET TO DEFAULT, every input assignable on its own (Orta's / option-p's model)

## THPS 2X (2.0.0): its extra lines in the layout

THPS keeps its own drawing (the game's menu font - e.g. "VSYNC" without the hyphen the font lacks) and its GENERAL
OPTIONS (the game's own menu, not RECOMP OPTIONS). Its RECOMP OPTIONS become:

- **DISPLAY**: DISPLAY MODE, RENDER RESOLUTION, ASPECT RATIO, STRETCH CUTSCENES (= THPS's MOVIES AT 16:9, now under
  the standard name, OFF / ON), **MENUS AT 16:9** (PILLARBOXED / STRETCHED), **HUD AT 16:9** (ORIGINAL PROPORTIONS /
  STRETCHED), FIELD OF VIEW. The three 16:9 lines only with a wide aspect; the WIDESCREEN sub-page goes away.
- **POST PROCESSING** (was PICTURE): ANTIALIASING (one line now, not a sub-list), ANISOTROPIC FILTER, **MOTION BLUR**
  (the game's own blur, ON / OFF), **BRIGHTNESS** (-3 ... +5, GitHub #11), UPSCALING, FSR PRESET, FSR SHARPNESS.
  No BLOOM lines (THPS does not use the toolkit's bloom).
- **PERFORMANCE**: **FRAME RATE** (ORIGINAL 60 / 120), VSYNC, FPS, FRAME TIME, GAME SPEED.
- **RENDERING**: SHADER COMPILE, SHADER NOTICE.
- TEXTURES, LANGUAGE: hidden (no packs, one language).
- **No BUTTON MAPPING line in RECOMP OPTIONS** (the maintainer 2026-11-24): THPS's button mapping stays where
  it was in 1.2.8 - OPTIONS -> CONTROLS -> RECOMP CONTROL OPTIONS -> PLAYER 1-4 (that player's mapping and camera
  lines).
- THPS's own pages last: **BUTTON CHEATS ▸**, **BOOT ANIMATION ▸**.
- Y = DEFAULT on a line, RESTART NEEDED (RESTART NOW / LATER) as today. THPS's own look for the rules above
  (2026-11-24): values as "LABEL: VALUE" text lines - how the game's own options look - instead of values side by side;
  the hint is the game's button bar, and every value page has the game's own "PRESS Y FOR DEFAULT SETTINGS" line on
  top.

## Toolkit state

On option-c: SHADER COMPILE (HYBRID default), SHADER NOTICE, stretch, aspect, AA, aniso, bloom, V-Sync, the
PERFORMANCE overlay with GAME SPEED (no averages), the four frame pacing modes + auto, FSR with presets / sharpness,
F12 screenshots, the internal F9 debug menu. Not yet: texture packs, texture filtering.
