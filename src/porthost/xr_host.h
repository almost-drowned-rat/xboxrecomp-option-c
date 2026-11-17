/* xr_host.h - the port executable's host platform layer (process, paths, log, crash handling, the MMIO fault
 * routing to the APU / OHCI traps), shared by every port (library xbox_porthost).
 *
 * Everything OS-specific of the game executable lives behind this header (porting guide, Portability). Windows:
 * host_win32.c; Linux + Android: host_linux.c. Moved into the toolkit 2026-11-16 from Panzer Dragoon Orta's
 * src/platform (Orta Recompiled; the maintainer's order 2026-11-15, step 4), names taken from host_set_port.
 * A port that still has its own src/platform/host_*.c keeps working; switching = include "xr_host.h", link
 * xbox_porthost, call host_set_port first, delete the copies.
 */
#ifndef XR_HOST_H
#define XR_HOST_H

#include <stddef.h>

#define HOST_PATH_MAX 1024

/* The port's names, before host_init: the dialog / window title ("Panzer Dragoon Orta Recomp"), the log file's
 * base name (<data>/<log_name>.log, the previous run as <log_name>.previous.log; on Android also the logcat tag)
 * and the prefix of the port's debug variables (<prefix>_WATCH_WRITE, _WATCH_MAX, _LOG_RAW). NULL / "" keeps a
 * default ("Xbox Recomp", "game", "RECOMP_PORT"). */
void host_set_port(const char *title, const char *log_name, const char *env_prefix);
const char *host_port_title(void);
const char *host_port_log_name(void);
const char *host_port_env_prefix(void);
/* getenv("<prefix>_<suffix>") */
const char *host_port_getenv(const char *suffix);

typedef struct HostOptions {
    char exe_dir[HOST_PATH_MAX];   /* folder of the executable, no trailing separator */
    char game_dir[HOST_PATH_MAX];  /* extracted disc: default.xbe + data folders */
    char save_dir[HOST_PATH_MAX];  /* Xbox U: (and partition images) */
    char data_dir[HOST_PATH_MAX];  /* settings, caches, logs, crash dumps */
    double max_seconds;            /* --max-seconds: exit after this long, 0 = no limit */
} HostOptions;

/* Parse the command line (--max-seconds, --game-dir, --save-dir, --data-dir,
 * --console, --input-script <file>, --input-live <file>), create the layout folders, redirect stdout/stderr
 * into <data>/<log_name>.log (previous run kept as <log_name>.previous.log), install
 * the crash handler and arm the time limit. Returns 0 on success. */
int host_init(int argc, char **argv, HostOptions *opt);

/* Read a whole file; caller frees with free(). NULL on failure. */
void *host_read_file(const char *path, size_t *out_size);

/* Write a whole file atomically: into pending_dir (created) first, then
 * moved over path in one step (REPLACE_EXISTING | WRITE_THROUGH), like the
 * saves -- an interrupted write leaves the old file. 1 on success
 * (settings.c). */
int host_write_file_atomic(const char *path, const char *pending_dir, const void *data, size_t n);

/* Set an environment variable unless the user already set it (runtime
 * defaults that a test run can still override). */
void host_setenv_default(const char *name, const char *value);
/* Set (overwrite) a variable; "" removes it. */
void host_setenv(const char *name, const char *value);

/* Sleep the calling thread (debug aids only, e.g. a port's race-delay switch in recomp_manual.c). */
void host_sleep_ms(unsigned ms);

/* A recursive host mutex (recomp_manual.c: the CRI pass / destroy lock). take: 1 when held, 0 after ms without
 * getting it. */
void *host_mutex_new(void);
int   host_mutex_take(void *m, unsigned ms);
void  host_mutex_give(void *m);

/* Start the exe again with the same arguments and the environment it started
 * with, then end this process (RECOMP OPTIONS: RESTART NOW, THPS 2X model).
 * --input-script / --input-live are left out. Returns only when the new
 * process could not be started. */
void host_restart(void);
/* 1 when this process was started by host_restart (--restart-wait): the player just chose the settings. */
int host_was_restarted(void);

/* Debugging aids that need the guest memory mapped (<prefix>_WATCH_WRITE, Windows). */
void host_debug_start(void);

/* End the process now (window closed): log drained, exit code 0. */
void host_quit(void);

/* Fatal error: logged and shown to the user. */
void host_fatal(const char *msg);
/* Information for the player (not an error): logged, and shown in a dialog unless RECOMP_NO_DIALOGS is set.
 * Blocks the calling thread until the player closes it. */
void host_notice(const char *msg);

#endif
