/*
 * host_info.h - which build runs where (platform layer), for the first lines
 * of every port's log:
 *
 *   version: v0.9.3.1 (game ae51948, toolkit ed5ca9a)
 *   host: Wine 9.0 (wine-9.0 Proton...) on Linux 6.1.52-valve16; Steam Deck, Gamescope
 *
 * The release name comes from <install root>\tools\release.txt (written by the
 * release launcher when it unpacks its files), else <install root>/data/release.txt
 * (the launcher writes it after a build from a read-only bundle: an AppImage);
 * "dev build" without either. The
 * commits from the build (cmake/build_info.cmake: the game repo and the
 * toolkit, "-dirty" with uncommitted changes). The host line names Windows
 * with its build, or Wine / Proton with the Unix system under it, plus what
 * the environment Wine passes through tells: Steam Deck (SteamDeck=1),
 * Gamescope (GAMESCOPE_WAYLAND_DISPLAY) or the desktop (XDG_CURRENT_DESKTOP).
 */

#ifndef HOST_INFO_H
#define HOST_INFO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "v0.9.3.1 (game ae51948, toolkit ed5ca9a)"; game_dir = the install's game
 * folder (its parent holds tools\release.txt) */
void host_version_text(const char *game_dir, char *buf, size_t n);
/* "Windows 10.0.26300 x64" / "Wine 9.0 (...) on Linux 6.x; Steam Deck, Gamescope" */
void host_describe(char *buf, size_t n);
/* "AMD Ryzen 5 5600H with Radeon Graphics, 6 cores / 12 threads", "15.4 GB (9.2 GB free)", "Balanced"
 * (2026-11-22, the maintainer's log wishlist; Windows and Wine - native Linux / Android: empty here) */
void host_describe_machine(char *cpu, size_t ncpu, char *ram, size_t nram, char *power, size_t npower);
/* the lines "version: ...", "host: ..." and, where known, "cpu: ...", "ram: ...", "power plan: ..." on stdout */
void host_log_version_and_host(const char *game_dir);

#ifdef __cplusplus
}
#endif

#endif
