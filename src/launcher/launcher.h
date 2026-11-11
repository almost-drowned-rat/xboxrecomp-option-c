/*
 * launcher.h -- the one exe a player downloads (porting guide M7, "Release";
 * THPS docs/release_plan.md). It carries no game code and no game data: the
 * port's own code (prebuilt libraries, its sources), the lifter with an
 * embedded Python, licences - as a zip appended to the exe (the payload) - and
 * builds the game on the player's machine at the first start:
 *
 *   folders + READMEs -> the game (installer; "Choose disc image..." when
 *   none) -> unpack the payload -> unpack the shipped compiler (SHA-256) ->
 *   lift -> compile -> link -> start the game.
 *
 * Nothing is downloaded: the release carries everything, the first start runs
 * offline (maintainer, 2026-11-10). An AppImage carries the payload already
 * unpacked, read-only (usr/share/<slug>/tools); its writable folder is the
 * one that holds the .AppImage (platform/host_paths.h).
 *
 * Later starts start the game at once (a stamp: release + XBE SHA-1);
 * `--repair` builds again. Everything stays inside the install folder. What to
 * run is data in the payload (lift.txt, build.txt, toolchain.txt, made by the
 * port's release script from its real build), so this file is the same for
 * every port; the port gives only its name, versions and icon (XrLauncherConfig).
 * SDL3 only (processes, dialogs, file system) - no OS code here, except on
 * Android, where SDL3 has no process support: there the steps run through
 * fork + execv, toolchain.txt may name programs in the app's native library
 * folder ({NATIVE}, from XR_NATIVE_DIR; files elsewhere cannot be executed
 * since API 29), and the game is a library the app loads after
 * xr_launcher_main returned 0 (no process is started).
 */
#ifndef XR_LAUNCHER_H
#define XR_LAUNCHER_H

#include <stddef.h>
#include <stdint.h>

#include "game_setup.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *game;                   /* "THPS 2X": the name in the messages */
    const char *disc;                   /* "THPS 2X (USA)": what the player has to provide */
    const char *window_title;           /* "THPS 2X Recomp" */
    const char *game_exe;               /* "THPS2X.exe": built next to the launcher ("THPS2X" off Windows) */
    uint32_t    title_id;               /* the installer's checks */
    const xr_game_version *versions;
    unsigned    version_count;
    const void *icon_bmp;               /* the port's window icon (xbox_set_window_icon) */
    size_t      icon_bmp_size;
    /* last, so older positional initializers stay valid (NULL): "panzer-dragoon-orta-recomp" - an AppImage's
     * usr/share/<slug>/tools holds the launcher's files, read-only (NULL: no such folder; unused on Windows) */
    const char *slug;
    /* optional, last: the file dialog answered with a URI, not a path (Android: content://...). The port turns it
     * into a path open() can read (Android: the fd of ContentResolver.openFileDescriptor, detached, as
     * "/proc/self/fd/N") and returns 1, or 0 when it cannot. The image is only read, never deleted or moved.
     * NULL: a URI pick is refused with a log line. */
    int (*open_uri)(const char *uri, char *path, size_t path_n);
} XrLauncherConfig;

/* The whole launcher; returns the process exit code. */
int xr_launcher_main(const XrLauncherConfig *cfg, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif
