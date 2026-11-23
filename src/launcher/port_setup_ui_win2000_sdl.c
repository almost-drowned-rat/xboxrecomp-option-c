/*
 * port_setup_ui_win2000_sdl.c - option-c's port launcher window on Linux (2026-11-23, the maintainer:
 * option-p's launcher taken over, "copy it, the base stays as it is, only add what is missing"): a copy of option-p's
 * launcher/game_setup_ui_win95_sdl.c (ps2recomp-option-p b42fb84, GPL-3.0) with the Xbox ports' texts (one
 * disc image, "Install to:" as in port_setup_ui_win95.c), the font from src/launcher/third_party/liberation and two
 * additions for launcher_port.c (xr_port_ui_handover: the first page's window carries on as the build's progress
 * window; xr_port_ui_default_icon). Android keeps port_setup_ui_sdl.c. SPDX-License-Identifier: GPL-3.0-or-later
 *
 * option-p's description, unchanged:
 * game_setup_ui_win95_sdl.c - the launcher's window on Linux: game_setup_ui_win95.c (the Windows window) drawn with
 * SDL3, the same page in the look of Windows 2000 (the maintainer, 2026-11-21: the Linux launchers in Windows 2000's
 * look - the #D4D0C8 face, the title bar's navy-to-light-blue gradient, Windows 2000's bevels) - the maintainer's rule for every port's launcher ("the more old-fashioned the
 * better") and for this port's Linux build (2026-11-21: "a direct port of the launcher with all its features and its
 * look"). The same layout constants, colours, bevels, owner-drawn buttons, sunken fields, title bar with the close
 * box, etched line and progress bar; the same texts and checks. Only the system parts differ: the font is
 * Liberation Sans (SIL OFL 1.1, engine/data/fonts; MS Sans Serif is a Windows system font) at 8 pt, rasterised by
 * stb_truetype and drawn smoothed with full-strength strokes (the maintainer's choice over Windows' unsmoothed font); the dialogs are SDL3's (the desktop's
 * own file and folder pickers by its portal); the window is borderless and dragged by its title bar. On a scaled
 * screen (125 %, 150 %, 200 %) the page is drawn at the screen's own pixels, as Windows draws a dialog at 120 dpi
 * and more: the layout scaled, the font rasterised at the larger size, the bevels and lines one pixel - not a
 * 96-dpi picture the desktop stretches (blurred, uneven columns: the maintainer, 2026-11-21).
 * 2026-11-21. The interface is game_setup_ui.h's.
 */
#include "game_setup_ui.h"
#include "launcher_port.h"              /* xr_port_ui_* (the additions) */
#include "platform/host_paths.h"        /* xbox_host_root: the install folder shown when it is fixed */

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

/* Liberation Sans Regular and Bold (src/launcher/third_party/liberation, SIL OFL 1.1), built in by CMakeLists.txt */
static const unsigned char k_font_regular[] = {
#include "liberation_sans_regular.inc"
};
static const unsigned char k_font_bold[] = {
#include "liberation_sans_bold.inc"
};

typedef struct { Uint8 r, g, b; } Rgb;
/* Windows 2000's standard scheme: 3D face, highlight, shadow, dark shadow; the active caption's gradient */
static const Rgb K_FACE = {212, 208, 200}, K_LIGHT = {255, 255, 255}, K_SHADOW = {128, 128, 128}, K_DARK = {64, 64, 64},
                 K_TITLE = {10, 36, 106}, K_TITLE2 = {166, 202, 240}, K_TITLE_TEXT = {255, 255, 255},
                 K_TEXT = {0, 0, 0}, K_FRAME = {0, 0, 0}, K_PROGRESS = {10, 36, 106}, K_WINDOW = {255, 255, 255};

#define UI_W          520
#define UI_H          227               /* with one disc line; a second one adds UI_ROW */
#define UI_TOP        13                /* a third line for the intro (Liberation Sans is wider than MS Sans Serif) */
#define UI_ROW        34
#define UI_TITLE_H    18
#define FONT_PX       11.0f             /* 8 pt at 96 dpi: Windows' -MulDiv(8, 96, 72) = an 11-pixel em */
#define FIELD_MAX     4096

enum { ID_ISO = 100, ID_ISO2, ID_TARGET, ID_FILE, ID_BROWSE = 200, ID_BROWSE2, ID_BROWSE_TARGET, ID_COPY, ID_BROWSE_FILE,
       ID_GO = 300, ID_QUIT, ID_DLG_YES = 400, ID_DLG_NO };

typedef struct {
    int  id, x, y, w, h;
    int  button;                        /* 1: a push button, 0: an edit field */
    char label[32];
    char text[FIELD_MAX];               /* an edit field's text (UTF-8) */
    int  caret, scroll, sel_all;        /* caret (byte offset), first shown pixel, all selected */
    int  enabled;
} Ctl;

typedef struct {
    stbtt_fontinfo info;
    float          scale;               /* the page's units (96 dpi) */
    int            ascent;
    float          pscale;              /* the window's pixels (ui->z) */
    int            pascent;
} Font;

struct XrSetupUi {
    char          title[160];
    char          extra_label[120];     /* the second disc's line ("" none) */
    int           rows, lines, has_extra;
    char          file_label[120];
    char          file_filter_name[120], file_filter[60];
    char         *file_path;
    size_t        file_n;
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *icon;
    const void   *icon_bmp;
    size_t        icon_size;
    float         z;                    /* the window's pixels per page unit (Windows' pixel at 96 dpi) */
    float         pt;                   /* the window's points (mouse, hit test) per page unit */
    int           height;               /* the page's height in its units */
    Font          font, bold;
    int           fonts_ok;
    Ctl           ctl[12];
    int           nctl, focus, pressed, pressed_inside;
    int           failed, closed, close_down, close_inside, form_result, in_form, progress, sdl_up;
    int           building;             /* a progress call came: the percentage shown */
    int           dialog_open;          /* a file / folder picker is up: the page waits for its answer */
    char          dialog_path[FIELD_MAX];
    int           dialog_target, dialog_done;
    char          intro[400];
    char          status[600];
    Uint64        last_draw, caret_blink;
    /* option-c: a message / question drawn inside the window in the page's look (the maintainer 2026-11-23: SDL's own
       box - the desktop's look - did not fit the page); dlg[0] OK / Yes (the default), dlg[1] No */
    int           dlg_on, dlg_yes_no, dlg_focus;
    const char   *dlg_msg;
    Ctl           dlg[2];
};

void xr_setup_ui_icon(XrSetupUi *ui, const void *bmp, size_t n)
{
    if (ui) {
        ui->icon_bmp = bmp;
        ui->icon_size = n;
    }
}

/* option-c (launcher_port.c): the first page's window handed on - the toolkit launcher's next xr_setup_ui_open
 * returns it, so the page and the build's progress are one window (as on Windows); and the port's icon for a
 * window the launcher opens by itself (a repair, a game\ folder filled by hand). */
static XrSetupUi *s_handover;
static const void *s_default_icon;
static size_t s_default_icon_n;

void xr_port_ui_handover(XrSetupUi *ui) { s_handover = ui; }

void xr_port_ui_default_icon(const void *bmp, size_t n)
{
    s_default_icon = bmp;
    s_default_icon_n = n;
}

XrSetupUi *xr_setup_ui_open(const char *title)
{
    XrSetupUi *ui;
    if (s_handover) {
        ui = s_handover;
        s_handover = NULL;
        return ui;
    }
    ui = (XrSetupUi *)calloc(1, sizeof *ui);
    if (ui) {
        snprintf(ui->title, sizeof ui->title, "%s Setup", title ? title : "Game");
        ui->icon_bmp = s_default_icon;
        ui->icon_size = s_default_icon_n;
    }
    return ui;
}

