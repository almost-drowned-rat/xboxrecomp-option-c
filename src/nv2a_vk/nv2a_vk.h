/* nv2a_vk.h -- Vulkan renderer for the pushbuffer executor (see nv2a_vk.c). */
#ifndef NV2A_VK_H
#define NV2A_VK_H

#include <stdint.h>

/* Register the Vulkan back end with the executor (nv2a_backend.h). */
void nv2a_vk_install(void);

/* Window events, delivered on the render thread (it pumps the SDL window in
 * every flip). Key: SDL scancode = USB HID usage ID and 1/0 for down/up,
 * auto-repeat dropped; scancode -1 when the window loses the focus, meaning
 * "release every key" -- fits xbox_InputKeyEvent directly. Quit: the window
 * was closed. Set either before nv2a_vk_install; NULL ignores the events. */
void nv2a_vk_set_key_handler(void (*fn)(int scancode, int down));
void nv2a_vk_set_quit_handler(void (*fn)(void));
/* The renderer cannot start (no device with Vulkan 1.3 and push descriptors;
 * no window, surface or swapchain): it never runs on headless. The reason
 * (each GPU, its driver's Vulkan version and what it lacks) goes to the log
 * and into a message box titled title, with help as the last line (e.g. the
 * README's requirements link; NULL = none) -- no box with
 * RECOMP_NO_DIALOGS=1. Then fn ends the process (NULL: exit(1)).
 * nv2a_vk_install checks the device at once, so set this before it. Only a
 * headless start asked for (RECOMP_VK_HEADLESS=1) skips all of this. Without
 * VK_EXT_vertex_input_dynamic_state the vertex formats are baked into the
 * pipelines (RECOMP_VK_NO_DYN_VERTEX_INPUT=1 forces that path). */
void nv2a_vk_set_fatal_handler(void (*fn)(void), const char *title, const char *help);
/* 1 + slot of a drawn-into surface covering the guest address, 0 if none
 * (CPU readback diagnostics). */
int nv2a_vk_render_target_at(uint32_t va);
/* 1 while the swapchain presents with FIFO -- the display then paces too, next
 * to the guest vblank -- or before the first swapchain; 0 with MAILBOX or
 * IMMEDIATE (default MAILBOX when the device has it; RECOMP_PRESENT). A port's
 * frame limiter keeps its FIFO behaviour while this is 1. Any thread. */
int nv2a_vk_present_fifo(void);

/* The refresh rate of the display the window is on, in Hz (59.94, 120, 144 ...);
 * 0 while not known (headless, before the window). Read on the render thread at
 * the window's creation and when it moves to another display or that display's
 * mode changes. A port's frame-rate option uses it (THPS 2X: 120 only on a display
 * at 120 Hz or more). Any thread. */
float nv2a_vk_display_hz(void);

/* RENDER RESOLUTION (porting_guide.md M6): factor = the vertical scale on the
 * title's own lines, 1.00..5.00 (the menu offers 0.25 steps at 4:3; a port
 * may offer the standard 16:9 sizes, e.g. 1.875 = 1600x900, kept to a
 * thousandth); wide = 1 when
 * gameplay is 16:9 -- then X = factor x 4/3 on every frame the port has not
 * marked 4:3 (xbox_SetPresentWide), else X = factor. 0 = no option (the
 * RECOMP_GL_SCALE default). Any thread; applied at the next flip, refused
 * with a log line if the surfaces would not fit. RECOMP_GL_SCALE[_X] set in
 * the environment overrides it. */
void nv2a_vk_set_render_scale(double factor, int wide);
/* 1 if the device can hold the current surfaces at that factor (image
 * limits, no earlier allocation failure there -- no free-VRAM estimate:
 * maintainer, 2026-10-26), 0 if the menu should refuse it. A factor that
 * fails to allocate sends the render scale back to the last good one. The 17 quarter steps are tracked; a factor between
 * two (1.875 = 900 lines) answers for the larger one; 1 before the first
 * frame. */
int nv2a_vk_render_scale_fits(double factor, int wide);

/* FPS COUNTER (porting_guide.md M6): which lines the counter in the window's
 * top-left corner shows -- frames per second, the longest frame of the last
 * half second in ms, both, or none (default). Any thread; the next frame.
 * RECOMP_FPS_HUD set in the environment overrides it (1 = both, fps, ms, 0). */
enum { NV2A_VK_HUD_OFF, NV2A_VK_HUD_FPS, NV2A_VK_HUD_BOTH, NV2A_VK_HUD_MS };
void nv2a_vk_set_fps_hud(int mode);   /* the old form; maps onto the PERFORMANCE overlay below */

/* PERFORMANCE overlay (maintainer 2026-11-22, same in option-p): three
 * independent parts, the port's ON/OFF switches FPS / FRAME TIME / GAME SPEED
 * (menu page PERFORMANCE, with V-SYNC). FPS = pictures shown a second;
 * FRAME TIME = the longest frame of the last half second; GAME SPEED = the
 * game's own speed in % (the vblanks its finished frames asked for - each
 * flip its swap interval - against the guest vblanks that went by; a title
 * that drops frames on purpose under its own clock reads low). Settings key
 * performance_overlay = fps / frametime / speed joined by '+', all, off
 * (nv2a_vk_perf_overlay_parse; unset: map the old fps_counter). Any thread; the
 * next frame. RECOMP_PERF_OVERLAY (same words) overrides; RECOMP_SPEED_LOG=1
 * logs "[speed] 5 s: average X %, slowest second Y %" (also while SPEED shows). */
enum { NV2A_VK_PERF_FPS = 1, NV2A_VK_PERF_FRAMETIME = 2, NV2A_VK_PERF_SPEED = 4, NV2A_VK_PERF_ALL = 7 };

/* UPSCALING (RECOMP OPTIONS > POST PROCESSING, maintainer 2026-11-20/21): fsr 0 = BILINEAR (as before), 1 = FSR
 * (AMD FidelityFX Super Resolution 1.0); preset 0 QUALITY / 1 BALANCED / 2 PERFORMANCE sets the internal resolution
 * (window height / 1.5 / 1.7 / 2.0, never below the title's own) - the RENDER RESOLUTION line has no effect while
 * FSR is on; sharpness 0 LOW / 1 MEDIUM / 2 HIGH / 3 MAX. Any thread; the next frame. RECOMP_VK_FSR=off|quality|
 * balanced|performance and RECOMP_VK_FSR_SHARP=low|medium|high|max override it. */
enum { NV2A_VK_FSR_QUALITY, NV2A_VK_FSR_BALANCED, NV2A_VK_FSR_PERFORMANCE };
enum { NV2A_VK_FSR_SHARP_LOW, NV2A_VK_FSR_SHARP_MEDIUM, NV2A_VK_FSR_SHARP_HIGH, NV2A_VK_FSR_SHARP_MAX };
void nv2a_vk_set_upscaling(int fsr, int preset, int sharpness);
void nv2a_vk_set_perf_overlay(int mask);
int  nv2a_vk_perf_overlay_parse(const char *s);

/* "Menus stretched" (aspect-ratio rule, porting_guide.md M6): with 16:9 on,
 * frames the port marked 4:3 (xbox_SetPresentWide(0): menus, movies) are
 * stretched to the 16:9 window instead of pillarboxed. Default off. Any
 * thread; the next flip. RECOMP_STRETCH_43=1|0 overrides it. */
void nv2a_vk_set_stretch_43(int on);

/* GAMMA RAMP: the title's D3DDevice_SetGammaRamp, which the console loads into the RAMDAC palette at vblank, so it
 * changes what is shown but not the frame buffer. Here every presented frame goes through the ramp in one
 * full-screen pass before FXAA / SMAA / the shader chain / the blit (render targets, readback and the GL dumps
 * stay untouched). 256 entries per channel, 0..255. An identity ramp (the default) costs nothing: no pass. Any
 * thread; the next flip. RECOMP_VK_GAMMA=0 turns it off. */
