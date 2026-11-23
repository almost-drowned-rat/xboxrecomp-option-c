/* status_overlay.h - option-c's status overlays (FPS / frame time, the shader notice) as text panels.
 *
 * One layout for every status line option-c itself shows (maintainer 2026-11-18: "it is only about
 * status messages: FPS, shaders etc."; porting guide M6): the layout of Option P's notices (ps2recomp_option_p
 * 20748c6, host\src\window.cpp DrawOverlay), rewritten here - no code taken (Option P is GPL-3.0, option-c MIT).
 * A dark panel 0x101420, line height max(14, window height / 48), text 0xEBEBEB (a title line 0x78BEFF, a chosen
 * line 0xFFD23C), margins line / 2 left and line / 4 top, a bold monospace font: DejaVu Sans Mono Bold (bundled,
 * third_party/dejavu, Bitstream Vera licence) rasterised on the CPU with stb_truetype (third_party/stb, MIT /
 * public domain). Only the bundled font is ever read. The renderer copies the finished BGRA pixels into the
 * swapchain image after the picture, so screenshots and surface dumps never contain a panel.
 */
#ifndef XR_STATUS_OVERLAY_H
#define XR_STATUS_OVERLAY_H

#include <stdint.h>

enum { SO_TEXT = 0xEBEBEB, SO_TITLE = 0x78BEFF, SO_CHOSEN = 0xFFD23C, SO_PANEL = 0x101420 };

typedef struct {
    const char *text;     /* ASCII; other bytes are drawn as '?' */
    uint32_t    rgb;      /* SO_TEXT / SO_TITLE / SO_CHOSEN or any 0xRRGGBB */
} SoLine;

/* A finished panel: tightly packed BGRA rows (w * 4 bytes each), opaque. */
typedef struct {
    uint8_t *px;
    int      w, h;
    uint32_t gen;         /* bumped each time the pixels change */
} SoPanel;

/* 1 when the bundled font is usable (made once; 0 = the caller keeps its fallback). */
int so_ready(void);

/* Lays out and rasterises `lines` for a window `win_h` pixels high into *p, only when the lines, their colours or
 * the line height changed since the last call with the same panel (cheap otherwise). 0 lines: an empty panel
 * (w = h = 0). Returns 0 on success, -1 when the font is not usable or memory ran out. */
int so_render(SoPanel *p, const SoLine *lines, int n, int win_h);

void so_free(SoPanel *p);

#endif