/* ── drawing ─────────────────────────────────────────────────────────── */

static void colour(XrSetupUi *ui, Rgb c) { SDL_SetRenderDrawColor(ui->ren, c.r, c.g, c.b, 255); }

/* a page coordinate (Windows' pixels at 96 dpi) as the window's pixel */
static int X(const XrSetupUi *ui, int v) { return (int)floorf((float)v * ui->z + 0.5f); }

/* [l, r) x [t, b) in the window's pixels */
static void prect(XrSetupUi *ui, int l, int t, int r, int b, Rgb c)
{
    SDL_FRect f;
    if (r <= l || b <= t)
        return;
    f.x = (float)l; f.y = (float)t; f.w = (float)(r - l); f.h = (float)(b - t);
    colour(ui, c);
    SDL_RenderFillRect(ui->ren, &f);
}

static void fill(XrSetupUi *ui, int l, int t, int r, int b, Rgb c)     /* [l, r) x [t, b) of the page, as FillRect */
{
    prect(ui, X(ui, l), X(ui, t), X(ui, r), X(ui, b), c);
}

/* GDI's MoveTo + LineTo in the window's pixels: one pixel thick, the end point not drawn (only straight lines) */
static void pline(XrSetupUi *ui, int x0, int y0, int x1, int y1, Rgb c)
{
    if (y0 == y1) {
        if (x1 > x0) prect(ui, x0, y0, x1, y0 + 1, c);
        else         prect(ui, x1 + 1, y0, x0 + 1, y0 + 1, c);
    } else {
        if (y1 > y0) prect(ui, x0, y0, x0 + 1, y1, c);
        else         prect(ui, x0, y1 + 1, x0 + 1, y0 + 1, c);
    }
}

/* Windows 2000's two-pixel bevel: raised (buttons, the window - the face and white over the dark shadow and the
   shadow) or sunken (fields, the progress groove - the shadow and the dark shadow over white and the face). */
static void pbevel(XrSetupUi *ui, int l, int t, int r, int b, int raised)
{
    const Rgb otl = raised ? K_FACE : K_SHADOW, obr = raised ? K_DARK : K_LIGHT;
    const Rgb itl = raised ? K_LIGHT : K_DARK, ibr = raised ? K_SHADOW : K_FACE;
    pline(ui, l, b - 1, l, t, otl);
    pline(ui, l, t, r - 1, t, otl);
    pline(ui, r - 1, t, r - 1, b - 1, obr);
    pline(ui, r - 1, b - 1, l - 1, b - 1, obr);
    pline(ui, l + 1, b - 2, l + 1, t + 1, itl);
    pline(ui, l + 1, t + 1, r - 2, t + 1, itl);
    pline(ui, r - 2, t + 1, r - 2, b - 2, ibr);
    pline(ui, r - 2, b - 2, l, b - 2, ibr);
}

static void bevel(XrSetupUi *ui, int l, int t, int r, int b, int raised)
{
    pbevel(ui, X(ui, l), X(ui, t), X(ui, r), X(ui, b), raised);
}

static int font_init(Font *f, const unsigned char *data)
{
    int ascent, descent, gap;
    if (!stbtt_InitFont(&f->info, data, stbtt_GetFontOffsetForIndex(data, 0)))
        return 0;
    f->scale = stbtt_ScaleForMappingEmToPixels(&f->info, FONT_PX);
    stbtt_GetFontVMetrics(&f->info, &ascent, &descent, &gap);
    f->ascent = (int)(ascent * f->scale + 0.5f);
    f->pscale = f->scale;
    f->pascent = f->ascent;
    return 1;
}

/* the font rasterised for the window's pixels: 8 pt at 96 dpi times its scale */
static void font_pixels(Font *f, float z)
{
    int ascent, descent, gap;
    f->pscale = stbtt_ScaleForMappingEmToPixels(&f->info, FONT_PX * z);
    stbtt_GetFontVMetrics(&f->info, &ascent, &descent, &gap);
    f->pascent = (int)(ascent * f->pscale + 0.5f);
}

/* the next code point of UTF-8 text (a bad byte as itself) */
static int next_cp(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int cp = *p, n = 0;
    if (cp < 0x80) n = 0;
    else if ((cp & 0xE0) == 0xC0) { cp &= 0x1F; n = 1; }
    else if ((cp & 0xF0) == 0xE0) { cp &= 0x0F; n = 2; }
    else if ((cp & 0xF8) == 0xF0) { cp &= 0x07; n = 3; }
    p++;
    while (n-- > 0 && (*p & 0xC0) == 0x80)
        cp = (cp << 6) | (*p++ & 0x3F);
    *s = (const char *)p;
    return cp;
}

/* a glyph's advance in whole pixels, as a bitmap font's (GDI's non-antialiased text steps by whole pixels) */
static int advance(const Font *f, int cp, int next)
{
    int adv, lsb;
    float x;
    stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
    x = adv * f->pscale;
    if (next)
        x += stbtt_GetCodepointKernAdvance(&f->info, cp, next) * f->pscale;
    return (int)(x + 0.5f);
}

/* the text's width in the window's pixels */
static int text_pixels_n(const Font *f, const char *s, size_t n)
{
    const char *p = s, *end = s + n;
    int w = 0;
    while (p < end && *p) {
        const int cp = next_cp(&p);
        const char *q = p;
        const int next = q < end && *q ? next_cp(&q) : 0;
        w += advance(f, cp, next);
    }
    return w;
}

/* ... and in the page's units (f->pscale / f->scale: the window's pixels per unit) */
static int text_width_n(const Font *f, const char *s, size_t n)
{
    return (int)(text_pixels_n(f, s, n) * f->scale / f->pscale + 0.5f);
}

static int text_width(const Font *f, const char *s) { return text_width_n(f, s, strlen(s)); }

/* text with its cell's top at y (as TextOut: the ascent below y), in one colour, smoothed; drawn only in
   the window's pixel columns [clip_l, clip_r) */
static void text_n(XrSetupUi *ui, const Font *f, int x, int y, const char *s, size_t n, Rgb c, int clip_l, int clip_r)
{
    const char *p = s, *end = s + n;
    unsigned char glyph[64 * 64];
    const int top = X(ui, y) + f->pascent;
    x = X(ui, x);
    SDL_SetRenderDrawBlendMode(ui->ren, SDL_BLENDMODE_BLEND);
    while (p < end && *p) {
        const int cp = next_cp(&p);
        const char *q = p;
        const int next = q < end && *q ? next_cp(&q) : 0;
        int x0, y0, x1, y1, gw, gh, gx, gy;
        stbtt_GetCodepointBitmapBox(&f->info, cp, f->pscale, f->pscale, &x0, &y0, &x1, &y1);
        gw = x1 - x0;
        gh = y1 - y0;
        if (gw > 0 && gh > 0 && gw <= 64 && gh <= 64) {
            stbtt_MakeCodepointBitmap(&f->info, glyph, gw, gh, gw, f->pscale, f->pscale, cp);
            for (gy = 0; gy < gh; ++gy)
                for (gx = 0; gx < gw; ++gx) {
                    /* antialiased, the coverage raised (^0.6) so the strokes stay as dark as a bitmap font's:
                       at 125 % and more the unsmoothed 1-pixel strokes read thin and blurred (the maintainer) */
                    const int v = glyph[gy * gw + gx];
                    const int px = x + x0 + gx;
                    if (!v || px < clip_l || px >= clip_r)
                        continue;
                    SDL_SetRenderDrawColor(ui->ren, c.r, c.g, c.b, (Uint8)(SDL_powf(v / 255.0f, 0.6f) * 255.0f + 0.5f));
                    SDL_RenderPoint(ui->ren, (float)px, (float)(top + y0 + gy));
                }
        }
        x += advance(f, cp, next);
    }
}

