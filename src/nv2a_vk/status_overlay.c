/* status_overlay.c - see status_overlay.h. CPU rasterisation of a few short status lines into an opaque BGRA panel. */
#include "status_overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
#include "third_party/stb/stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "dejavu_mono_bold.h"    /* s_font_ttf[], s_font_ttf_len: generated from third_party/dejavu (CMakeLists.txt) */

#define SO_MAX_W 1280            /* the panel never grows past 1280 x 720 (Option P's limit) */
#define SO_MAX_H 720
#define SO_MAX_LINES 16

static stbtt_fontinfo s_font;
static int s_font_state;          /* 0 not tried, 1 ready, -1 failed */

int so_ready(void)
{
    if (!s_font_state) {
        int off = stbtt_GetFontOffsetForIndex(s_font_ttf, 0);
        s_font_state = off >= 0 && stbtt_InitFont(&s_font, s_font_ttf, off) ? 1 : -1;
        if (s_font_state < 0)
            fprintf(stderr, "[overlay] bundled font not usable - status overlays fall back to the block HUD\n");
    }
    return s_font_state > 0;
}

/* A fingerprint of what the panel shows (lines, colours, line height): re-rasterise only when it changes. */
static uint64_t key_of(const SoLine *lines, int n, int line_px)
{
    uint64_t h = 1469598103934665603ull ^ (uint64_t)line_px;
    int i;
    for (i = 0; i < n; i++) {
        const unsigned char *c = (const unsigned char *)(lines[i].text ? lines[i].text : "");
        h = (h ^ lines[i].rgb) * 1099511628211ull;
        for (; *c; c++)
            h = (h ^ *c) * 1099511628211ull;
        h = (h ^ 0x0Au) * 1099511628211ull;
    }
    return h;
}

/* Per panel: the key of what it holds (kept next to the pixels by address; few panels exist). */
typedef struct { const SoPanel *p; uint64_t key; } SoKey;
static SoKey s_keys[8];

static uint64_t *key_slot(const SoPanel *p)
{
    int i;
    for (i = 0; i < 8; i++)
        if (s_keys[i].p == p)
            return &s_keys[i].key;
    for (i = 0; i < 8; i++)
        if (!s_keys[i].p) {
            s_keys[i].p = p;
            s_keys[i].key = 0;
            return &s_keys[i].key;
        }
    return NULL;
}

static int glyph_of(unsigned char c) { return c >= 0x20 && c < 0x7F ? c : '?'; }

int so_render(SoPanel *p, const SoLine *lines, int n, int win_h)
{
    int line_px = win_h / 48 > 14 ? win_h / 48 : 14;
    int pad_l = line_px / 2, pad_t = line_px / 4;
    int font_px = line_px - 4 > 6 ? line_px - 4 : 6;
    float scale;
    int ascent, descent, gap, adv, lsb, i, w = 0, h;
    uint64_t key, *slot;
    uint8_t *px;

    if (!so_ready())
        return -1;
    if (n > SO_MAX_LINES)
        n = SO_MAX_LINES;
    key = key_of(lines, n, line_px);
    slot = key_slot(p);
    if (slot && *slot == key && (p->px || n == 0))
        return 0;
    if (n <= 0) {
        so_free(p);
        p->gen++;
        if (slot) *slot = key;
        return 0;
    }

    scale = stbtt_ScaleForPixelHeight(&s_font, (float)font_px);
    stbtt_GetFontVMetrics(&s_font, &ascent, &descent, &gap);
    stbtt_GetCodepointHMetrics(&s_font, 'M', &adv, &lsb);   /* monospace: one advance for every glyph */
    {
        int cw = (int)(adv * scale + 0.5f);
        for (i = 0; i < n; i++) {
            int len = lines[i].text ? (int)strlen(lines[i].text) : 0;
            if (len * cw > w)
                w = len * cw;
        }
        w += 2 * pad_l;
    }
    h = n * line_px + 2 * pad_t;
    if (w > SO_MAX_W) w = SO_MAX_W;
    if (h > SO_MAX_H) h = SO_MAX_H;
    w = (w + 3) & ~3;                                        /* 16-byte rows for the buffer copy */

    px = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
    if (!px)
        return -1;
    for (i = 0; i < w * h; i++) {                            /* the opaque panel */
        px[i * 4 + 0] = (uint8_t)(SO_PANEL & 0xFF);
        px[i * 4 + 1] = (uint8_t)((SO_PANEL >> 8) & 0xFF);
        px[i * 4 + 2] = (uint8_t)((SO_PANEL >> 16) & 0xFF);
        px[i * 4 + 3] = 0xFF;
    }
    for (i = 0; i < n; i++) {
        const unsigned char *c = (const unsigned char *)(lines[i].text ? lines[i].text : "");
        float x = (float)pad_l;
        int base = pad_t + i * line_px + (line_px - font_px) / 2 + (int)(ascent * scale + 0.5f);
        uint32_t rgb = lines[i].rgb;
        int pr = (SO_PANEL >> 16) & 0xFF, pg = (SO_PANEL >> 8) & 0xFF, pb = SO_PANEL & 0xFF;
        int tr = (rgb >> 16) & 0xFF, tg = (rgb >> 8) & 0xFF, tb = rgb & 0xFF;
        for (; *c && x < (float)(w - pad_l); c++) {
            int g = glyph_of(*c), gw, gh, xo, yo, yy, xx;
            unsigned char *bm;
            stbtt_GetCodepointHMetrics(&s_font, g, &adv, &lsb);
            bm = stbtt_GetCodepointBitmap(&s_font, scale, scale, g, &gw, &gh, &xo, &yo);
            for (yy = 0; bm && yy < gh; yy++) {
                int py = base + yo + yy;
                if (py < 0 || py >= h) continue;
                for (xx = 0; xx < gw; xx++) {
                    int pxx = (int)(x + 0.5f) + xo + xx, a = bm[yy * gw + xx];
                    uint8_t *d;
                    if (pxx < 0 || pxx >= w || !a) continue;
                    d = px + ((size_t)py * (size_t)w + (size_t)pxx) * 4u;
                    d[0] = (uint8_t)(pb + (tb - pb) * a / 255);   /* coverage blended onto the panel colour */
                    d[1] = (uint8_t)(pg + (tg - pg) * a / 255);
                    d[2] = (uint8_t)(pr + (tr - pr) * a / 255);
                }
            }
            if (bm) stbtt_FreeBitmap(bm, NULL);
            x += adv * scale;
        }
    }
    so_free(p);
    p->px = px;
    p->w = w;
    p->h = h;
    p->gen++;
    if (slot) *slot = key;
    return 0;
}

void so_free(SoPanel *p)
{
    free(p->px);
    p->px = NULL;
    p->w = p->h = 0;
}
