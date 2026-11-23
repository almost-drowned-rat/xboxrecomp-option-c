/* port_setup_ui_sdl.c - the toolkit's progress UI (game_setup_ui.h: xr_setup_ui_*) for the Linux and Android port
 * launchers (launcher_port.h; origin: Panzer Dragoon Orta's launcher/setup_ui_sdl.c, Orta Recompiled, moved into
 * the toolkit unchanged on 2026-11-16),
 * in the look of the old Windows setup window (port_setup_win32.c, replaced 2026-11-23 by port_setup_ui_win95.c; the progress bar in the middle of the screen, for
 * the Linux AppImage too): a Windows 95 style dialog - grey #C0C0C0 face, 3D bevels, a navy title bar with white bold text, a
 * blocky navy progress bar in a sunken groove. Drawn with the SDL renderer (SDL_RenderDebugText, no font library;
 * MS Sans Serif is not shipped). Only the style; no Microsoft graphics. The close box cancels (as on Windows).
 *   Android: full screen, the dialog centred on black, scaled to the screen (libmain.so, packaging/android).
 *   Linux:   the window is the dialog (borderless, at 2x), dragged by its title bar (the port's launcher target).
 * All six functions of game_setup_ui.c are defined here, so the linker never pulls that archive member (as
 * setup_win32.c on Windows). O, 2026-11-11. SPDX-License-Identifier: GPL-3.0-or-later */
#include "game_setup_ui.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* full screen with the dialog centred: Android (SETUP_UI_FULLSCREEN: a desktop preview of that layout) */
#if defined(__ANDROID__) || defined(SETUP_UI_FULLSCREEN)
#define UI_FULLSCREEN 1
#else
#define UI_FULLSCREEN 0
#endif

#define DLG_W   400.0f                  /* the dialog in logical units (the Windows window's proportions) */
#define DLG_H   118.0f
#define TITLE_H 18.0f
#define COLS    ((int)((DLG_W - 28.0f) / 8.0f))   /* the debug font is 8x8 units */
#define LINES   3

struct XrSetupUi {
    char          title[128];
    char          status[1024];
    double        fraction;
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *icon;                 /* the port's icon in the title bar */
    int           sdl_up, failed, closed, close_down;
    float         ox, oy, s;            /* the dialog's place and scale on the screen */
    Uint64        last_draw;
};

#if !UI_FULLSCREEN
#define DESK_SCALE 2.0f                 /* the debug font's 8 units at 16 px: readable on a desktop */
/* borderless window: the title bar drags it, the close box stays a button */
static SDL_HitTestResult SDLCALL hit_test(SDL_Window *w, const SDL_Point *pt, void *data)
{
    float x = pt->x / DESK_SCALE, y = pt->y / DESK_SCALE;
    (void)w; (void)data;
    if (y >= 3 && y < 3 + TITLE_H && x < DLG_W - 24)
        return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}
#endif

XrSetupUi *xr_setup_ui_open(const char *title)
{
    XrSetupUi *ui = (XrSetupUi *)calloc(1, sizeof *ui);
    if (ui)
        snprintf(ui->title, sizeof ui->title, "%s Setup", title ? title : "Game");
    return ui;
}

static int ui_show(XrSetupUi *ui)
{
    if (ui->win || ui->failed)
        return ui->win != NULL;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        ui->failed = 1;
        return 0;
    }
    ui->sdl_up = 1;
#if UI_FULLSCREEN
    if (!SDL_CreateWindowAndRenderer(ui->title, 0, 0, SDL_WINDOW_FULLSCREEN, &ui->win, &ui->ren)) {
        ui->failed = 1;
        return 0;
    }
#else
    if (!SDL_CreateWindowAndRenderer(ui->title, (int)(DLG_W * DESK_SCALE), (int)(DLG_H * DESK_SCALE),
                                     SDL_WINDOW_BORDERLESS, &ui->win, &ui->ren)) {
        ui->failed = 1;
        return 0;
    }
    SDL_SetWindowHitTest(ui->win, hit_test, ui);
#endif
    {
        /* the port's icon (kernel.h xbox_set_window_icon): the window's, and drawn in the title bar (setup_win32.c) */
        extern const void *xbox_window_icon(size_t *n);
        size_t in = 0;
        const void *ib = xbox_window_icon(&in);
        SDL_Surface *is = ib ? SDL_LoadBMP_IO(SDL_IOFromConstMem(ib, in), true) : NULL;
        if (is) {
            SDL_SetWindowIcon(ui->win, is);
            ui->icon = SDL_CreateTextureFromSurface(ui->ren, is);
            if (ui->icon)
                SDL_SetTextureScaleMode(ui->icon, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(is);
        }
    }
    return 1;
}