static void text(XrSetupUi *ui, const Font *f, int x, int y, const char *s, Rgb c)
{
    text_n(ui, f, x, y, s, strlen(s), c, -100000, 100000);
}

static int line_height(const Font *f)
{
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(&f->info, &ascent, &descent, &gap);
    return (int)((ascent - descent) * f->scale + 0.5f);
}

/* DrawText's DT_WORDBREAK: the words wrapped at the rectangle's width, lines stacked from its top */
static void text_wrapped(XrSetupUi *ui, const Font *f, int l, int t, int r, int b, const char *s, Rgb c)
{
    const int width = r - l, step = line_height(f);
    int y = t;
    while (*s && y + step <= b + step / 2) {
        size_t fit = 0, k = 0, n = strlen(s);
        while (*s == ' ')
            s++, n--;
        /* the longest run of whole words that fits; a word longer than the line broken where it must */
        while (k < n) {
            size_t e = k;
            while (e < n && s[e] != ' ' && s[e] != '\n')
                e++;
            if (text_width_n(f, s, e) > width) {
                if (!fit) {
                    fit = e;
                    while (fit > 1 && text_width_n(f, s, fit) > width)
                        fit--;
                }
                break;
            }
            fit = e;
            if (e < n && s[e] == '\n')
                break;
            k = e + 1;
        }
        if (!fit)
            fit = n;
        text_n(ui, f, l, y, s, fit, c, X(ui, l), X(ui, r));
        s += fit;
        if (*s == '\n')
            s++;
        y += step;
    }
}

/* Marlett's "r" (the close box's cross) at 8 pt: two pixels thick, 8 x 7 */
static void close_cross(XrSetupUi *ui, int x, int y)
{
    static const char *rows[7] = {"##....##", ".##..##.", "..####..", "...##...", "..####..", ".##..##.", "##....##"};
    const int k = ui->z < 1.5f ? 1 : (int)(ui->z + 0.5f), px = X(ui, x), py = X(ui, y);   /* whole pixels, square */
    int r, i;
    for (r = 0; r < 7; ++r)
        for (i = 0; i < 8; ++i)
            if (rows[r][i] == '#')
                prect(ui, px + i * k, py + r * k, px + (i + 1) * k, py + (r + 1) * k, K_TEXT);
}

/* DrawFocusRect: a dotted rectangle, every other pixel (the window's pixels) */
static void focus_rect(XrSetupUi *ui, int l, int t, int r, int b)
{
    int i;
    colour(ui, K_FRAME);
    for (i = l; i < r; i += 2) {
        SDL_RenderPoint(ui->ren, (float)i, (float)t);
        SDL_RenderPoint(ui->ren, (float)i, (float)(b - 1));
    }
    for (i = t; i < b; i += 2) {
        SDL_RenderPoint(ui->ren, (float)l, (float)i);
        SDL_RenderPoint(ui->ren, (float)(r - 1), (float)i);
    }
}

static Ctl *find(XrSetupUi *ui, int id)
{
    int i;
    for (i = 0; i < ui->nctl; ++i)
        if (ui->ctl[i].id == id)
            return &ui->ctl[i];
    return NULL;
}

/* Owner-drawn push button: raised, sunken while pressed, the default one framed, a dotted focus rectangle. */
static void draw_button(XrSetupUi *ui, const Ctl *c, int focused)
{
    int l = c->x, t = c->y, r = c->x + c->w, b = c->y + c->h, tw, down;
    int pl = X(ui, l), pt = X(ui, t), pr = X(ui, r), pb = X(ui, b);
    const int inset = X(ui, 4) - X(ui, 0);
    down = ui->pressed == c->id && ui->pressed_inside;
    prect(ui, pl, pt, pr, pb, K_FACE);
    if (c->id == ID_GO || c->id == ID_DLG_YES) {   /* FrameRect black (one pixel), then inside it */
        prect(ui, pl, pt, pr, pt + 1, K_FRAME);
        prect(ui, pl, pb - 1, pr, pb, K_FRAME);
        prect(ui, pl, pt, pl + 1, pb, K_FRAME);
        prect(ui, pr - 1, pt, pr, pb, K_FRAME);
        pl++, pt++, pr--, pb--;
        l++, t++, r--, b--;
    }
    pbevel(ui, pl, pt, pr, pb, !down);
    tw = text_width(&ui->font, c->label);
    text(ui, &ui->font, l + (r - l - tw) / 2 + down, t + (b - t - line_height(&ui->font)) / 2 + down, c->label,
         c->enabled ? K_TEXT : K_SHADOW);
    if (focused)
        focus_rect(ui, pl + inset, pt + inset, pr - inset, pb - inset);
}

/* A sunken white field (WS_EX_CLIENTEDGE), the face colour when disabled; its text scrolled to keep the caret. */
static void draw_edit(XrSetupUi *ui, Ctl *c, int focused)
{
    const int l = c->x, t = c->y, r = c->x + c->w, b = c->y + c->h;
    const int il = l + 2 + 1, ir = r - 2 - 1;       /* the client edge, then the edit's own margin */
    const int ty = t + 2 + (b - t - 4 - line_height(&ui->font)) / 2;
    int caret_x;
    fill(ui, l, t, r, b, c->enabled ? K_WINDOW : K_FACE);
    bevel(ui, l, t, r, b, 0);
    caret_x = text_width_n(&ui->font, c->text, (size_t)c->caret);
    if (caret_x - c->scroll > ir - il - 1)
        c->scroll = caret_x - (ir - il - 1);
    if (caret_x - c->scroll < 0)
        c->scroll = caret_x;
    if (c->scroll < 0)
        c->scroll = 0;
    if (focused && c->sel_all && c->text[0]) {
        const int w = text_width(&ui->font, c->text);
        fill(ui, il - c->scroll < il ? il : il - c->scroll, ty, il + w - c->scroll > ir ? ir : il + w - c->scroll,
             ty + line_height(&ui->font), K_TITLE);
        text_n(ui, &ui->font, il - c->scroll, ty, c->text, strlen(c->text), K_LIGHT, X(ui, il), X(ui, ir));
    } else {
        text_n(ui, &ui->font, il - c->scroll, ty, c->text, strlen(c->text), c->enabled ? K_TEXT : K_SHADOW, X(ui, il),
               X(ui, ir));
    }
    if (focused && c->enabled && !c->sel_all && ((SDL_GetTicks() - ui->caret_blink) / 530) % 2 == 0) {
        const int x = il + caret_x - c->scroll;
        if (x >= il && x < ir)
            prect(ui, X(ui, x), X(ui, ty), X(ui, x) + 1, X(ui, ty + line_height(&ui->font)), K_TEXT);
    }
}

/* the window's pixels and points per page unit, now (the desktop may move it to another scale) */
static void measure(XrSetupUi *ui)
{
    int pw = 0, ph = 0, ww = 0, wh = 0;
    float z;
    SDL_GetCurrentRenderOutputSize(ui->ren, &pw, &ph);
    SDL_GetWindowSize(ui->win, &ww, &wh);
    z = pw > 0 ? (float)pw / UI_W : 1.0f;
    ui->pt = ww > 0 ? (float)ww / UI_W : 1.0f;
    if (z != ui->z) {
        ui->z = z;
        font_pixels(&ui->font, z);
        font_pixels(&ui->bold, z);
    }
}

