/*
 * port_setup_ui_win95.c - option-c's port launcher window on Windows (2026-11-23, the maintainer: "all projects
 * have the same designs - Linux Win2000, Windows Win95 - the design follows Option P's"): a copy of ps2recomp-option-p's
 * launcher/game_setup_ui_win95.c (b42fb84, GPL-3.0) with the Xbox ports' texts (one disc image, "Install to:") and
 * the additions launcher_port.c needs (xr_port_ui_handover: the first page's window carries on as the build's
 * progress window; xr_port_ui_default_icon; target NULL = the folder fixed). Replaces port_setup_win32.c (the setup
 * window with a child process). SPDX-License-Identifier: GPL-3.0-or-later
 *
 * option-p's description, unchanged:
 * game_setup_ui_win95.c - the launcher's window on Windows, in the look of Windows 95 (the maintainer's rule for
 * every port's launcher, 2026-11-04 / 2026-11-20: "the more old-fashioned the better"): a borderless window drawn
 * with plain GDI - grey #C0C0C0 face, two-pixel 3D bevels, a navy title bar with white bold text, MS Sans Serif (the
 * system font, not shipped), a blocky progress bar; owner-drawn buttons. Only the style, no Microsoft graphics;
 * works under Wine. After the maintainer's launcher of another port of his, in C for this launcher. The interface is game_setup_ui.h's (the SDL window's on other systems).
 *
 * Its first page (xr_setup_ui_form): the disc image line with Browse..., Go and Quit; then, in the same window, the
 * steps' text and the progress bar. A start with the game already built opens no window.
 */
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include "game_setup_ui.h"
#include "launcher_port.h"              /* xr_port_ui_* (the additions) */
#include "platform/host_paths.h"        /* xbox_host_root: the install folder shown when it is fixed */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define K_FACE        RGB(192, 192, 192)
#define K_LIGHT       RGB(255, 255, 255)
#define K_SHADOW      RGB(128, 128, 128)
#define K_DARK        RGB(0, 0, 0)
#define K_TITLE       RGB(0, 0, 128)
#define K_TITLE_TEXT  RGB(255, 255, 255)
#define K_TEXT        RGB(0, 0, 0)
#define K_PROGRESS    RGB(0, 0, 128)

#define UI_W          520
#define UI_H          236               /* with one disc line; a second one adds UI_ROW */
#define UI_ROW        34
#define UI_TITLE_H    18

enum { ID_ISO = 100, ID_ISO2, ID_TARGET, ID_FILE, ID_BROWSE = 200, ID_BROWSE2, ID_BROWSE_TARGET, ID_COPY, ID_BROWSE_FILE,
       ID_GO = 300, ID_QUIT };

struct XrSetupUi {
    wchar_t  title[160];
    wchar_t  extra_label[120];          /* the second disc's line ("" none) */
    int      rows;                      /* path lines above the target folder's: the discs' (1 or 2) and the file's */
    int      lines;                     /* all lines: those, the target folder's, the copy box's */
    int      has_extra;                 /* a second disc's line (row 1) */
    wchar_t  file_label[120];           /* an optional file's line (the last of the rows; "" none) */
    wchar_t  file_filter[200];          /* its dialog's filter: name, pattern, "All files", "*.*" (each 0-ended) */
    char    *file_path;
    size_t   file_n;
    HWND     win, edit, edit2, edit_file, target, copy;
    HFONT    font, bold, marlett;
    HBRUSH   face;
    HICON    icon;
    const void *icon_bmp;
    size_t   icon_size;
    int      failed, closed, close_down, form_result, in_form, progress;
    wchar_t  intro[400];
    wchar_t  status[600];
    ULONGLONG last_draw;
};

static void widen(const char *s, wchar_t *out, int n)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, s ? s : "", -1, out, n))
        out[0] = 0;
}

void xr_setup_ui_icon(XrSetupUi *ui, const void *bmp, size_t n)
{
    if (ui) {
        ui->icon_bmp = bmp;
        ui->icon_size = n;
    }
}

