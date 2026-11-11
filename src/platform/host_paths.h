/*
 * host_paths.h - where a port's writable files live and what a restart runs
 * (platform layer, every host).
 *
 *   xbox_host_root()  the WRITABLE install root, UTF-8, ending in the host's
 *                     separator. data/ (saves, logs, caches, the launcher's
 *                     build) lives under it.
 *                       Windows:  the exe's folder (GetModuleFileNameW).
 *                       AppImage: the folder that holds the .AppImage file
 *                                 (dirname $APPIMAGE) - the exe itself sits
 *                                 in the read-only squashfs mount ($APPDIR).
 *                       else:     the exe's folder (/proc/self/exe).
 *   xbox_host_self()  the file a restart executes: $APPIMAGE inside an
 *                     AppImage, else the running exe.
 *
 * Read-only resources shipped with the program (shaders, the launcher's
 * tools inside an AppImage) stay at SDL_GetBasePath() - inside an AppImage
 * that is the mount, which is right for reading and wrong for writing.
 *
 * $APPIMAGE / $APPDIR are only believed when the running exe really lies
 * under $APPDIR: a program started FROM an AppImage inherits both variables
 * and must still find its own folder. Both results are computed once and
 * never change for the life of the process; NULL never comes back (an empty
 * string when the host cannot tell, which the caller treats as "./").
 */

#ifndef HOST_PATHS_H
#define HOST_PATHS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *xbox_host_root(void);
const char *xbox_host_self(void);

/* The rule itself, without the process state (tests/host_paths): exe = the
 * running exe's absolute path, appimage / appdir = the environment (NULL when
 * unset). Writes the root (with a trailing separator) and the restart file.
 * Returns 1 when the AppImage rule applied, 0 otherwise. */
int xbox_host_paths_resolve(const char *exe, const char *appimage, const char *appdir,
                            char *root, size_t root_n, char *self, size_t self_n);

#ifdef __cplusplus
}
#endif

#endif