/* The window sized so that its pixels are the page at the screen's scale: on Wayland its points stay the page's
   units and the pixels come from the density; where points are pixels (X11) the window grows instead. */
static void fit_window(XrSetupUi *ui)
{
    /* the display scale: density times content scale (Wayland 125 %: 1.25 x 1.0; X11 at 120 dpi: 1.0 x 1.25) */
    float want = SDL_GetWindowDisplayScale(ui->win), density = SDL_GetWindowPixelDensity(ui->win);
    int ww = 0, wh = 0, tw, th;
    if (want < 1.0f)
        want = 1.0f;
    if (density <= 0.0f)
        density = 1.0f;
    tw = (int)(UI_W * want / density + 0.5f);
    th = (int)(ui->height * want / density + 0.5f);
    if (SDL_GetWindowSize(ui->win, &ww, &wh) && (ww != tw || wh != th))
        SDL_SetWindowSize(ui->win, tw, th);
}

/* option-c: the message / question box inside the window - a small Windows 2000 dialog over the page's middle: the
   raised face, the caption's gradient with the window's title, the text, OK or Yes / No (the default one framed) */
#define DLG_W 400
#define DLG_H 136
static void dialog_place(XrSetupUi *ui, int *l, int *t)
{
    const int ch = UI_H + UI_ROW * (ui->lines - 1);
    *l = (UI_W - DLG_W) / 2;
    *t = (ch - DLG_H) / 2;
    if (*t < UI_TITLE_H + 6)
        *t = UI_TITLE_H + 6;
}

static void draw_dialog(XrSetupUi *ui)
{
    int l, t, r, b, x;
    dialog_place(ui, &l, &t);
    r = l + DLG_W;
    b = t + DLG_H;
    fill(ui, l, t, r, b, K_FACE);
    bevel(ui, l, t, r, b, 1);
    {
        const int pl = X(ui, l + 3), pr = X(ui, r - 3), pt = X(ui, t + 3), pb = X(ui, t + 3 + UI_TITLE_H);
        const int span = pr - pl > 1 ? pr - pl - 1 : 1;
        for (x = pl; x < pr; ++x) {
            const int k = x - pl;
            const Rgb c = {(Uint8)(K_TITLE.r + (K_TITLE2.r - K_TITLE.r) * k / span),
                           (Uint8)(K_TITLE.g + (K_TITLE2.g - K_TITLE.g) * k / span),
                           (Uint8)(K_TITLE.b + (K_TITLE2.b - K_TITLE.b) * k / span)};
            prect(ui, x, pt, x + 1, pb, c);
        }
    }
    text(ui, &ui->bold, l + 7, t + 5, ui->title, K_TITLE_TEXT);
    text_wrapped(ui, &ui->font, l + 14, t + 3 + UI_TITLE_H + 12, r - 14, b - 44, ui->dlg_msg ? ui->dlg_msg : "", K_TEXT);
    draw_button(ui, &ui->dlg[0], ui->dlg_focus == 0);
    if (ui->dlg_yes_no)
        draw_button(ui, &ui->dlg[1], ui->dlg_focus == 1);
}

static void paint(XrSetupUi *ui)
{
    const int cw = UI_W, ch = UI_H + UI_ROW * (ui->lines - 1);
    int title_l = 3, title_t = 3, title_r = cw - 3, title_b = 3 + UI_TITLE_H;
    int i;
    const int down = UI_ROW * (ui->lines - 1);
    SDL_SetRenderTarget(ui->ren, NULL);
    measure(ui);
    fill(ui, 0, 0, cw, ch, K_FACE);
    bevel(ui, 0, 0, cw, ch, 1);
    /* the title bar: Windows 2000's gradient, navy to light blue, the icon left, white bold caption; a close box with
       Marlett's cross */
    {
        int x;
        const int pl = X(ui, title_l), pr = X(ui, title_r), pt = X(ui, title_t), pb = X(ui, title_b);
        const int span = pr - pl > 1 ? pr - pl - 1 : 1;
        for (x = pl; x < pr; ++x) {        /* a colour per pixel column */
            const int k = x - pl;
            const Rgb c = {(Uint8)(K_TITLE.r + (K_TITLE2.r - K_TITLE.r) * k / span),
                           (Uint8)(K_TITLE.g + (K_TITLE2.g - K_TITLE.g) * k / span),
                           (Uint8)(K_TITLE.b + (K_TITLE2.b - K_TITLE.b) * k / span)};
            prect(ui, x, pt, x + 1, pb, c);
        }
    }
    if (ui->icon) {
        const float k = ui->z < 1.5f ? 1.0f : floorf(ui->z + 0.5f);     /* whole pixels, square */
        SDL_FRect d = {(float)X(ui, title_l + 2), (float)X(ui, title_t + 1), 16.0f * k, 16.0f * k};
        SDL_RenderTexture(ui->ren, ui->icon, NULL, &d);
    }
    text(ui, &ui->bold, title_l + (ui->icon ? 21 : 4), title_t + 2, ui->title, K_TITLE_TEXT);
    {
        const int l = title_r - 18, t = title_t + 2, r = title_r - 2, bb = title_t + 16;
        const int pressed = ui->close_down && ui->close_inside;
        fill(ui, l, t, r, bb, K_FACE);
        bevel(ui, l, t, r, bb, !pressed);
        close_cross(ui, l + 4 + pressed, t + 3 + pressed);
    }
    /* the page: what to do, the disc image's line */
    text_wrapped(ui, &ui->font, 14, 30, cw - 14, 58 + UI_TOP, ui->intro, K_TEXT);
    text(ui, &ui->font, 14, 70 + UI_TOP, "Disc image (.iso):", K_TEXT);
    if (ui->has_extra)
        text(ui, &ui->font, 14, 70 + UI_TOP + UI_ROW, ui->extra_label, K_TEXT);
    if (ui->file_path)
        text(ui, &ui->font, 14, 70 + UI_TOP + UI_ROW * (ui->rows - 1), ui->file_label, K_TEXT);
    text(ui, &ui->font, 14, 70 + UI_TOP + UI_ROW * ui->rows, "Install to:", K_TEXT);
    /* an etched line above the progress area */
    pline(ui, X(ui, 10), X(ui, 104 + UI_TOP + down), X(ui, cw - 10), X(ui, 104 + UI_TOP + down), K_SHADOW);
    pline(ui, X(ui, 10), X(ui, 104 + UI_TOP + down) + 1, X(ui, cw - 10), X(ui, 104 + UI_TOP + down) + 1, K_LIGHT);
    /* under the bar (drawn below): the step's text, and right of it how far the build is */
    if (ui->building) {
        /* the percentage right-aligned on the step's line; the step's text wraps short of it */
        char done[8];
        snprintf(done, sizeof done, "%d%%", ui->progress < 0 ? 0 : ui->progress > 100 ? 100 : ui->progress);
        text(ui, &ui->font, cw - 14 - text_width(&ui->font, done), 134 + UI_TOP + down, done, K_TEXT);
        text_wrapped(ui, &ui->font, 14, 134 + UI_TOP + down, cw - 14 - text_width(&ui->font, "100%") - 12, 147 + UI_TOP + down,
                     ui->status, K_TEXT);
    } else {
        text_wrapped(ui, &ui->font, 14, 134 + UI_TOP + down, cw - 14, 147 + UI_TOP + down, ui->status, K_TEXT);
    }
    {
        /* the progress bar (Windows 2000's): a one-pixel sunken edge - the shadow over the highlight - filled
           smoothly in navy from the left, a pixel inside it */
        const int pct = ui->progress < 0 ? 0 : ui->progress > 100 ? 100 : ui->progress;
        const int l = X(ui, 14), t = X(ui, 114 + UI_TOP + down), r = X(ui, cw - 14), b = X(ui, 128 + UI_TOP + down);
        fill(ui, 14, 114 + UI_TOP + down, cw - 14, 128 + UI_TOP + down, K_FACE);
        prect(ui, l, t, r, t + 1, K_SHADOW);
        prect(ui, l, t, l + 1, b, K_SHADOW);
        prect(ui, l, b - 1, r, b, K_LIGHT);
        prect(ui, r - 1, t, r, b, K_LIGHT);
        prect(ui, l + 2, t + 2, l + 2 + (r - l - 4) * pct / 100, b - 2, K_PROGRESS);
    }
    for (i = 0; i < ui->nctl; ++i) {
        Ctl *c = &ui->ctl[i];
        if (c->button)
            draw_button(ui, c, !ui->dlg_on && ui->focus == i);
        else
            draw_edit(ui, c, !ui->dlg_on && ui->focus == i);
    }
    if (ui->dlg_on)
        draw_dialog(ui);
    SDL_RenderPresent(ui->ren);
}

