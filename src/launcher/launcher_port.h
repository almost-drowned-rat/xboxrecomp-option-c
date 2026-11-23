/* launcher_port.h - the port's whole launcher in one call (2026-11-16; from Panzer Dragoon Orta's launcher/main.c,
 * setup_win32.c and setup_ui_sdl.c, Orta Recompiled - the maintainer's order 2026-11-15, step 4: the launcher
 * UI into the toolkit).
 *
 * xr_launcher_port_main (since 2026-11-23 in Option P's designs, the maintainer: every project the same - option-p's
 * windows, Windows 95 look on Windows (port_setup_ui_win95.c), Windows 2000 look on Linux (port_setup_ui_win2000_sdl.c)):
 *   - Windows / Linux, a fresh download started without arguments: the first page (disc image, install folder,
 *     Go / Quit). Go with the launcher's own folder: the toolkit's launcher (xr_launcher_main --image) builds in the
 *     same window (the two lines greyed, the step's text and the bar with its percentage below), then starts the game;
 *     another folder: the launcher (Windows: the exe + tools\bundle\*.zip; Linux: the AppImage) copied there and
 *     started there with --image;
 *   - every other start (an installed folder, an update's rebuild, a game\ folder filled by hand, --repair): the
 *     toolkit's launcher in the same window and look; no game found -> the first page again with a note;
 *   - Android: port_setup_ui_sdl.c (the progress), no first page;
 *   - no Vulkan 1.3 device: the apology box first (xr_vk_check, src/porthost).
 * A port's launcher/main.c is then its XrLauncherConfig + this struct + one call. Link: xbox_launcher, xbox_porthost,
 * and the port UI sources (CMake variable XBOXRECOMP_PORT_LAUNCHER_SOURCES) in the launcher executable itself. */
#ifndef XR_LAUNCHER_PORT_H
#define XR_LAUNCHER_PORT_H

#include "launcher.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const XrLauncherConfig *cfg;        /* names, versions, icon (launcher.h) */
    const char *launcher_exe;           /* the launcher's own file name, Windows ("Panzer Dragoon Orta Recomp.exe") */
    const char *example_dir;            /* a short install folder for the "path too long" hint ("C:\\Games\\Orta") */
    /* media\README.txt the setup writes into a new install (never over an existing one); NULL = none, the toolkit's
     * launcher then writes its own. Orta: only the boot animation, 1080p60 (no soundtracks, no shader module). */
    const char *media_readme;
    const char *android_vulkan_lib;     /* the APK's Vulkan library for the device check (Orta: libvulkan_orta.so) */
} XrPortLauncher;

int xr_launcher_port_main(const XrPortLauncher *p, int argc, char **argv);

/* inside the toolkit */
int  xr_port_vk_gate(const XrPortLauncher *p);
#if !defined(__ANDROID__)
/* inside the toolkit, Windows / Linux (port_setup_ui_win95.c / port_setup_ui_win2000_sdl.c, option-p's windows):
 * the first page (disc image, install folder, Go / Quit: 1 Go, 0 Quit, -1 no window / no dialogs), the window handed
 * on to the toolkit launcher's progress, the port's icon for the window */
struct XrSetupUi;
int  xr_setup_ui_form(struct XrSetupUi *ui, const char *disc_name, char *iso, size_t n, const char *note,
                      const char *extra_label, char *extra, size_t extra_n, char *target, size_t target_n, int *copy);
void xr_setup_ui_icon(struct XrSetupUi *ui, const void *bmp, size_t n);
void xr_port_ui_handover(struct XrSetupUi *ui);
void xr_port_ui_default_icon(const void *bmp, size_t n);
#endif

#ifdef __cplusplus
}
#endif

#endif
