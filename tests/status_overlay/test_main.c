/*
 * Status overlays (src/nv2a_vk/status_overlay.c, 2026-11-19): the panels option-c draws for the FPS / frame-time
 * counter and the shader notice, in the layout after Option P's notices.
 *
 * Checks: the bundled font loads; a panel has the layout's size (line height max(14, H / 48), margins), the panel
 * colour 0x101420 at its corners and text pixels in the line colour; the same lines again do not re-rasterise
 * (gen unchanged), other lines do; a taller window gives taller lines; no lines = an empty panel. Writes the panels
 * as BMP files next to the test (argv[1], optional) for a look by eye.
 */
#include "status_overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what)
{
    printf("  %-62s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

static void write_bmp(const char *path, const SoPanel *p)
{
    FILE *f = fopen(path, "wb");
    unsigned char h[54];
    int y, row = p->w * 4;
    unsigned size = 54u + (unsigned)(row * p->h);
    if (!f) return;
    memset(h, 0, sizeof h);
    h[0] = 'B'; h[1] = 'M';
    memcpy(h + 2, &size, 4);
    h[10] = 54; h[14] = 40;
    memcpy(h + 18, &p->w, 4);
    memcpy(h + 22, &p->h, 4);
    h[26] = 1; h[28] = 32;
    fwrite(h, 1, 54, f);
    for (y = p->h - 1; y >= 0; y--)               /* bottom-up rows */
        fwrite(p->px + (size_t)y * (size_t)row, 1, (size_t)row, f);
    fclose(f);
}

static int is_panel(const unsigned char *px) { return px[0] == 0x20 && px[1] == 0x14 && px[2] == 0x10; }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    SoPanel fps = { 0 }, note = { 0 }, big = { 0 };
    SoLine fl[2] = { { "60 FPS", SO_TEXT }, { "16.7 MS", SO_TEXT } };
    SoLine nl[1] = { { "BUILDING SHADERS", SO_TITLE } };
    char path[512];
    uint32_t gen;
    int i, text = 0, title = 0, line_px = 1080 / 48;

    setvbuf(stdout, NULL, _IONBF, 0);
    check(so_ready(), "the bundled font loads");

    check(so_render(&fps, fl, 2, 1080) == 0 && fps.px, "FPS panel rendered (1080 lines)");
    check(fps.h == 2 * line_px + 2 * (line_px / 4), "FPS panel height = 2 lines + margins");
    check(fps.w > 7 * (line_px / 2) && fps.w % 4 == 0, "FPS panel width fits '16.7 MS', 4-pixel aligned");
    check(is_panel(fps.px) && is_panel(fps.px + ((size_t)fps.w * fps.h - 1) * 4), "panel colour 0x101420 at the corners");
    for (i = 0; i < fps.w * fps.h; i++)
        if (fps.px[i * 4] > 0xC0 && fps.px[i * 4 + 1] > 0xC0 && fps.px[i * 4 + 2] > 0xC0) text++;
    check(text > 100, "text pixels in 0xEBEBEB");

    gen = fps.gen;
    so_render(&fps, fl, 2, 1080);
    check(fps.gen == gen, "the same lines again: no re-rasterising");
    fl[0].text = "59 FPS";
    so_render(&fps, fl, 2, 1080);
    check(fps.gen != gen, "other text: rasterised again");

    check(so_render(&note, nl, 1, 1080) == 0 && note.px, "notice panel rendered");
    for (i = 0; i < note.w * note.h; i++)
        if (note.px[i * 4 + 2] > 0x60 && note.px[i * 4] > 0xC0) title++;       /* blue-ish: B high, R mid */
    check(title > 100, "notice text in the title colour 0x78BEFF");

    check(so_render(&big, fl, 2, 2160) == 0 && big.h > fps.h, "a taller window gives taller lines");
    check(so_render(&big, fl, 0, 2160) == 0 && big.w == 0 && !big.px, "no lines: an empty panel");

    snprintf(path, sizeof path, "%s/overlay_fps.bmp", dir);
    write_bmp(path, &fps);
    snprintf(path, sizeof path, "%s/overlay_notice.bmp", dir);
    write_bmp(path, &note);
    printf("panels: fps %dx%d, notice %dx%d -> %s\n", fps.w, fps.h, note.w, note.h, dir);
    so_free(&fps);
    so_free(&note);
    so_free(&big);
    printf("%s (%d failure%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