/* ── the controls ────────────────────────────────────────────────────── */

static Ctl *child(XrSetupUi *ui, int button, const char *label, int x, int y, int w, int h, int id)
{
    Ctl *c = &ui->ctl[ui->nctl++];
    memset(c, 0, sizeof *c);
    c->id = id;
    c->button = button;
    snprintf(c->label, sizeof c->label, "%s", label);
    c->x = x; c->y = y; c->w = w; c->h = h;
    c->enabled = 1;
    return c;
}

static void set_text(Ctl *c, const char *s)
{
    if (!c)
        return;
    snprintf(c->text, sizeof c->text, "%s", s ? s : "");
    c->caret = (int)strlen(c->text);
    c->scroll = 0;
    c->sel_all = 0;
}

static const char *get_text(XrSetupUi *ui, int id)
{
    Ctl *c = find(ui, id);
    return c ? c->text : "";
}

static void set_focus(XrSetupUi *ui, int index)
{
    if (index < 0 || index >= ui->nctl || !ui->ctl[index].enabled)
        return;
    ui->focus = index;
    ui->caret_blink = SDL_GetTicks();
    if (!ui->ctl[index].button) {
        ui->ctl[index].sel_all = 1;     /* a field reached by Tab selects its text, as a dialog's does */
        SDL_StartTextInput(ui->win);
    } else {
        SDL_StopTextInput(ui->win);
    }
}

static void tab(XrSetupUi *ui, int back)
{
    int i, k = ui->focus;
    for (i = 0; i < ui->nctl; ++i) {
        k = (k + (back ? ui->nctl - 1 : 1)) % ui->nctl;
        if (ui->ctl[k].enabled) {
            set_focus(ui, k);
            return;
        }
    }
}

static int exists(const char *path)
{
    SDL_PathInfo info;
    return path && *path && SDL_GetPathInfo(path, &info);
}

/* ── the dialogs ─────────────────────────────────────────────────────── */

static int no_dialogs_env(void)
{
    const char *e = getenv("RECOMP_NO_DIALOGS");
    return e && *e == '1';
}

static int dialog_run(XrSetupUi *ui, const char *msg, int yes_no);

static int box(XrSetupUi *ui, SDL_MessageBoxFlags flags, const char *msg, int yes_no)
{
    SDL_MessageBoxButtonData yn[2] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes"},
                                      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No"}};
    SDL_MessageBoxButtonData ok[1] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
                                       1, "OK"}};
    SDL_MessageBoxData d;
    int chosen = 0;
    char shown[2048];
    size_t i;
    /* the launcher's messages name its logs Windows' way (data\launcher.log): Linux's slashes here */
    snprintf(shown, sizeof shown, "%s", msg);
    for (i = 0; shown[i]; ++i)
        if (shown[i] == '\\')
            shown[i] = '/';
    /* option-c: inside the window, in its look (the desktop's box only without a window, or off the main thread) */
    if (ui && ui->win && ui->fonts_ok && !ui->dlg_on && SDL_IsMainThread())
        return dialog_run(ui, shown, yes_no);
    memset(&d, 0, sizeof d);
    d.flags = flags;
    d.window = ui && ui->win ? ui->win : NULL;
    d.title = ui ? ui->title : "Setup";
    d.message = shown;
    d.numbuttons = yes_no ? 2 : 1;
    d.buttons = yes_no ? yn : ok;
    if (!SDL_ShowMessageBox(&d, &chosen)) {
        fprintf(stderr, "%s: %s\n", d.title, shown);
        return 0;
    }
    return chosen;
}

static void SDLCALL picked(void *user, const char *const *files, int filter)
{
    XrSetupUi *ui = (XrSetupUi *)user;
    (void)filter;
    if (files && files[0])
        snprintf(ui->dialog_path, sizeof ui->dialog_path, "%s", files[0]);
    else
        ui->dialog_path[0] = 0;
    ui->dialog_done = 1;
}

static void browse(XrSetupUi *ui, int id)
{
    static const SDL_DialogFileFilter discs[2] = {
        {"Disc images (*.iso)", "iso"}, {"All files (*.*)", "*"}};
    SDL_DialogFileFilter file[2];
    ui->dialog_open = 1;
    ui->dialog_done = 0;
    ui->dialog_path[0] = 0;
    ui->dialog_target = id == ID_BROWSE ? ID_ISO : id == ID_BROWSE2 ? ID_ISO2 : id == ID_BROWSE_FILE ? ID_FILE : ID_TARGET;
    if (id == ID_BROWSE_TARGET) {
        SDL_ShowOpenFolderDialog(picked, ui, ui->win, NULL, false);
    } else if (id == ID_BROWSE_FILE) {
        char pattern[60];
        snprintf(pattern, sizeof pattern, "%s", ui->file_filter);
        file[0].name = ui->file_filter_name;
        file[0].pattern = strncmp(ui->file_filter, "*.", 2) ? ui->file_filter : ui->file_filter + 2;
        file[1].name = "All files (*.*)";
        file[1].pattern = "*";
        SDL_ShowOpenFileDialog(picked, ui, ui->win, file, 2, NULL, false);
    } else {
        SDL_ShowOpenFileDialog(picked, ui, ui->win, discs, 2, NULL, false);
    }
}

/* ── events ──────────────────────────────────────────────────────────── */

static void command(XrSetupUi *ui, int id)
{
    switch (id) {
    case ID_BROWSE:
    case ID_BROWSE2:
    case ID_BROWSE_FILE:
    case ID_BROWSE_TARGET:
        browse(ui, id);
        break;
    case ID_GO:
        if (ui->in_form)
            ui->form_result = 1;
        break;
    case ID_QUIT:
        /* as WM_CLOSE: during the installation asked first (the maintainer 2026-11-23) - No goes on, Yes stops it
           cleanly (launcher.c stop_build_tools); RECOMP_NO_DIALOGS=1 (the tests): Yes */
        if (!ui->in_form && !ui->closed && !no_dialogs_env() &&
            !box(ui, SDL_MESSAGEBOX_INFORMATION, "The installation is not finished. Quit anyway?", 1))
            break;
        ui->closed = 1;
        if (ui->in_form)
            ui->form_result = 0;
        break;
    }
}

static void close_window(XrSetupUi *ui) { command(ui, ID_QUIT); }

static int hit(const Ctl *c, int x, int y) { return x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h; }

/* option-c: the in-window box's own loop (modal: the page below takes no input). 1 = OK / Yes, 0 = No; the window's
   close box / Alt+F4 / Escape = No (with OK only: OK). Return / Space = the focused button, Tab / arrows move it. */