/* The optional second disc's line on every page (game_setup_ui.h) - before the window is made. Before
   2026-11-21 it came only with the first page's xr_setup_ui_form: the page shown again after a failed disc check (in
   the target folder's copy, started with --go) had no Japanese-disc line. The same row as the first page's. */
void xr_setup_ui_extra_line(XrSetupUi *ui, const char *label)
{
    if (!ui || ui->win || !label || !*label)
        return;
    widen(label, ui->extra_label, 120);
    ui->has_extra = 1;
}

/* option-c (launcher_port.c): the first page's window handed on - the toolkit launcher's next xr_setup_ui_open
 * returns it, so the page and the build's progress are one window; and the port's icon for a window the launcher
 * opens by itself (an update's rebuild, a repair, a game\ folder filled by hand). */
static XrSetupUi *s_handover;
static const void *s_default_icon;
static size_t s_default_icon_n;

void xr_port_ui_handover(struct XrSetupUi *ui) { s_handover = ui; }

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
        wchar_t t[128];
        widen(title ? title : "Game", t, 128);
        _snwprintf(ui->title, 160, L"%ls Setup", t);
        ui->title[159] = 0;
        ui->icon_bmp = s_default_icon;
        ui->icon_size = s_default_icon_n;
    }
    return ui;
}

/* The classic two-pixel bevel: raised (buttons, the window) or sunken (fields, the progress groove). */
static void line(HDC dc, int x0, int y0, int x1, int y1, COLORREF c)
{
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x0, y0, NULL);
    LineTo(dc, x1, y1);
    SelectObject(dc, old);
    DeleteObject(pen);
}

static void bevel(HDC dc, RECT r, int raised)
{
    const COLORREF otl = raised ? K_LIGHT : K_SHADOW, obr = raised ? K_DARK : K_LIGHT;
    const COLORREF itl = raised ? K_FACE : K_DARK, ibr = raised ? K_SHADOW : K_FACE;
    line(dc, r.left, r.bottom - 1, r.left, r.top, otl);
    line(dc, r.left, r.top, r.right - 1, r.top, otl);
    line(dc, r.right - 1, r.top, r.right - 1, r.bottom - 1, obr);
    line(dc, r.right - 1, r.bottom - 1, r.left - 1, r.bottom - 1, obr);
    line(dc, r.left + 1, r.bottom - 2, r.left + 1, r.top + 1, itl);
    line(dc, r.left + 1, r.top + 1, r.right - 2, r.top + 1, itl);
    line(dc, r.right - 2, r.top + 1, r.right - 2, r.bottom - 2, ibr);
    line(dc, r.right - 2, r.bottom - 2, r.left, r.bottom - 2, ibr);
}

static void text(HDC dc, int x, int y, const wchar_t *s, HFONT font, COLORREF colour)
{
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colour);
    TextOutW(dc, x, y, s, (int)wcslen(s));
    SelectObject(dc, old);
}

static void paint(XrSetupUi *ui)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(ui->win, &ps);
    RECT client, title, close, rect, groove;
    HBRUSH brush;
    int inner_l, inner_w, slots, blocks, b;
    const int gap = 2;

    GetClientRect(ui->win, &client);
    FillRect(dc, &client, ui->face);
    bevel(dc, client, 1);
    /* the title bar: solid navy, the icon left, white bold caption; a close box with Marlett's cross */
    SetRect(&title, 3, 3, client.right - 3, 3 + UI_TITLE_H);
    brush = CreateSolidBrush(K_TITLE);
    FillRect(dc, &title, brush);
    DeleteObject(brush);
    if (ui->icon)
        DrawIconEx(dc, title.left + 2, title.top + 1, ui->icon, 16, 16, 0, NULL, DI_NORMAL);
    text(dc, title.left + (ui->icon ? 21 : 4), title.top + 2, ui->title, ui->bold, K_TITLE_TEXT);
    SetRect(&close, title.right - 18, title.top + 2, title.right - 2, title.top + 16);
    FillRect(dc, &close, ui->face);
    bevel(dc, close, !ui->close_down);
    text(dc, close.left + 3 + ui->close_down, close.top + 1 + ui->close_down, L"r", ui->marlett, K_TEXT);
    /* the page: what to do, the disc image's line */
    SetRect(&rect, 14, 30, client.right - 14, 58);
    {
        HGDIOBJ old = SelectObject(dc, ui->font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, K_TEXT);
        DrawTextW(dc, ui->intro, -1, &rect, DT_WORDBREAK);
        SelectObject(dc, old);
    }
    text(dc, 14, 70, L"Disc image (.iso):", ui->font, K_TEXT);
    if (ui->has_extra)
        text(dc, 14, 70 + UI_ROW, ui->extra_label, ui->font, K_TEXT);
    if (ui->file_path)
        text(dc, 14, 70 + UI_ROW * (ui->rows - 1), ui->file_label, ui->font, K_TEXT);
    text(dc, 14, 70 + UI_ROW * ui->rows, L"Install to:", ui->font, K_TEXT);
    /* an etched line above the progress area */
    {
        const int down = UI_ROW * (ui->lines - 1);
        line(dc, 10, 104 + down, client.right - 10, 104 + down, K_SHADOW);
        line(dc, 10, 105 + down, client.right - 10, 105 + down, K_LIGHT);
    }
    /* the step's text and the progress bar: a sunken groove with navy blocks spread over its whole width */
    SetRect(&rect, 14, 114 + UI_ROW * (ui->lines - 1), client.right - 14, 146 + UI_ROW * (ui->lines - 1));
    {
        HGDIOBJ old = SelectObject(dc, ui->font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, K_TEXT);
        DrawTextW(dc, ui->status, -1, &rect, DT_WORDBREAK);
        SelectObject(dc, old);
    }
    SetRect(&groove, 14, 152 + UI_ROW * (ui->lines - 1), client.right - 14, 172 + UI_ROW * (ui->lines - 1));
    FillRect(dc, &groove, ui->face);
    bevel(dc, groove, 0);
    inner_l = groove.left + 3;
    inner_w = groove.right - groove.left - 6;
    slots = (inner_w + gap) / (10 + gap);
    if (slots < 1)
        slots = 1;
    blocks = (slots * (ui->progress < 0 ? 0 : ui->progress > 100 ? 100 : ui->progress) + 50) / 100;
    brush = CreateSolidBrush(K_PROGRESS);
    for (b = 0; b < blocks; ++b) {
        RECT r;
        SetRect(&r, inner_l + b * (inner_w + gap) / slots, groove.top + 3,
                inner_l + (b + 1) * (inner_w + gap) / slots - gap, groove.bottom - 3);
        FillRect(dc, &r, brush);
    }
    DeleteObject(brush);
    EndPaint(ui->win, &ps);
}

/* Owner-drawn push button: raised, sunken while pressed, the default one framed, a dotted focus rectangle. */
static void draw_button(XrSetupUi *ui, const DRAWITEMSTRUCT *d)
{
    RECT r = d->rcItem, t;
    const int down = (d->itemState & ODS_SELECTED) != 0;
    wchar_t label[64];
    HGDIOBJ old;
    FillRect(d->hDC, &r, ui->face);
    if (d->CtlID == ID_GO) {
        FrameRect(d->hDC, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        InflateRect(&r, -1, -1);
    }
    bevel(d->hDC, r, !down);
    GetWindowTextW(d->hwndItem, label, 64);
    t = r;
    if (down)
        OffsetRect(&t, 1, 1);
    old = SelectObject(d->hDC, ui->font);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, IsWindowEnabled(d->hwndItem) ? K_TEXT : K_SHADOW);
    DrawTextW(d->hDC, label, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->hDC, old);
    if (d->itemState & ODS_FOCUS) {
        RECT f = r;
        InflateRect(&f, -4, -4);
        DrawFocusRect(d->hDC, &f);
    }
}

static HWND child(XrSetupUi *ui, const wchar_t *cls, const wchar_t *label, DWORD style, DWORD ex, int x, int y, int w,
                  int h, int id)
{
    HWND c = CreateWindowExW(ex, cls, label, WS_CHILD | WS_VISIBLE | style, x, y, w, h, ui->win,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)ui->font, TRUE);
    return c;
}

static int on_close_box(HWND hwnd, int x, int y)
{
    RECT client;
    GetClientRect(hwnd, &client);
    return x >= client.right - 21 && x < client.right - 5 && y >= 5 && y < 19;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    XrSetupUi *ui = (XrSetupUi *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        ui = (XrSetupUi *)((CREATESTRUCTW *)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ui);
        ui->win = hwnd;
    }
    if (!ui)
        return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_PAINT:
        paint(ui);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        draw_button(ui, (const DRAWITEMSTRUCT *)lp);
        return TRUE;
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)wp, K_FACE);
        return (LRESULT)ui->face;
    case WM_NCHITTEST: {    /* dragged by the title bar */
        POINT p;
        RECT client;
        p.x = (short)LOWORD(lp);
        p.y = (short)HIWORD(lp);
        ScreenToClient(hwnd, &p);
        GetClientRect(hwnd, &client);
        if (p.y >= 3 && p.y < 3 + UI_TITLE_H && p.x < client.right - 22)
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN:
        if (on_close_box(hwnd, (short)LOWORD(lp), (short)HIWORD(lp))) {
            ui->close_down = 1;
            InvalidateRect(hwnd, NULL, FALSE);
            SetCapture(hwnd);
        }
        return 0;
    case WM_LBUTTONUP:
        if (ui->close_down) {
            ui->close_down = 0;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
            if (on_close_box(hwnd, (short)LOWORD(lp), (short)HIWORD(lp)))
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BROWSE:
        case ID_BROWSE2: {
            wchar_t path[MAX_PATH] = L"";
            OPENFILENAMEW ofn;
            memset(&ofn, 0, sizeof ofn);
            ofn.lStructSize = sizeof ofn;
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"Disc images (*.iso)\0*.iso\0All files (*.*)\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn))
                SetWindowTextW(LOWORD(wp) == ID_BROWSE2 ? ui->edit2 : ui->edit, path);
            return 0;
        }
        case ID_BROWSE_FILE: {
            wchar_t path[MAX_PATH] = L"";
            OPENFILENAMEW ofn;
            memset(&ofn, 0, sizeof ofn);
            ofn.lStructSize = sizeof ofn;
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = ui->file_filter;
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn))
                SetWindowTextW(ui->edit_file, path);
            return 0;
        }
        case ID_BROWSE_TARGET: {
            wchar_t path[MAX_PATH] = L"";
            BROWSEINFOW bi;
            PIDLIST_ABSOLUTE id;
            memset(&bi, 0, sizeof bi);
            bi.hwndOwner = hwnd;
            bi.lpszTitle = L"The folder the game is installed in:";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            id = SHBrowseForFolderW(&bi);
            if (id) {
                if (SHGetPathFromIDListW(id, path))
                    SetWindowTextW(ui->target, path);
                CoTaskMemFree(id);
            }
            return 0;
        }
        case ID_GO:
            if (ui->in_form)
                ui->form_result = 1;
            return 0;
        case ID_QUIT:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_CLOSE:
        /* during the installation: asked first (the maintainer 2026-11-23) - No goes on, Yes stops it cleanly
           (launcher.c stop_build_tools); RECOMP_NO_DIALOGS=1 (the tests): Yes */
        if (!ui->in_form && !ui->closed && !xr_no_dialogs() &&
            MessageBoxW(hwnd, L"The installation is not finished. Quit anyway?", ui->title,
                        MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 0;
        ui->closed = 1;
        if (ui->in_form)
            ui->form_result = 0;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* The window, made once (the first page or the first progress call). */
static int ui_show(XrSetupUi *ui)
{
    WNDCLASSEXW wc;
    HDC screen;
    int height, x, y;
    if (ui->win || ui->failed)
        return ui->win != NULL;
    screen = GetDC(NULL);
    height = -MulDiv(8, GetDeviceCaps(screen, LOGPIXELSY), 72);
    ReleaseDC(NULL, screen);
    /* 8 pt MS Sans Serif, the Windows 95 dialog font (a system font; Windows substitutes when it is missing) */
    ui->font = CreateFontW(height, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"MS Sans Serif");
    ui->bold = CreateFontW(height, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"MS Sans Serif");
    ui->marlett = CreateFontW(height, 0, 0, 0, FW_NORMAL, 0, 0, 0, SYMBOL_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Marlett");
    ui->face = CreateSolidBrush(K_FACE);
    ui->icon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));   /* IDC_ARROW */
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hbrBackground = ui->face;
    wc.lpszClassName = L"XrRecompPortSetup95";
    RegisterClassExW(&wc);
    ui->rows = 1 + (ui->has_extra ? 1 : 0) + (ui->file_path ? 1 : 0);
    ui->lines = ui->rows + 1;
    x = (GetSystemMetrics(SM_CXSCREEN) - UI_W) / 2;
    y = (GetSystemMetrics(SM_CYSCREEN) - (UI_H + UI_ROW * (ui->lines - 1))) / 2;
    /* WS_CLIPCHILDREN: the face painted around the buttons and the field, not over them (a repaint of the window
       for the progress would wipe them until they repaint themselves) */
    if (!CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, ui->title,
                         WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN, x, y,
                         UI_W, UI_H + UI_ROW * (ui->lines - 1), NULL, NULL, wc.hInstance, ui)) {
        ui->failed = 1;
        return 0;
    }
    /* sunken white fields (WS_EX_CLIENTEDGE draws the 3D groove without visual styles), Browse..., Go, Quit */
    ui->edit = child(ui, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 170, 67, 250, 21, ID_ISO);
    child(ui, L"BUTTON", L"Browse...", BS_OWNERDRAW | WS_TABSTOP, 0, 428, 67, 76, 22, ID_BROWSE);
    if (ui->has_extra) {
        ui->edit2 = child(ui, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 170, 67 + UI_ROW, 250, 21,
                          ID_ISO2);
        child(ui, L"BUTTON", L"Browse...", BS_OWNERDRAW | WS_TABSTOP, 0, 428, 67 + UI_ROW, 76, 22, ID_BROWSE2);
    }
    if (ui->file_path) {
        const int y = 67 + UI_ROW * (ui->rows - 1);
        ui->edit_file = child(ui, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 170, y, 250, 21, ID_FILE);
        child(ui, L"BUTTON", L"Browse...", BS_OWNERDRAW | WS_TABSTOP, 0, 428, y, 76, 22, ID_BROWSE_FILE);
    }
    /* the target folder (issue 12: everything of the game in one folder of the player's choice); the discs are
       unpacked into its game\ folder after the build (2026-11-21: no copy box any more - the images needed once) */
    ui->target = child(ui, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 170, 67 + UI_ROW * ui->rows, 250,
                       21, ID_TARGET);
    child(ui, L"BUTTON", L"Browse...", BS_OWNERDRAW | WS_TABSTOP, 0, 428, 67 + UI_ROW * ui->rows, 76, 22,
          ID_BROWSE_TARGET);
    {
        const int bottom = UI_H + UI_ROW * (ui->lines - 1) - 46;
        child(ui, L"BUTTON", L"Go", BS_OWNERDRAW | WS_TABSTOP, 0, UI_W - 184, bottom, 80, 24, ID_GO);
        child(ui, L"BUTTON", L"Quit", BS_OWNERDRAW | WS_TABSTOP, 0, UI_W - 96, bottom, 80, 24, ID_QUIT);
    }
    ShowWindow(ui->win, SW_SHOW);
    return 1;
}