static void fill(SDL_Renderer *r, float x, float y, float w, float h, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_FRect f = { x, y, w, h };
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderFillRect(r, &f);
}

/* the classic two-pixel bevel (setup_win32.c bevel): raised (the window, the close box) or sunken (the groove) */
static void bevel(SDL_Renderer *r, float x, float y, float w, float h, int raised)
{
    Uint8 otl = raised ? 255 : 128, obr = raised ? 0 : 255, itl = raised ? 192 : 0, ibr = raised ? 128 : 192;
    fill(r, x, y, w, 1, otl, otl, otl);
    fill(r, x, y, 1, h, otl, otl, otl);
    fill(r, x, y + h - 1, w, 1, obr, obr, obr);
    fill(r, x + w - 1, y, 1, h, obr, obr, obr);
    fill(r, x + 1, y + 1, w - 2, 1, itl, itl, itl);
    fill(r, x + 1, y + 1, 1, h - 2, itl, itl, itl);
    fill(r, x + 1, y + h - 2, w - 2, 1, ibr, ibr, ibr);
    fill(r, x + w - 2, y + 1, 1, h - 2, ibr, ibr, ibr);
}

/* word-wrapped to COLS, at most LINES lines; '\n' starts a new line (game_setup_ui.c ui_text) */
static void text_wrapped(SDL_Renderer *ren, float x, float y, const char *s)
{
    char line[COLS + 1];
    int row;
    for (row = 0; row < LINES && *s; row++) {
        int n = 0, brk = -1;
        while (s[n] && s[n] != '\n' && n < COLS) {
            if (s[n] == ' ')
                brk = n;
            n++;
        }
        if (s[n] && s[n] != '\n' && s[n] != ' ' && brk > 0)
            n = brk;
        memcpy(line, s, (size_t)n);
        line[n] = 0;
        SDL_RenderDebugText(ren, x, y + 11.0f * row, line);
        s += n;
        while (*s == ' ')
            s++;
        if (*s == '\n')
            s++;
    }
}