static int dialog_run(XrSetupUi *ui, const char *msg, int yes_no)
{
    int l, t, result = -1, i;
    dialog_place(ui, &l, &t);
    memset(ui->dlg, 0, sizeof ui->dlg);
    for (i = 0; i < 2; ++i) {
        ui->dlg[i].button = 1;
        ui->dlg[i].enabled = 1;
        ui->dlg[i].w = 80;
        ui->dlg[i].h = 24;
        ui->dlg[i].y = t + DLG_H - 36;
    }
    ui->dlg[0].id = ID_DLG_YES;
    ui->dlg[1].id = ID_DLG_NO;
    snprintf(ui->dlg[0].label, sizeof ui->dlg[0].label, "%s", yes_no ? "Yes" : "OK");
    snprintf(ui->dlg[1].label, sizeof ui->dlg[1].label, "No");
    if (yes_no) {
        ui->dlg[0].x = l + DLG_W / 2 - 86;
        ui->dlg[1].x = l + DLG_W / 2 + 6;
    } else {
        ui->dlg[0].x = l + (DLG_W - 80) / 2;
    }
    ui->dlg_msg = msg;
    ui->dlg_yes_no = yes_no;
    ui->dlg_focus = 0;
    ui->dlg_on = 1;
    ui->pressed = 0;
    ui->close_down = 0;
    SDL_CaptureMouse(false);
    SDL_RaiseWindow(ui->win);
    while (result < 0) {
        SDL_Event e;
        while (result < 0 && SDL_PollEvent(&e)) {
            const float s = ui->pt > 0.0f ? ui->pt : 1.0f;
            switch (e.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                result = yes_no ? 0 : 1;
                break;
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                fit_window(ui);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    const int x = (int)(e.button.x / s), y = (int)(e.button.y / s);
                    for (i = 0; i < 1 + yes_no; ++i)
                        if (hit(&ui->dlg[i], x, y)) {
                            ui->pressed = ui->dlg[i].id;
                            ui->pressed_inside = 1;
                            ui->dlg_focus = i;
                            SDL_CaptureMouse(true);
                        }
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (ui->pressed) {
                    const int x = (int)(e.motion.x / s), y = (int)(e.motion.y / s);
                    ui->pressed_inside = hit(&ui->dlg[ui->pressed == ID_DLG_YES ? 0 : 1], x, y);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_LEFT && ui->pressed) {
                    const int x = (int)(e.button.x / s), y = (int)(e.button.y / s);
                    const int k = ui->pressed == ID_DLG_YES ? 0 : 1;
                    SDL_CaptureMouse(false);
                    ui->pressed = 0;
                    if (hit(&ui->dlg[k], x, y))
                        result = k == 0 ? 1 : 0;
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_SPACE)
                    result = ui->dlg_focus == 0 ? 1 : 0;
                else if (e.key.key == SDLK_ESCAPE)
                    result = yes_no ? 0 : 1;
                else if (yes_no && (e.key.key == SDLK_TAB || e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT))
                    ui->dlg_focus ^= 1;
                break;
            default:
                break;
            }
        }
        paint(ui);
        SDL_Delay(15);
    }
    ui->dlg_on = 0;
    ui->pressed = 0;
    ui->dlg_msg = NULL;
    paint(ui);
    return result;
}

static int on_close_box(int x, int y) { return x >= UI_W - 21 && x < UI_W - 5 && y >= 5 && y < 19; }

