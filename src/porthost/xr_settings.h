/* xr_settings.h - the port's saves/settings.ini (key=value lines, unknown lines kept as they are). 2026-11-16: from
 * Panzer Dragoon Orta's src/settings.c (= THPS 2X src/settings.c via templates/game, option-c b7d907c), moved into
 * the toolkit (maintainer's order 2026-11-15, step 4); the env prefix is host_set_port's (xr_host.h).
 *
 * xr_settings_get(key, def): the environment "<PREFIX>_<KEY>" (key upper-cased) wins, then the file, then def.
 * xr_settings_set writes the whole file atomically (host_write_file_atomic via saves/.pending), only on a change.
 * A port switching over: call host_set_port first, then xr_settings_init(save_dir); delete its own settings.c (or
 * keep its settings_* names as one-line wrappers). Not thread-safe: call from the game's main / menu thread. */
#ifndef XR_SETTINGS_H
#define XR_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

void        xr_settings_init(const char *save_dir);
const char *xr_settings_get(const char *key, const char *def);
void        xr_settings_set(const char *key, const char *value);

#ifdef __cplusplus
}
#endif

#endif
