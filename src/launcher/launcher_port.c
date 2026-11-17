/* launcher_port.c - xr_launcher_port_main (launcher_port.h): a port launcher's main, from Panzer Dragoon Orta's
 * launcher/main.c (Orta Recompiled), moved into the toolkit on 2026-11-16 (maintainer's order
 * 2026-11-15, step 4). Built only into the port's launcher executable (XBOXRECOMP_PORT_LAUNCHER_SOURCES), with
 * port_setup_win32.c (Windows) or port_setup_ui_sdl.c (Linux, Android).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "launcher_port.h"
#include "platform/host_paths.h"   /* xbox_host_root: the writable folder (beside the .AppImage, or the exe's folder) */
#include "xr_host.h"               /* host_set_port: the title in the Vulkan apology */
#include "xr_vk_check.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
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
#endif

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

int xr_launcher_port_main(const XrPortLauncher *p, int argc, char **argv)
{
    int parent = 0;
#ifdef _WIN32
    int i;
#endif
    if (!p || !p->cfg)
        return 1;
    host_set_port(p->cfg->window_title, NULL, NULL);   /* "Panzer Dragoon Orta Recomp" in the apology, as before */
#ifdef _WIN32
    for (i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--setup-parent")) {
            xr_port_setup_set_parent(strtoull(argv[i + 1], NULL, 10));
            parent = 1;
        }
    if (argc <= 1 && !folder_in_use())
        return xr_port_setup_window(p);
#endif
    /* the toolkit's launcher; an installed folder only starts the game exe, which checks Vulkan itself */
    if (!parent && !folder_in_use() && !xr_port_vk_gate(p))
        return 1;
    return xr_launcher_main(p->cfg, argc, argv);
}