static SDL_HitTestResult SDLCALL hit_test(SDL_Window *w, const SDL_Point *pt, void *data)
{
    XrSetupUi *ui = (XrSetupUi *)data;
    const float k = ui->pt > 0.0f ? ui->pt : 1.0f;
    const int x = (int)(pt->x / k), y = (int)(pt->y / k);
    (void)w;
    if (y >= 3 && y < 3 + UI_TITLE_H && x < UI_W - 22)       /* dragged by the title bar */
        return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

/* the byte offset in a field's text nearest to pixel x (from the field's text start) */
static int caret_at(XrSetupUi *ui, const Ctl *c, int x)
{
    const char *p = c->text;
    int best = 0, bestd = 1 << 30;
    for (;;) {
        const int off = (int)(p - c->text), w = text_width_n(&ui->font, c->text, (size_t)off);
        const int d = abs(w - x);
        if (d < bestd) {
            bestd = d;
            best = off;
        }
        if (!*p)
            break;
        next_cp(&p);
    }
    return best;
}

static int prev_offset(const char *s, int at)
{
    if (at <= 0)
        return 0;
    at--;
    while (at > 0 && ((unsigned char)s[at] & 0xC0) == 0x80)
        at--;
    return at;
}

static int next_offset(const char *s, int at)
{
    const char *p = s + at;
    if (!*p)
        return at;
    next_cp(&p);
    return (int)(p - s);
}

static void edit_insert(Ctl *c, const char *s)
{
    size_t n = strlen(s), len;
    if (c->sel_all) {
        c->text[0] = 0;
        c->caret = 0;
        c->sel_all = 0;
    }
    len = strlen(c->text);
    if (len + n >= sizeof c->text)
        return;
    memmove(c->text + c->caret + n, c->text + c->caret, len - (size_t)c->caret + 1);
    memcpy(c->text + c->caret, s, n);
    c->caret += (int)n;
}

static void key(XrSetupUi *ui, const SDL_KeyboardEvent *k)
{
    Ctl *c = ui->focus >= 0 && ui->focus < ui->nctl ? &ui->ctl[ui->focus] : NULL;
    const int ctrl = (k->mod & SDL_KMOD_CTRL) != 0;
    ui->caret_blink = SDL_GetTicks();
    if (k->key == SDLK_TAB) {
        tab(ui, (k->mod & SDL_KMOD_SHIFT) != 0);
        return;
    }
    if (k->key == SDLK_RETURN || k->key == SDLK_KP_ENTER) {   /* the focused button, else the default one (Go) */
        Ctl *go = find(ui, ID_GO);
        if (c && c->button && c->enabled)
            command(ui, c->id);
        else if (go && go->enabled)
            command(ui, ID_GO);
        return;
    }
    if (!c)
        return;
    if (c->button) {
        if (k->key == SDLK_SPACE && c->enabled)
            command(ui, c->id);
        return;
    }
    if (!c->enabled)
        return;
    if (ctrl && k->key == SDLK_A) {
        c->sel_all = 1;
        return;
    }
    if (ctrl && (k->key == SDLK_C || k->key == SDLK_X)) {
        if (c->sel_all) {
            SDL_SetClipboardText(c->text);
            if (k->key == SDLK_X) {
                c->text[0] = 0;
                c->caret = 0;
                c->sel_all = 0;
            }
        }
        return;
    }
    if (ctrl && k->key == SDLK_V) {
        char *clip = SDL_GetClipboardText();
        if (clip) {
            char *nl = strpbrk(clip, "\r\n");
            if (nl)
                *nl = 0;
            edit_insert(c, clip);
            SDL_free(clip);
        }
        return;
    }
    switch (k->key) {
    case SDLK_BACKSPACE:
        if (c->sel_all) { c->text[0] = 0; c->caret = 0; c->sel_all = 0; break; }
        if (c->caret > 0) {
            const int p = prev_offset(c->text, c->caret);
            memmove(c->text + p, c->text + c->caret, strlen(c->text + c->caret) + 1);
            c->caret = p;
        }
        break;
    case SDLK_DELETE:
        if (c->sel_all) { c->text[0] = 0; c->caret = 0; c->sel_all = 0; break; }
        if (c->text[c->caret]) {
            const int n = next_offset(c->text, c->caret);
            memmove(c->text + c->caret, c->text + n, strlen(c->text + n) + 1);
        }
        break;
    case SDLK_LEFT:
        c->caret = c->sel_all ? 0 : prev_offset(c->text, c->caret);
        c->sel_all = 0;
        break;
    case SDLK_RIGHT:
        c->caret = c->sel_all ? (int)strlen(c->text) : next_offset(c->text, c->caret);
        c->sel_all = 0;
        break;
    case SDLK_HOME:
        c->caret = 0;
        c->sel_all = 0;
        break;
    case SDLK_END:
        c->caret = (int)strlen(c->text);
        c->sel_all = 0;
        break;
    default:
        break;
    }
}

static void pump(XrSetupUi *ui)
{
    SDL_Event e;
    int dirty = 0;
    while (SDL_PollEvent(&e)) {
        const float s = ui->pt > 0.0f ? ui->pt : 1.0f;     /* points per page unit */
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            close_window(ui);
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
            dirty = 1;
            break;
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            fit_window(ui);
            dirty = 1;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT && !ui->dialog_open) {
                const int x = (int)(e.button.x / s), y = (int)(e.button.y / s);
                int i;
                if (on_close_box(x, y)) {
                    ui->close_down = 1;
                    ui->close_inside = 1;
                    SDL_CaptureMouse(true);
                }
                for (i = 0; i < ui->nctl; ++i) {
                    Ctl *c = &ui->ctl[i];
                    if (!c->enabled || !hit(c, x, y))
                        continue;
                    set_focus(ui, i);
                    if (c->button) {
                        ui->pressed = c->id;
                        ui->pressed_inside = 1;
                        SDL_CaptureMouse(true);
                    } else {
                        c->sel_all = 0;
                        c->caret = caret_at(ui, c, x - (c->x + 3) + c->scroll);
                    }
                }
                dirty = 1;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (ui->close_down || ui->pressed) {
                const int x = (int)(e.motion.x / s), y = (int)(e.motion.y / s);
                Ctl *c = ui->pressed ? find(ui, ui->pressed) : NULL;
                ui->close_inside = on_close_box(x, y);
                ui->pressed_inside = c && hit(c, x, y);
                dirty = 1;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                const int x = (int)(e.button.x / s), y = (int)(e.button.y / s);
                SDL_CaptureMouse(false);
                if (ui->close_down) {
                    ui->close_down = 0;
                    if (on_close_box(x, y))
                        close_window(ui);
                }
                if (ui->pressed) {
                    Ctl *c = find(ui, ui->pressed);
                    const int id = ui->pressed;
                    ui->pressed = 0;
                    if (c && hit(c, x, y))
                        command(ui, id);
                }
                dirty = 1;
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (!ui->dialog_open)
                key(ui, &e.key);
            dirty = 1;
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (!ui->dialog_open && ui->focus >= 0 && ui->focus < ui->nctl && !ui->ctl[ui->focus].button &&
                ui->ctl[ui->focus].enabled)
                edit_insert(&ui->ctl[ui->focus], e.text.text);
            dirty = 1;
            break;
        default:
            break;
        }
    }
    if (ui->dialog_open && ui->dialog_done) {
        ui->dialog_open = 0;
        if (ui->dialog_path[0])
            set_text(find(ui, ui->dialog_target), ui->dialog_path);
        dirty = 1;
    }
    (void)dirty;
    if (ui->win)
        paint(ui);                      /* (cheap: the caret blinks, the page is a few hundred rectangles) */
}

/* ── the window ──────────────────────────────────────────────────────── */

/* The window, made once (the first page or the first progress call). */
static int ui_show(XrSetupUi *ui)
{
    int height;
    if (ui->win || ui->failed)
        return ui->win != NULL;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        fprintf(stderr, "[LAUNCHER] no setup window (no display: %s) - building without one\n", SDL_GetError());
        ui->failed = 1;                 /* headless: build without a window */
        return 0;
    }
    ui->sdl_up = 1;
    ui->fonts_ok = font_init(&ui->font, k_font_regular) && font_init(&ui->bold, k_font_bold);
    if (!ui->fonts_ok) {
        fprintf(stderr, "[LAUNCHER] no setup window (its font did not load)\n");
        ui->failed = 1;
        return 0;
    }
    ui->rows = 1 + (ui->has_extra ? 1 : 0) + (ui->file_path ? 1 : 0);
    ui->lines = ui->rows + 1;
    height = UI_H + UI_ROW * (ui->lines - 1);
    ui->height = height;
    ui->z = ui->pt = 1.0f;
    if (!SDL_CreateWindowAndRenderer(ui->title, UI_W, height,
                                     SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &ui->win, &ui->ren)) {
        fprintf(stderr, "[LAUNCHER] no setup window (%s) - building without one\n", SDL_GetError());
        ui->failed = 1;
        return 0;
    }
    fit_window(ui);
    SDL_SetRenderVSync(ui->ren, 1);
    SDL_SetWindowHitTest(ui->win, hit_test, ui);
    SDL_SetWindowPosition(ui->win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    if (ui->icon_bmp && ui->icon_size) {
        SDL_Surface *bmp = SDL_LoadBMP_IO(SDL_IOFromConstMem(ui->icon_bmp, ui->icon_size), true);
        if (bmp) {
            SDL_SetWindowIcon(ui->win, bmp);
            ui->icon = SDL_CreateTextureFromSurface(ui->ren, bmp);
            if (ui->icon)
                SDL_SetTextureScaleMode(ui->icon, SDL_SCALEMODE_LINEAR);     /* a 64x64 BMP drawn at 16x16 and up */
            SDL_DestroySurface(bmp);
        }
    }
    /* sunken white fields, Browse..., Go, Quit - the Windows window's places */
    child(ui, 0, "", 170, 67 + UI_TOP, 250, 21, ID_ISO);
    child(ui, 1, "Browse...", 428, 67 + UI_TOP, 76, 22, ID_BROWSE);
    if (ui->has_extra) {
        child(ui, 0, "", 170, 67 + UI_TOP + UI_ROW, 250, 21, ID_ISO2);
        child(ui, 1, "Browse...", 428, 67 + UI_TOP + UI_ROW, 76, 22, ID_BROWSE2);
    }
    if (ui->file_path) {
        const int y = 67 + UI_TOP + UI_ROW * (ui->rows - 1);
        child(ui, 0, "", 170, y, 250, 21, ID_FILE);
        child(ui, 1, "Browse...", 428, y, 76, 22, ID_BROWSE_FILE);
    }
    child(ui, 0, "", 170, 67 + UI_TOP + UI_ROW * ui->rows, 250, 21, ID_TARGET);
    child(ui, 1, "Browse...", 428, 67 + UI_TOP + UI_ROW * ui->rows, 76, 22, ID_BROWSE_TARGET);
    {
        const int bottom = height - 46;
        child(ui, 1, "Go", UI_W - 184, bottom, 80, 24, ID_GO);
        child(ui, 1, "Quit", UI_W - 96, bottom, 80, 24, ID_QUIT);
    }
    ui->focus = -1;
    SDL_ShowWindow(ui->win);
    SDL_SyncWindow(ui->win);
    fit_window(ui);                     /* (Wayland: the scale known once the window is on a screen) */
    SDL_RaiseWindow(ui->win);
    return 1;
}

static void enable_page(XrSetupUi *ui, int on)
{
    int i;
    for (i = 0; i < ui->nctl; ++i)
        if (ui->ctl[i].id != ID_QUIT)
            ui->ctl[i].enabled = on;
    if (!on && ui->focus >= 0 && ui->focus < ui->nctl && ui->ctl[ui->focus].id != ID_QUIT) {
        ui->focus = -1;
        SDL_StopTextInput(ui->win);
    }
}

static int index_of(XrSetupUi *ui, int id)
{
    int i;
    for (i = 0; i < ui->nctl; ++i)
        if (ui->ctl[i].id == id)
            return i;
    return -1;
}

int xr_setup_ui_form(XrSetupUi *ui, const char *disc_name, char *iso, size_t n, const char *note,
                     const char *extra_label, char *extra, size_t extra_n, char *target, size_t target_n, int *copy)
{
    char path[FIELD_MAX], path2[FIELD_MAX], folder[FIELD_MAX];
    if (!ui || xr_no_dialogs())
        return -1;
    if (!ui->win && extra_label && *extra_label && extra) {     /* the second line: before the window is made */
        snprintf(ui->extra_label, sizeof ui->extra_label, "%s", extra_label);
        ui->has_extra = 1;
    }
    if (!ui_show(ui))
        return -1;
    snprintf(ui->intro, sizeof ui->intro, "%s%sChoose your %s disc image and the folder to install into, then press Go. "
             "The game is built on this PC once (a few minutes) and played from that folder.",
             note ? note : "", note && *note ? " " : "", disc_name ? disc_name : "");
    ui->status[0] = 0;
    ui->progress = 0;
    ui->building = 0;
    set_text(find(ui, ID_ISO), iso);
    if (find(ui, ID_ISO2) && extra)
        set_text(find(ui, ID_ISO2), extra);
    if (find(ui, ID_FILE))
        set_text(find(ui, ID_FILE), ui->file_path);
    /* option-c: target NULL = the folder is fixed (the launcher already runs in it - asked again for the image):
       shown, greyed, not checked */
    set_text(find(ui, ID_TARGET), target ? target : xbox_host_root());
    enable_page(ui, 1);
    if (!target) {
        if (find(ui, ID_TARGET))
            find(ui, ID_TARGET)->enabled = 0;
        if (find(ui, ID_BROWSE_TARGET))
            find(ui, ID_BROWSE_TARGET)->enabled = 0;
    }
    set_focus(ui, index_of(ui, ID_GO));
    ui->in_form = 1;
    ui->form_result = -1;
    for (;;) {
        pump(ui);
        if (ui->form_result == 1) {
            snprintf(path, sizeof path, "%s", get_text(ui, ID_ISO));
            if (!path[0] || !exists(path)) {
                box(ui, SDL_MESSAGEBOX_ERROR, "Please choose your disc image (.iso) with Browse...", 0);
                ui->form_result = -1;
                continue;
            }
            snprintf(folder, sizeof folder, "%s", get_text(ui, ID_TARGET));
            if (target && !folder[0]) {
                box(ui, SDL_MESSAGEBOX_ERROR, "Please choose the target folder with Browse...", 0);
                ui->form_result = -1;
                continue;
            }
            path2[0] = 0;
            if (find(ui, ID_ISO2))
                snprintf(path2, sizeof path2, "%s", get_text(ui, ID_ISO2));
            if (path2[0] && !exists(path2)) {    /* optional, but real */
                box(ui, SDL_MESSAGEBOX_ERROR, "The second disc image cannot be found. Leave its line empty if you do not "
                    "have that disc.", 0);
                ui->form_result = -1;
                continue;
            }
            if (find(ui, ID_FILE)) {
                const char *f = get_text(ui, ID_FILE);
                if (f[0] && !exists(f)) {
                    box(ui, SDL_MESSAGEBOX_ERROR, "That file cannot be found. Leave its line empty if you do not have it.",
                        0);
                    ui->form_result = -1;
                    continue;
                }
                snprintf(ui->file_path, ui->file_n, "%s", f);
            }
            break;
        }
        if (ui->form_result == 0)
            break;
        SDL_Delay(15);
    }
    ui->in_form = 0;
    if (ui->form_result != 1)
        return 0;
    snprintf(iso, n, "%s", path);
    if (extra && extra_n)
        snprintf(extra, extra_n, "%s", find(ui, ID_ISO2) ? path2 : "");
    if (target && target_n)
        snprintf(target, target_n, "%s", folder);
    if (copy)
        *copy = 1;                      /* (the discs are always unpacked into game/) */
    enable_page(ui, 0);
    snprintf(ui->intro, sizeof ui->intro, "The game is being built from your %s disc - this happens only once.",
             disc_name ? disc_name : "");
    pump(ui);
    return 1;
}

void xr_setup_ui_progress(void *p, double fraction, const char *msg)
{
    XrSetupUi *ui = (XrSetupUi *)p;
    Uint64 now;
    if (!ui || !ui_show(ui))
        return;
    if (!ui->intro[0])
        snprintf(ui->intro, sizeof ui->intro, "The game is being built from your disc - this happens only once.");
    enable_page(ui, 0);
    now = SDL_GetTicks();
    ui->progress = (int)((fraction < 0 ? 0 : fraction > 1 ? 1 : fraction) * 100.0 + 0.5);
    ui->building = 1;
    snprintf(ui->status, sizeof ui->status, "%s", msg ? msg : "");
    if (fraction < 1.0 && now - ui->last_draw < 50) {
        SDL_PumpEvents();               /* the window stays alive between redraws */
        return;                         /* 20 redraws a second are plenty */
    }
    ui->last_draw = now;
    pump(ui);
}

void xr_setup_ui_file_line(XrSetupUi *ui, const char *label, const char *filter_name, const char *filter, char *path,
                           size_t n)
{
    if (!ui || ui->win || !label || !*label || !path || !n)
        return;                         /* (before the window is made) */
    snprintf(ui->file_label, sizeof ui->file_label, "%s", label);
    snprintf(ui->file_filter_name, sizeof ui->file_filter_name, "%s",
             filter_name && *filter_name ? filter_name : "Files");
    snprintf(ui->file_filter, sizeof ui->file_filter, "%s", filter && *filter ? filter : "*.*");
    ui->file_path = path;
    ui->file_n = n;
}

void xr_setup_ui_extra_line(XrSetupUi *ui, const char *label)
{
    if (!ui || ui->win || !label || !*label)
        return;                         /* (before the window is made) */
    snprintf(ui->extra_label, sizeof ui->extra_label, "%s", label);
    ui->has_extra = 1;
}

int xr_setup_ui_cancelled(void *p)
{
    XrSetupUi *ui = (XrSetupUi *)p;
    /* the close box answered while a step runs - pumped (events + a redraw) on the main thread only: the steps'
       worker threads ask this too (launcher.c chd_job, the copy job), and a redraw from there raced the main thread's
       run_job redraw on the same renderer - a crash in SDL's GL renderer (GL_QueueDrawPoints, WSL llvmpipe, the
       tar.gz's first start at "Reading the disc", 2026-11-21). The main thread's run_job pumps anyway. */
    if (ui && ui->win && !ui->in_form && SDL_IsMainThread())
        pump(ui);
    return ui && ui->closed;
}

int xr_no_dialogs(void) { return no_dialogs_env(); }

void xr_setup_ui_message(XrSetupUi *ui, int error, const char *msg)
{
    if (xr_no_dialogs() || (ui && ui->closed && !ui->in_form))
        return;                         /* (the player quit during the installation: no "... failed" after it) */
    box(ui, error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_WARNING, msg ? msg : "", 0);
}

void xr_setup_ui_close(XrSetupUi *ui)
{
    if (!ui)
        return;
    if (ui->icon)
        SDL_DestroyTexture(ui->icon);
    if (ui->ren)
        SDL_DestroyRenderer(ui->ren);
    if (ui->win)
        SDL_DestroyWindow(ui->win);
    free(ui);
}