void nv2a_vk_set_gamma_ramp(const uint8_t r[256], const uint8_t g[256], const uint8_t b[256]);
/* The same with a source text for the log: every CHANGED ramp logs one "[VK] gamma: identity | ramp r a/b/c g .. b ..
 * at 64/128/192 (source <text>)" line (plus one "source start" line at the first flip); a port's menu passes e.g.
 * "menu brightness +2". nv2a_vk_set_gamma_ramp = source "port". From menus / settings, not per frame.
 * RECOMP_SHOT_GAMMA=1: named shots (g_xbox_screenshot_hook) are taken from the frame after SMAA, bloom and the ramp,
 * as the player sees it (also headless, for the shot), instead of the title's surface before them. */
void nv2a_vk_set_gamma_ramp_from(const uint8_t r[256], const uint8_t g[256], const uint8_t b[256],
                                 const char *source);

/* WINDOW CROP (maintainer rule, 2026-11-01; porting guide aspect-ratio rule): in windowed mode the black margins a
 * title leaves inside its frame (TV safe area / overscan) are cut off - the presented surface is read back every
 * 120 flips, its black edge rows / columns found (symmetric, at most 20 % a side), and the window shows the content
 * rectangle at its aspect. On by default; never fullscreen / borderless, the boot video or under a shader chain.
 * Any thread. RECOMP_VK_WINDOW_CROP=0|1 overrides it. Real 4:3 / 16:9 through the title comes at the end. */
void nv2a_vk_set_window_crop(int on);

/* HUD un-stretch (2026-11-07): draws with the transform program whose FNV
 * hash is program_hash (RECOMP_VK_SQUASH_LOG=1 prints the screen-space ones)
 * and a box of at most 60 % of the 640 logical width get their width divided
 * by k around the left edge, the centre or the right edge (THPS 2X: k =
 * aspect / (4/3); a title with its own 16:9 HUD: k = aspect / (16/9), which is
 * below 1 at 16:10 - widened back). Any thread; per frame. k == 1 or hash 0:
 * off. */
void nv2a_vk_set_hud_squash(uint32_t program_hash, double k);
/* 1 (default): squashed draws anchor by quarters (left edge / centre / right
 * edge); 0: all at the centre - for full-screen 2D screens the title draws
 * with its HUD program (menus over gameplay, name entry), kept together. */
void nv2a_vk_set_hud_squash_edges(int on);

/* ANTIALIASING (porting_guide.md M6): OFF, FXAA (the present pass, the default
 * -- the picture as before the option), SMAA (iryoku/smaa, three passes at the
 * presented surface's size before it is scaled into the window), MSAA 2X / 4X
 * (the presented surface and its depth only). Any thread; the next flip.
 * RECOMP_VK_AA=off|fxaa|smaa|msaa2|msaa4 overrides it; RECOMP_VK_FXAA=0 still
 * turns FXAA off. */
enum { NV2A_VK_AA_OFF, NV2A_VK_AA_FXAA, NV2A_VK_AA_SMAA, NV2A_VK_AA_MSAA2, NV2A_VK_AA_MSAA4 };
void nv2a_vk_set_antialiasing(int mode);
/* 0 once an MSAA count failed to allocate this run or the device lacks it --
 * the renderer went back to the count that last worked; the port's menu then
 * takes its setting back too. 1 for every other mode, and before the first
 * frame. Any thread. */
int nv2a_vk_antialiasing_fits(int mode);

