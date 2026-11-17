/* launcher_port.h - the port's whole launcher in one call (2026-11-16; from Panzer Dragoon Orta's launcher/main.c,
 * setup_win32.c and setup_ui_sdl.c, Orta Recompiled - the maintainer's order 2026-11-15, step 4: the launcher
 * UI into the toolkit).
 *
 * xr_launcher_port_main does what Orta's launcher main did:
 *   - Windows, a fresh download started without arguments: the setup window (port_setup_win32.c, the Windows 95 look
 *     of my other ports' launchers: disc image, install folder, Go -> Play); Go copies the launcher into the install folder and
 *     runs the copy there with --image / --no-start / --setup-parent, whose progress shows in the same window;
 *   - every other start (an installed folder, a game\ folder filled by hand, --repair, the setup's copy): the
 *     toolkit's launcher (xr_launcher_main) with its progress in the same look (Windows: port_setup_win32.c, Linux
 *     and Android: port_setup_ui_sdl.c - both replace game_setup_ui.c's xr_setup_ui_* functions);
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

/* inside the toolkit: the setup window and its parent (port_setup_win32.c, Windows) */
int  xr_port_setup_window(const XrPortLauncher *p);
void xr_port_setup_set_parent(unsigned long long hwnd);
int  xr_port_vk_gate(const XrPortLauncher *p);

#ifdef __cplusplus
}
#endif

#endif
