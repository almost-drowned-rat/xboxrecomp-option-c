/* launcher_port.c - xr_launcher_port_main (launcher_port.h): a port launcher's main, from Panzer Dragoon Orta's
 * launcher/main.c (Orta Recompiled), moved into the toolkit on 2026-11-16 (maintainer's order
 * 2026-11-15, step 4). Built only into the port's launcher executable (XBOXRECOMP_PORT_LAUNCHER_SOURCES), with
 * option-p's launcher windows (the maintainer 2026-11-23: every project in Option P's designs) -
 * port_setup_ui_win95.c (Windows) or port_setup_ui_win2000_sdl.c (Linux) - or port_setup_ui_sdl.c (Android).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE                 /* realpath */
#endif
#include "launcher_port.h"
#include "platform/host_paths.h"   /* xbox_host_root: the writable folder (beside the .AppImage, or the exe's folder) */
#include "xr_host.h"               /* host_set_port: the title in the Vulkan apology */
#include "xr_vk_check.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__ANDROID__)
#include "game_setup_ui.h"
#ifdef _WIN32
#include <stdlib.h>                     /* _fullpath */
#else
#include <limits.h>
#include <sys/stat.h>                   /* chmod: the copied AppImage */
#endif
#endif

/* 1 when a Vulkan 1.3 device is there, else the apology (stderr, and a box unless RECOMP_NO_DIALOGS=1) and 0 */
int xr_port_vk_gate(const XrPortLauncher *p)
{
    char found[1024], msg[1600];
    const char *nd = getenv("RECOMP_NO_DIALOGS");
    if (xr_vk_check(found, sizeof found, p ? p->android_vulkan_lib : NULL))
        return 1;
    xr_vk_message(msg, sizeof msg, found);
    fprintf(stderr, "%s\n", msg);
    if (!(nd && *nd == '1'))
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, p && p->cfg ? p->cfg->window_title : "Recomp", msg, NULL);
    return 0;
}