static void pump(XrSetupUi *ui)
{
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        if (!ui->win || !IsDialogMessageW(ui->win, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
}

static void enable_page(XrSetupUi *ui, BOOL on)
{
    EnableWindow(ui->edit, on);
    EnableWindow(GetDlgItem(ui->win, ID_BROWSE), on);
    if (ui->edit2) {
        EnableWindow(ui->edit2, on);
        EnableWindow(GetDlgItem(ui->win, ID_BROWSE2), on);
    }
    if (ui->edit_file) {
        EnableWindow(ui->edit_file, on);
        EnableWindow(GetDlgItem(ui->win, ID_BROWSE_FILE), on);
    }
    EnableWindow(ui->target, on);
    EnableWindow(GetDlgItem(ui->win, ID_BROWSE_TARGET), on);
    if (ui->copy)
        EnableWindow(ui->copy, on);
    EnableWindow(GetDlgItem(ui->win, ID_GO), on);
}

int xr_setup_ui_form(XrSetupUi *ui, const char *disc_name, char *iso, size_t n, const char *note,
                     const char *extra_label, char *extra, size_t extra_n, char *target, size_t target_n, int *copy)
{
    wchar_t disc[160], path[MAX_PATH * 2], path2[MAX_PATH * 2], noted[300], folder[MAX_PATH * 2];
    if (!ui || xr_no_dialogs())
        return -1;
    if (!ui->win && extra_label && *extra_label && extra) {     /* the second line: before the window is made */
        widen(extra_label, ui->extra_label, 120);
        ui->has_extra = 1;
    }
    if (!ui_show(ui))
        return -1;
    widen(disc_name, disc, 160);
    widen(note, noted, 300);
    _snwprintf(ui->intro, 400, L"%ls%lsChoose your %ls disc image and the folder to install into, then press Go. The game "
               L"is built on this PC once (a few minutes) and played from that folder.", noted,
               noted[0] ? L" " : L"", disc);
    ui->intro[399] = 0;
    wcscpy(ui->status, L"");
    ui->progress = 0;
    widen(iso, path, MAX_PATH * 2);
    SetWindowTextW(ui->edit, path);
    if (ui->edit2 && extra) {
        widen(extra, path2, MAX_PATH * 2);
        SetWindowTextW(ui->edit2, path2);
    }
    if (ui->edit_file) {
        wchar_t f[MAX_PATH * 2];
        widen(ui->file_path, f, MAX_PATH * 2);
        SetWindowTextW(ui->edit_file, f);
    }
    /* option-c: target NULL = the folder is fixed (the launcher already runs in it - asked again for the image):
       shown, greyed, not checked */
    widen(target ? target : xbox_host_root(), folder, MAX_PATH * 2);
    SetWindowTextW(ui->target, folder);
    enable_page(ui, TRUE);
    if (!target) {
        EnableWindow(ui->target, FALSE);
        EnableWindow(GetDlgItem(ui->win, ID_BROWSE_TARGET), FALSE);
    }
    SetFocus(GetDlgItem(ui->win, ID_GO));
    InvalidateRect(ui->win, NULL, FALSE);
    ui->in_form = 1;
    ui->form_result = -1;
    for (;;) {
        pump(ui);
        if (ui->form_result == 1) {
            GetWindowTextW(ui->edit, path, MAX_PATH * 2);
            if (!path[0] || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
                MessageBoxW(ui->win, L"Please choose your disc image (.iso) with Browse...",
                            ui->title, MB_OK | MB_ICONSTOP);
                ui->form_result = -1;
                continue;
            }
            GetWindowTextW(ui->target, folder, MAX_PATH * 2);
            if (target && !folder[0]) {
                MessageBoxW(ui->win, L"Please choose the target folder with Browse...", ui->title, MB_OK | MB_ICONSTOP);
                ui->form_result = -1;
                continue;
            }
            path2[0] = 0;
            if (ui->edit2)
                GetWindowTextW(ui->edit2, path2, MAX_PATH * 2);
            if (path2[0] && GetFileAttributesW(path2) == INVALID_FILE_ATTRIBUTES) {    /* optional, but real */
                MessageBoxW(ui->win, L"The second disc image cannot be found. Leave its line empty if you do not "
                            L"have that disc.", ui->title, MB_OK | MB_ICONSTOP);
                ui->form_result = -1;
                continue;
            }
            if (ui->edit_file) {
                wchar_t f[MAX_PATH * 2];
                GetWindowTextW(ui->edit_file, f, MAX_PATH * 2);
                if (f[0] && GetFileAttributesW(f) == INVALID_FILE_ATTRIBUTES) {
                    MessageBoxW(ui->win, L"That file cannot be found. Leave its line empty if you do not have it.",
                                ui->title, MB_OK | MB_ICONSTOP);
                    ui->form_result = -1;
                    continue;
                }
                if (!WideCharToMultiByte(CP_UTF8, 0, f, -1, ui->file_path, (int)ui->file_n, NULL, NULL))
                    ui->file_path[0] = 0;
            }
            break;
        }
        if (ui->form_result == 0)
            break;
        Sleep(15);
    }
    ui->in_form = 0;
    if (ui->form_result != 1)
        return 0;
    if (!WideCharToMultiByte(CP_UTF8, 0, path, -1, iso, (int)n, NULL, NULL))
        return 0;
    if (extra && extra_n && !WideCharToMultiByte(CP_UTF8, 0, ui->edit2 ? path2 : L"", -1, extra, (int)extra_n, NULL,
                                                 NULL))
        extra[0] = 0;
    if (target && target_n && !WideCharToMultiByte(CP_UTF8, 0, folder, -1, target, (int)target_n, NULL, NULL))
        target[0] = 0;
    if (copy)
        *copy = 1;                      /* (the discs are always unpacked into game\) */
    enable_page(ui, FALSE);
    _snwprintf(ui->intro, 400, L"The game is being built from your %ls disc - this happens only once.", disc);
    ui->intro[399] = 0;
    InvalidateRect(ui->win, NULL, FALSE);
    pump(ui);
    return 1;
}

void xr_setup_ui_progress(void *p, double fraction, const char *msg)
{
    XrSetupUi *ui = (XrSetupUi *)p;
    ULONGLONG now;
    if (!ui || !ui_show(ui))
        return;
    if (!ui->intro[0])
        wcscpy(ui->intro, L"The game is being built from your disc - this happens only once.");
    enable_page(ui, FALSE);
    pump(ui);
    now = GetTickCount64();
    if (fraction < 1.0 && now - ui->last_draw < 50)
        return;                         /* 20 redraws a second are plenty */
    ui->last_draw = now;
    ui->progress = (int)((fraction < 0 ? 0 : fraction > 1 ? 1 : fraction) * 100.0 + 0.5);
    widen(msg, ui->status, 600);
    InvalidateRect(ui->win, NULL, FALSE);
    UpdateWindow(ui->win);
}

void xr_setup_ui_file_line(XrSetupUi *ui, const char *label, const char *filter_name, const char *filter, char *path,
                           size_t n)
{
    wchar_t name[120], pattern[60];
    size_t k = 0;
    if (!ui || ui->win || !label || !*label || !path || !n)
        return;                         /* (before the window is made) */
    widen(label, ui->file_label, 120);
    widen(filter_name && *filter_name ? filter_name : "Files", name, 120);
    widen(filter && *filter ? filter : "*.*", pattern, 60);
    memset(ui->file_filter, 0, sizeof ui->file_filter);
    wcscpy(ui->file_filter + k, name);
    k += wcslen(name) + 1;
    wcscpy(ui->file_filter + k, pattern);
    k += wcslen(pattern) + 1;
    wcscpy(ui->file_filter + k, L"All files (*.*)");
    k += wcslen(L"All files (*.*)") + 1;
    wcscpy(ui->file_filter + k, L"*.*");
    ui->file_path = path;
    ui->file_n = n;
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

void xr_setup_ui_message(XrSetupUi *ui, int error, const char *msg)
{
    wchar_t w[2048];
    if (xr_no_dialogs() || (ui && ui->closed && !ui->in_form))
        return;                         /* (the player quit during the installation: no "... failed" after it) */
    widen(msg, w, 2048);
    MessageBoxW(ui && ui->win ? ui->win : NULL, w, ui ? ui->title : L"Setup",
                MB_OK | (error ? MB_ICONSTOP : MB_ICONWARNING));
}

void xr_setup_ui_close(XrSetupUi *ui)
{
    if (!ui)
        return;
    if (ui->win)
        DestroyWindow(ui->win);
    if (ui->font) DeleteObject(ui->font);
    if (ui->bold) DeleteObject(ui->bold);
    if (ui->marlett) DeleteObject(ui->marlett);
    if (ui->face) DeleteObject(ui->face);
    free(ui);
}
