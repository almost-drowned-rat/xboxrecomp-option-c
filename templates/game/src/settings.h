/*
 * settings.h -- the port's settings file, saves\settings.ini (CLAUDE.md:
 * "one settings file in saves/", shared by every RECOMP OPTIONS line and the
 * game's own extra options). From THPS 2X (src/settings.c), made generic.
 *
 * Format: one "key=value" per line, UTF-8; lines starting with # or ; and
 * unknown keys are kept as they are. Written atomically like the saves: the
 * new file goes to saves\.pending\ first and replaces the old one in one
 * move (host_write_file_atomic), so an interrupted write leaves the previous
 * file. Every setting can be overridden for one run with @PREFIX@_<KEY>
 * (key upper-cased), e.g. @PREFIX@_ASPECT_RATIO; an override is not written
 * back.
 */
#ifndef @PREFIX@_SETTINGS_H
#define @PREFIX@_SETTINGS_H

void        settings_init(const char *save_dir);
/* The value (env override first), or def when unset. */
const char *settings_get(const char *key, const char *def);
/* Sets and writes the file at once (no-op when the value is unchanged). */
void        settings_set(const char *key, const char *value);

#endif