/* 1 when the install folder is (being) used as a game folder already: then the toolkit's launcher takes over */
static int folder_in_use(void)
{
    const char *base = xbox_host_root();   /* = SDL_GetBasePath() on Windows; beside the .AppImage on Linux */
    char p[1024];
    SDL_PathInfo info;
    if (!base || !*base)
        return 1;
    snprintf(p, sizeof p, "%sdata/build/stamp.txt", base);
    if (SDL_GetPathInfo(p, &info))
        return 1;                       /* installed: start the game */
    snprintf(p, sizeof p, "%sgame/default.xbe", base);
    if (SDL_GetPathInfo(p, &info))
        return 1;                       /* extracted disc files put into game\ by hand */
    snprintf(p, sizeof p, "%sgame", base);
    if (SDL_GetPathInfo(p, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
        /* an .iso put into game\ by hand (the toolkit's way): any file other than README.txt counts */
        char **files = SDL_GlobDirectory(p, "*", 0, NULL);
        int used = 0, i;
        for (i = 0; files && files[i]; i++)
            if (SDL_strcasecmp(files[i], "README.txt") != 0)
                used = 1;
        SDL_free(files);
        if (used)
            return 1;
    }
    return 0;
}

#if !defined(__ANDROID__)
/* ── Windows + Linux: the first page (option-p's launcher windows, the maintainer 2026-11-23) ───────────────────── */

#define FP 4096

static int is_sep(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

static void strip_slashes(char *s)
{
    size_t k = strlen(s);
    while (k > 1 && is_sep(s[k - 1]) && !(k == 3 && s[1] == ':'))
        s[--k] = 0;
}

static int same_dir(const char *a, const char *b)
{
    char ra[FP], rb[FP];
#ifdef _WIN32
    if (_fullpath(ra, a, sizeof ra) && _fullpath(rb, b, sizeof rb)) {
        strip_slashes(ra);
        strip_slashes(rb);
        return !SDL_strcasecmp(ra, rb);
    }
    return !SDL_strcasecmp(a, b);
#else
    if (realpath(a, ra) && realpath(b, rb))
        return !strcmp(ra, rb);
    return !strcmp(a, b);
#endif
}

static void join2(char *out, size_t n, const char *a, const char *b)
{
    size_t k = strlen(a);
    snprintf(out, n, "%s%s%s", a, k && is_sep(a[k - 1]) ? "" : "/", b);
}

/* a folder chosen that is neither empty nor one of ours: the game goes into a new subfolder there (as the old
 * Windows setup window's folder picker did) */
static void into_subfolder(char *target, size_t n, const char *title)
{
    char **files = SDL_GlobDirectory(target, "*", 0, NULL);
    char probe[FP];
    SDL_PathInfo info;
    int empty = !files || !files[0];
    SDL_free(files);
    join2(probe, sizeof probe, target, "data");
    if (empty || SDL_GetPathInfo(probe, &info))
        return;
    if (strlen(target) + 1 + strlen(title) < n) {
        strcat(target, "/");
        strcat(target, title);
    }
}

/* the port's media/README.txt in a new install (never over an existing one; NULL: the launcher writes its own) */
static void write_media_readme(const XrPortLauncher *p, const char *target)
{
    char dir[FP], file[FP];
    SDL_PathInfo info;
    SDL_IOStream *io;
    if (!p->media_readme || !*p->media_readme)
        return;
    join2(dir, sizeof dir, target, "media");
    SDL_CreateDirectory(dir);
    join2(file, sizeof file, dir, "README.txt");
    if (SDL_GetPathInfo(file, &info))
        return;
    if ((io = SDL_IOFromFile(file, "wb"))) {
        SDL_WriteIO(io, p->media_readme, strlen(p->media_readme));
        SDL_CloseIO(io);
    }
}

#ifdef _WIN32
/* the release zip's tools\bundle\*.zip (the pinned compiler, so the first start needs no internet) goes along into
 * another install folder; a failed copy ends in the launcher's "package is incomplete" message there */
static void copy_bundle(const char *self_dir, const char *target)
{
    char from_dir[FP], to_dir[FP], from[FP], to[FP];
    char **files;
    int i;
    join2(from_dir, sizeof from_dir, self_dir, "tools/bundle");
    files = SDL_GlobDirectory(from_dir, "*.zip", 0, NULL);
    if (!files)
        return;
    join2(to_dir, sizeof to_dir, target, "tools/bundle");
    SDL_CreateDirectory(to_dir);
    for (i = 0; files[i]; i++) {
        join2(from, sizeof from, from_dir, files[i]);
        join2(to, sizeof to, to_dir, files[i]);
        if (!SDL_CopyFile(from, to))
            fprintf(stderr, "[LAUNCHER] cannot copy %s to %s: %s\n", from, to, SDL_GetError());
    }
    SDL_free(files);
}
#endif

/* this launcher (Windows: the exe + the compiler zip; Linux: the AppImage) copied into the chosen folder and started
 * there with the image; 1 = started */
static int install_elsewhere(const XrPortLauncher *p, struct XrSetupUi *ui, const char *self, const char *target,
                             const char *iso)
{
    const char *base = strrchr(self, '/');
    char to[FP], *args[4];
    SDL_PropertiesID props;
    SDL_Process *proc;
#ifdef _WIN32
    const char *bs = strrchr(self, '\\');
    if (bs > base)
        base = bs;
    if (strlen(target) > 200) {
        char m[300];
        snprintf(m, sizeof m, "The install folder's path is too long - please choose a shorter one (for example %s).",
                 p->example_dir ? p->example_dir : "C:\\Games\\Recomp");
        xr_setup_ui_message((XrSetupUi *)ui, 1, m);
        return 0;
    }
#endif
    if (!SDL_CreateDirectory(target)) {
        xr_setup_ui_message((XrSetupUi *)ui, 1, "The install folder cannot be made there - please choose another one.");
        return 0;
    }
    join2(to, sizeof to, target, base ? base + 1 : self);
    if (!SDL_CopyFile(self, to)) {
        fprintf(stderr, "[LAUNCHER] cannot copy %s to %s: %s\n", self, to, SDL_GetError());
        xr_setup_ui_message((XrSetupUi *)ui, 1, "The game cannot be copied into the install folder (full, no right to "
                                                "write there, or the game there still running?) - please choose another "
                                                "one.");
        return 0;
    }
#ifdef _WIN32
    {
        char self_dir[FP];
        snprintf(self_dir, sizeof self_dir, "%s", self);
        if (base)
            self_dir[base - self] = 0;
        copy_bundle(self_dir, target);
    }
#else
    chmod(to, 0755);                    /* (the copy a program again) */
#endif
    write_media_readme(p, target);
    args[0] = to;
    args[1] = "--image";
    args[2] = (char *)iso;
    args[3] = NULL;
    props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args);
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, target);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    proc = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!proc) {
        fprintf(stderr, "[LAUNCHER] cannot start %s: %s\n", to, SDL_GetError());
        xr_setup_ui_message((XrSetupUi *)ui, 1, "The game copied into the install folder does not start - start it "
                                                "there yourself.");
        return 0;
    }
    fprintf(stderr, "[LAUNCHER] installing in %s: started %s\n", target, to);
    SDL_DestroyProcess(proc);
    return 1;
}