/* BLOOM (a port-side look option, not on the console): bright parts of the presented frame glow. A bright pass
 * (soft knee above a threshold) at a quarter of the surface's size, a separable Gaussian blur there (two rounds,
 * 16-bit float), added onto the frame in one full-screen pass - after SMAA, before the gamma ramp, FXAA, the shader
 * chain and the blit (render targets, readback, screenshots and the GL dumps stay untouched). OFF (the default: no
 * pass, no images) or a strength 1..NV2A_VK_BLOOM_MAX: higher = a lower threshold and a stronger glow (5 = the
 * middle). Never during the boot animation, never headless. Any thread; the next flip.
 * RECOMP_VK_BLOOM=0..10 overrides it. */
enum { NV2A_VK_BLOOM_OFF = 0, NV2A_VK_BLOOM_MAX = 10 };
void nv2a_vk_set_bloom(int strength);

/* SHADERS (shader module, porting_guide.md M6): a RetroArch slang preset
 * (.slangp, UTF-8 path) applied by librashader to the game picture -- after
 * the render scale and SMAA / MSAA, into the window's game rectangle only
 * (the bars stay black, the FPS counter stays on top). FXAA is skipped while a
 * shader is on. The library is the user's librashader.dll / .so / .dylib next
 * to the exe (C ABI 2), loaded on the first preset; librashader's own disk
 * cache stays off. A preset is built on a worker thread -- the picture stays
 * unshaded until it is ready (NV2A_VK_SHADER_LOADING). NULL or "" = none (the
 * default; no pass, no cost). Any thread; RECOMP_SHADER_PRESET set in the
 * environment overrides it. A new preset drops the parameter overrides. */
enum { NV2A_VK_SHADER_OFF, NV2A_VK_SHADER_LOADING, NV2A_VK_SHADER_ON, NV2A_VK_SHADER_FAILED,
       NV2A_VK_SHADER_NO_LIBRARY };
void nv2a_vk_set_shader_preset(const char *path);
/* The port's shipped pipeline list (keys only, recorded in test play with RECOMP_VK_PIPELIST_RECORD=<file>):
 * prewarmed at the device's start, before the player's own caches, so the first session compiles less on the fly.
 * Call before the window is made; RECOMP_VK_PIPELIST=<file> does the same. NULL = none. (nv2a_vk.c "shipped
 * pipeline list"; maintainer 2026-11-15, step 6.) */
void nv2a_vk_set_pipeline_list(const char *path);
/* The pipeline list in the BACKGROUND (maintainer 2026-11-19, an option against Android's black first start): read at
 * start, built a few entries per frame on the async compile worker (installed as async builds; the first frame comes
 * at once). Call before the renderer starts; RECOMP_VK_PIPELIST_BG=0/1 when not called. Default off. _progress: 1
 * while entries are still going to the worker; done / total = pipelines handed over / in the list (a progress line). */
void nv2a_vk_set_pipeline_list_background(int on);
int nv2a_vk_pipeline_list_progress(uint32_t *done, uint32_t *total);
/* Async shader compile at run time, for a port's options menu (2026-11-18, maintainer's order via A: an option in
 * RECOMP OPTIONS). NV2A_VK_ASYNC_OFF = compile on the executor (every object drawn from its first frame, hitches),
 * _SKIP = build on the worker and skip the draw until it is done, _WAIT = build on the worker and wait up to
 * RECOMP_VK_ASYNC_WAIT_US per frame first (the default). RECOMP_VK_ASYNC stays the start value. Any thread; takes
 * effect at the renderer's next draw (builds already queued finish and are installed in every mode). Logged as
 * "[vk] async: mode <old> -> <new> (runtime)". _get = the mode in effect (or requested and not applied yet).
 * _HYBRID (RECOMP_VK_ASYNC=hybrid, in development - docs/hybrid_ubershader.md): while the shader builds, the draw is
 * drawn with the prebuilt uber fragment shader when the device has VK_EXT_graphics_pipeline_library (fast linking)
 * and a vertex shader for the draw exists; otherwise as _WAIT. */
