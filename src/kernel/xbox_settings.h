/*
 * xbox_settings.h - the console's system settings as a title sees them, one table per port.
 *
 * On the console they live in the EEPROM (dashboard settings, factory region) and reach a title through
 * ExQueryNonVolatileSetting, the AV pack the HAL / SMC report, and raw SMBus reads of the EEPROM. All of those
 * read this one table, so they can never disagree. The table starts with the toolkit's defaults (the answers it
 * always gave: US English, NTSC-M, no video flags as the title sees them, region fields not reported); a port sets
 * its own per disc variant with xbox_settings_set_port() before the title starts; RECOMP_XBOX_* environment
 * variables override single values on top (maintainer, 2026-10-24; settings file and menu: later).
 *
 * NTSC-M is the standard AV region (maintainer, 2026-11-01; every port): a port answers av_region =
 * XBOX_AV_REGION_NTSC_M (60 Hz) also for a PAL disc - in PAL mode titles switch to their PAL TV layout (Panzer
 * Dragoon Orta PAL: 580 of 640 columns); game region and languages stay the disc's own. Only a PAL title that
 * misbehaves under NTSC-M keeps PAL (checked in M1) and then the PAL60 rule:
 * PAL60 rule (maintainer, 2026-10-24): a PAL title that supports PAL60 always runs at 60 Hz - the port sets
 * pal60_only, and no setting or override can turn it back to 50 Hz. A title without PAL60 keeps 50 Hz.
 */
#ifndef XBOX_SETTINGS_H
#define XBOX_SETTINGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* XC_LANGUAGE values */
enum { XBOX_LANG_ENGLISH = 1, XBOX_LANG_JAPANESE, XBOX_LANG_GERMAN, XBOX_LANG_FRENCH, XBOX_LANG_SPANISH,
       XBOX_LANG_ITALIAN, XBOX_LANG_KOREAN, XBOX_LANG_CHINESE, XBOX_LANG_PORTUGUESE };
#define XBOX_LANG_BIT(l) (1u << (l))

/* XC_VIDEO: the EEPROM word. XAPI's XGetVideoFlags returns it shifted right by 16. */
#define XBOX_VIDEO_WIDESCREEN   0x00010000u
#define XBOX_VIDEO_HDTV_720P    0x00020000u
#define XBOX_VIDEO_HDTV_1080I   0x00040000u
#define XBOX_VIDEO_HDTV_480P    0x00080000u
#define XBOX_VIDEO_LETTERBOX    0x00100000u
#define XBOX_VIDEO_PAL_60HZ     0x00400000u

/* XC_FACTORY_AV_REGION (0x103): standard in bits 8-15, the refresh it implies in 0x00C00000 */
#define XBOX_AV_REGION_NTSC_M   0x00400100u
#define XBOX_AV_REGION_NTSC_J   0x00400200u
#define XBOX_AV_REGION_PAL_I    0x00800300u

/* XC_FACTORY_GAME_REGION (0x104) */
#define XBOX_GAME_REGION_NA     0x00000001u
#define XBOX_GAME_REGION_JAPAN  0x00000002u
#define XBOX_GAME_REGION_ROW    0x00000004u   /* rest of the world: Europe, Australia */

typedef struct XboxSettings {
    uint32_t language;        /* XC_LANGUAGE, if no host language applies (the disc's own fallback) */
    uint32_t languages;       /* XBOX_LANG_BIT set of the languages the disc has; 0: always `language` */
    uint32_t video;           /* XC_VIDEO, the EEPROM word */
    uint32_t av_region;       /* XC_FACTORY_AV_REGION; 0: not reported (index unhandled, as before) */
    uint32_t game_region;     /* XC_FACTORY_GAME_REGION; 0: not reported (as before) */
    uint32_t dvd_region;      /* XC_DVD_REGION */
    int32_t  timezone_bias;   /* XC_TIMEZONE_BIAS, minutes */
    int      pal60_only;      /* PAL60 rule: a PAL title that supports PAL60 never runs at 50 Hz */
} XboxSettings;

/* The port's values for the disc variant it runs (call before the title starts; later calls replace them). */
void xbox_settings_set_port(const XboxSettings *s);

/* The port's XC_VIDEO word alone, at any time (a RECOMP OPTIONS line: ASPECT RATIO writes XBOX_VIDEO_WIDESCREEN).
 * Every later query of the title answers it - XQueryValue, the EEPROM image, the AV capabilities word - after the
 * RECOMP_XBOX_VIDEO override and the PAL60 rule, as at the start. The window's 16:9 (xbox_video_widescreen) keeps
 * the value of the start: a title that read its video flags once still draws what it read, and a restart-bound
 * option must not reshape the window under it. The port saves the value and sets it again before the next boot
 * (RESTART NOW); xbox_settings_set_port before the boot does the same. Before xbox_settings_set_port, the
 * toolkit's defaults with this word. */
void xbox_settings_set_video(uint32_t video);

/* The values in effect: the port's (or the defaults) with the RECOMP_XBOX_* overrides and the PAL60 rule
 * applied, the language resolved against the host's UI language. */
const XboxSettings *xbox_settings(void);

/* What the HAL / SMC report as the AV pack word: pack | standard << 8 | refresh (AV_OPTION_QUERY_AVPACK). */
uint32_t xbox_settings_avpack_word(void);

/* One byte of the EEPROM image at `offset` (raw SMBus reads of slave 0xA8): the factory AV region and the user
 * section's language, video and DVD region from the table; everything else 0 (the encrypted section, which
 * holds the game region on the console, is not modelled). */
uint8_t xbox_settings_eeprom_byte(uint32_t offset);

/* Tests: forget the port's values and the resolved table. */
void xbox_settings_reset(void);

/* Settings query log (maintainer, 2026-10-25: verify in M1 that a title reads and accepts the values): the first
 * read of each index on each path is logged once to stderr - "[SETTINGS] <path> index 0x.. = value (status)
 * caller 0x<guest return address> (first read)". Returns 1 when this call logged. Index per path: the ExQuery
 * value index, the AvSendTVEncoderOption option, the SMBus EEPROM offset. */
enum { XBOX_SETTINGS_PATH_EXQUERY, XBOX_SETTINGS_PATH_AVOPTION, XBOX_SETTINGS_PATH_EEPROM, XBOX_SETTINGS_PATHS };
int xbox_settings_log_read(int path, uint32_t index, uint32_t value, uint32_t status, uint32_t caller);
void xbox_settings_log_reset(void);   /* tests */

#ifdef __cplusplus
}
#endif

#endif /* XBOX_SETTINGS_H */