static void draw(XrSetupUi *ui)
{
    SDL_Renderer *r = ui->ren;
    int ow = 0, oh = 0, slots, blocks, k;
    float gx, gy, gw, gh, inner_l, inner_w, gap = 2.0f, sx, sy;
    double f = ui->fraction < 0 ? 0 : ui->fraction > 1 ? 1 : ui->fraction;
    SDL_GetRenderOutputSize(r, &ow, &oh);
#if UI_FULLSCREEN
    /* the dialog fills ~70 % of the width (at most 85 % of the height), centred */
    sx = ow * 0.70f / DLG_W;
    sy = oh * 0.85f / DLG_H;
#else
    /* the window is the dialog (a HiDPI output is just more pixels) */
    sx = ow / DLG_W;
    sy = oh / DLG_H;
#endif
    ui->s = sx < sy ? sx : sy;
    if (ui->s < 1.0f)
        ui->s = 1.0f;
    ui->ox = (ow / ui->s - DLG_W) / 2.0f;
    ui->oy = (oh / ui->s - DLG_H) / 2.0f;
    SDL_SetRenderScale(r, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_SetRenderScale(r, ui->s, ui->s);
    {
        const float x = ui->ox, y = ui->oy;
        /* the window: face + raised bevel */
        fill(r, x, y, DLG_W, DLG_H, 192, 192, 192);
        bevel(r, x, y, DLG_W, DLG_H, 1);
        /* title bar: navy, white bold caption (drawn twice, half a unit apart), the close box right */
        fill(r, x + 3, y + 3, DLG_W - 6, TITLE_H, 0, 0, 128);
        if (ui->icon) {
            SDL_FRect ir = { x + 5, y + 4, 16, 16 };
            SDL_RenderTexture(r, ui->icon, NULL, &ir);
        }
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDebugText(r, x + 24, y + 3 + (TITLE_H - 8) / 2, ui->title);
        SDL_RenderDebugText(r, x + 24.5f, y + 3 + (TITLE_H - 8) / 2, ui->title);
        fill(r, x + DLG_W - 21, y + 5, 16, 14, 192, 192, 192);
        bevel(r, x + DLG_W - 21, y + 5, 16, 14, !ui->close_down);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderDebugText(r, x + DLG_W - 17 + ui->close_down, y + 8 + ui->close_down, "x");
        /* the status text */
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        text_wrapped(r, x + 14, y + 32, ui->status);
        /* the groove + its blocks (setup_win32.c: ~10 units + 2 gap over the whole inner width) */
        gx = x + 14; gy = y + 32 + 11.0f * LINES + 6; gw = DLG_W - 28; gh = 20;
        bevel(r, gx, gy, gw, gh, 0);
        inner_l = gx + 3;
        inner_w = gw - 6;
        slots = (int)((inner_w + gap) / (10 + gap));
        if (slots < 1)
            slots = 1;
        blocks = (int)(slots * f + 0.5);
        for (k = 0; k < blocks; k++) {
            float l = inner_l + k * (inner_w + gap) / slots, rr = inner_l + (k + 1) * (inner_w + gap) / slots - gap;
            fill(r, l, gy + 3, rr - l, gh - 6, 0, 0, 128);
        }
    }
    SDL_RenderPresent(r);
}

/* a touch / click in dialog units inside the close box */
static int in_close(XrSetupUi *ui, float px, float py)
{
    float x = px / ui->s - ui->ox, y = py / ui->s - ui->oy;
    return ui->s > 0 && x >= DLG_W - 24 && x <= DLG_W - 2 && y >= 2 && y <= 22;
}

/* Quit / the close box / Alt+F4 during the installation: asked first (the maintainer 2026-11-23, every launcher) -
 * No goes on, Yes stops it cleanly (launcher.c stop_build_tools). RECOMP_NO_DIALOGS=1 (the tests): Yes. */
static void want_quit(XrSetupUi *ui)
{
    static const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
    };
    SDL_MessageBoxData box;
    int hit = 0;
    ui->close_down = 0;
    if (ui->closed)
        return;
    if (xr_no_dialogs()) {
        ui->closed = 1;
        return;
    }
    SDL_zero(box);
    box.flags = SDL_MESSAGEBOX_INFORMATION;
    box.window = ui->win;
    box.title = ui->title;
    box.message = "The installation is not finished. Quit anyway?";
    box.numbuttons = 2;
    box.buttons = buttons;
    if (!SDL_ShowMessageBox(&box, &hit))
        hit = 1;                        /* no box possible: the player's close stands */
    if (hit == 1)
        ui->closed = 1;
    if (ui->ren)
        draw(ui);                       /* (the box may have covered the dialog) */
}

void xr_setup_ui_progress(void *p, double fraction, const char *text)
{
    XrSetupUi *ui = (XrSetupUi *)p;
    SDL_Event e;
    Uint64 now;
    if (!ui || !ui_show(ui))
        return;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            want_quit(ui);
        } else if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_UP) {
            int ow = 0, oh = 0;
            SDL_GetRenderOutputSize(ui->ren, &ow, &oh);
            if (in_close(ui, e.tfinger.x * ow, e.tfinger.y * oh)) {
                if (e.type == SDL_EVENT_FINGER_DOWN)
                    ui->close_down = 1;
                else if (ui->close_down)
                    want_quit(ui);
            } else if (e.type == SDL_EVENT_FINGER_UP) {
                ui->close_down = 0;
            }
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            float d = SDL_GetWindowPixelDensity(ui->win);   /* window points -> output pixels */
            if (d <= 0)
                d = 1;
            if (in_close(ui, e.button.x * d, e.button.y * d)) {
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                    ui->close_down = 1;
                else if (ui->close_down)
                    want_quit(ui);
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                ui->close_down = 0;
            }
        }
    }
    snprintf(ui->status, sizeof ui->status, "%s", text ? text : "");
    ui->fraction = fraction;
    now = SDL_GetTicks();
    if (fraction < 1.0 && now - ui->last_draw < 50)
        return;
    ui->last_draw = now;
    draw(ui);
}

int xr_setup_ui_cancelled(void *p)
{
    XrSetupUi *ui = (XrSetupUi *)p;
    return ui && ui->closed;
}

int xr_no_dialogs(void)
{
    const char *e = getenv("RECOMP_NO_DIALOGS");
    return e && *e == '1';
}

void xr_setup_ui_message(XrSetupUi *ui, int error, const char *text)
{
    if (xr_no_dialogs() || (ui && ui->closed))
        return;                         /* (the player quit during the installation: no "... failed" after it) */
    SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_WARNING,
                             ui ? ui->title : "First start", text, ui ? ui->win : NULL);
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
    if (ui->sdl_up)
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    free(ui);
}