enum { NV2A_VK_ASYNC_OFF = 0, NV2A_VK_ASYNC_SKIP = 1, NV2A_VK_ASYNC_WAIT = 2, NV2A_VK_ASYNC_HYBRID = 3 };
void nv2a_vk_set_async_mode(int mode);
int nv2a_vk_get_async_mode(void);
/* SHADER NOTICE (maintainer 2026-11-18; RECOMP OPTIONS line right under SHADER COMPILE): a small "BUILDING
 * SHADERS" panel in the top-right corner while draws wait for, skip or build a shader (and 1 s after), in the
 * layout of every option-c status line (status_overlay.c). On by default; any thread; RECOMP_VK_SHADER_NOTICE=0/1
 * forces it. Never in screenshots or surface dumps. */
void nv2a_vk_set_shader_notice(int on);
/* A parameter of the current preset (its #pragma parameter name), applied now
 * or once the preset is built. Any thread. */
void nv2a_vk_set_shader_param(const char *name, float value);
/* The parameters of the preset in place (its #pragma parameter lines), for a
 * port's menu: up to max of them into out; returns their count (0 while none
 * is in place). initial = the value the chain uses without an override (the
 * shader's default or the preset file's own value); step 0 means "none
 * given". *gen changes whenever the list does (a new preset). Any thread. */
typedef struct {
    char name[64];
    char desc[128];
    float initial, minimum, maximum, step;
} Nv2aVkShaderParam;
int nv2a_vk_shader_params(Nv2aVkShaderParam *out, int max, unsigned *gen);
/* NV2A_VK_SHADER_*: OFF, LOADING, ON, FAILED (the preset or a frame failed,
 * logged; the picture is unshaded) or NO_LIBRARY (no librashader next to the
 * exe, or another ABI). Any thread. */
int nv2a_vk_shader_status(void);
/* 1 if librashader's file is next to the exe (the menu greys the shader
 * lines out otherwise) -- the file only; its ABI is checked at the first
 * preset. Any thread. */
int nv2a_vk_shader_library_present(void);

/* DISPLAY MODE (porting_guide.md M6): WINDOWED, BORDERLESS (fullscreen at the
 * desktop size) or FULLSCREEN (exclusive, the display's native mode). Any
 * thread; applied by the render thread at the next flip, or when the window
 * is created. RECOMP_DISPLAY_MODE=windowed|borderless|fullscreen overrides it
 * (automated runs: windowed). Before the first call: windowed. */
enum { NV2A_VK_DISPLAY_WINDOWED, NV2A_VK_DISPLAY_BORDERLESS, NV2A_VK_DISPLAY_FULLSCREEN };
void nv2a_vk_set_display_mode(int mode);

/* The windowed rectangle the port saved last time (client area, screen
 * coordinates): call before the first frame. The height is kept, the width
 * follows the title's aspect ratio; a position on no display is centred
 * instead; w or h <= 0 = the default 960x720 / 1280x720. */
void nv2a_vk_set_window_rect(int x, int y, int w, int h);

/* The windowed rectangle as it is now, and a counter that changes whenever it
 * moved or was resized while windowed (not maximised, not fullscreen): the
 * port saves it when the counter changed. Any thread. */
unsigned nv2a_vk_window_rect(int *x, int *y, int *w, int *h);

/* The boot animation (src/bootvideo, porting_guide.md M6), before the title
 * starts, from the port's thread: nv2a_vk_boot_begin, then one call per
 * decoded frame (B8G8R8A8, w x h, rows top first; copied) - the NV2A thread,
 * which owns the window, presents it like a title frame (letterboxed in its
 * own aspect, display mode, antialiasing and a shader chain in place) through
 * g_xbox_nv2a_idle_hook. It cannot be skipped (the one video that is never
 * skippable, maintainer 2026-10-27). nv2a_vk_boot_frame returns 1 to go on, 0
 * when there is no window (headless, renderer failed). nv2a_vk_boot_end waits
 * until the frame is dropped; the title starts on black. */
int nv2a_vk_boot_begin(void);
int nv2a_vk_boot_frame(const uint8_t *bgra, int w, int h);
void nv2a_vk_boot_end(void);

#endif