/* the toolkit launcher found no game: the page again, with a note, the folder fixed (launcher.h image page) */
static int image_page(void *ui, const char *disc, char *iso, size_t n, const char *note)
{
    int copy = 1;
    return xr_setup_ui_form((struct XrSetupUi *)ui, disc, iso, n, note, NULL, NULL, 0, NULL, 0, &copy);
}

/* a fresh start without arguments: the page; Go in this folder -> the toolkit launcher in the same window, another
 * folder -> this launcher copied there and started there; no window (no display, RECOMP_NO_DIALOGS) -> as before */
static int first_page(const XrPortLauncher *p, int argc, char **argv)
{
    struct XrSetupUi *ui;
    static char iso[FP], target[FP], root[FP];
#ifdef _WIN32
    const char *self = xbox_host_self();
#else
    const char *self = getenv("APPIMAGE");     /* only the AppImage is one file that can be copied */
#endif
    const char *note = NULL;
    int copy = 1;
    if (!xr_port_vk_gate(p))
        return 1;
    snprintf(root, sizeof root, "%s", xbox_host_root());
    strip_slashes(root);
    snprintf(target, sizeof target, "%s", root);
    ui = (struct XrSetupUi *)xr_setup_ui_open(p->cfg->window_title);
    xr_setup_ui_icon(ui, p->cfg->icon_bmp, p->cfg->icon_bmp_size);
    for (;;) {
        int rc = xr_setup_ui_form(ui, p->cfg->disc, iso, sizeof iso, note, NULL, NULL, 0, target, sizeof target, &copy);
        if (rc < 0) {
            xr_setup_ui_close((XrSetupUi *)ui);
            return xr_launcher_main(p->cfg, argc, argv);
        }
        if (rc == 0) {
            xr_setup_ui_close((XrSetupUi *)ui);
            return 0;
        }
        strip_slashes(target);
        if (same_dir(target, root)) {
            char *args[4];
            write_media_readme(p, root);
            args[0] = argc > 0 ? argv[0] : (char *)"launcher";
            args[1] = "--image";
            args[2] = iso;
            args[3] = NULL;
            xr_port_ui_handover(ui);    /* the page's window shows the build */
            return xr_launcher_main(p->cfg, 3, args);
        }
        if (!self || !*self) {
            /* the folder download (tar.gz) installs where it lies: its tools are beside the launcher */
            xr_setup_ui_message((XrSetupUi *)ui, 0, "This download installs into its own folder. To play from another "
                                                    "folder, move the whole folder there first (or use the AppImage).");
            snprintf(target, sizeof target, "%s", root);
            continue;
        }
        into_subfolder(target, sizeof target, p->cfg->window_title);
        if (install_elsewhere(p, ui, self, target, iso)) {
            xr_setup_ui_close((XrSetupUi *)ui);
            return 0;
        }
        note = "That did not work.";
    }
}
#endif

int xr_launcher_port_main(const XrPortLauncher *p, int argc, char **argv)
{
    if (!p || !p->cfg)
        return 1;
    host_set_port(p->cfg->window_title, NULL, NULL);   /* "Panzer Dragoon Orta Recomp" in the apology, as before */
#if !defined(__ANDROID__)
    xr_port_ui_default_icon(p->cfg->icon_bmp, p->cfg->icon_bmp_size);
    xr_launcher_set_image_page(image_page);
    if (argc <= 1 && !folder_in_use())
        return first_page(p, argc, argv);
#endif
    /* the toolkit's launcher; an installed folder only starts the game exe, which checks Vulkan itself */
    if (!folder_in_use() && !xr_port_vk_gate(p))
        return 1;
    return xr_launcher_main(p->cfg, argc, argv);
}
