/*
 * xbox_settings.c - the console's system settings, one table per port (xbox_settings.h).
 *
 * Overrides (each optional; numbers decimal or 0x-hex):
 *   RECOMP_XBOX_LANGUAGE     XC_LANGUAGE (1 English .. 9 Portuguese), instead of the host-language choice
 *   RECOMP_XBOX_VIDEO        XC_VIDEO, the EEPROM word (0x00010000 widescreen, 0x00400000 PAL60, ...)
 *   RECOMP_XBOX_AV_REGION    XC_FACTORY_AV_REGION (0x00400100 NTSC-M, 0x00800300 PAL-I)
 *   RECOMP_XBOX_GAME_REGION  XC_FACTORY_GAME_REGION (1 NA, 2 Japan, 4 rest of the world)
 *   RECOMP_XBOX_DVD_REGION   XC_DVD_REGION
 * Under the PAL60 rule (pal60_only) a PAL standard always gets the PAL60 flag, whatever an override says.
 */
#include "xbox_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include "kernel.h"

/* The toolkit's answers before per-port settings: English, no video flags as a title sees them (the old word
 * 0x03 had widescreen/HDTV in the bits XGetVideoFlags shifts out, so titles saw 0), region fields not reported,
 * DVD region 1, Eastern time. Ports that never call xbox_settings_set_port (THPS 2X today) keep exactly that. */
static const XboxSettings k_defaults = {
    XBOX_LANG_ENGLISH, 0, 0, 0, 0, 1, -300, 0,
};

static XboxSettings s_port;
static int s_have_port;
static XboxSettings s_eff;
static int s_resolved;

void xbox_settings_set_port(const XboxSettings *s)
{
    s_port = *s;
    s_have_port = 1;
    s_resolved = 0;
}

void xbox_settings_set_video(uint32_t video)
{
    if (!s_have_port) {
        s_port = k_defaults;
        s_have_port = 1;
    }
    s_port.video = video;
    s_resolved = 0;                     /* the next query resolves again: overrides and the PAL60 rule apply */
}

void xbox_settings_reset(void)
{
    s_have_port = 0;
    s_resolved = 0;
}

static int env_u32(const char *name, uint32_t *out)
{
    const char *e = getenv(name);
    char *end;
    unsigned long v;
    if (!e || !*e)
        return 0;
    v = strtoul(e, &end, 0);
    if (*end)
        return 0;
    *out = (uint32_t)v;
    return 1;
}

/* The host's UI language as an XC_LANGUAGE value, 0 if it is none of the Xbox's. */
static uint32_t host_language(void)
{
#if defined(_WIN32)
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_ENGLISH:    return XBOX_LANG_ENGLISH;
    case LANG_JAPANESE:   return XBOX_LANG_JAPANESE;
    case LANG_GERMAN:     return XBOX_LANG_GERMAN;
    case LANG_FRENCH:     return XBOX_LANG_FRENCH;
    case LANG_SPANISH:    return XBOX_LANG_SPANISH;
    case LANG_ITALIAN:    return XBOX_LANG_ITALIAN;
    case LANG_KOREAN:     return XBOX_LANG_KOREAN;
    case LANG_CHINESE:    return XBOX_LANG_CHINESE;
    case LANG_PORTUGUESE: return XBOX_LANG_PORTUGUESE;
    default:              return 0;
    }
#else
    static const struct { const char *p; uint32_t l; } k[] = {
        {"en", XBOX_LANG_ENGLISH}, {"ja", XBOX_LANG_JAPANESE}, {"de", XBOX_LANG_GERMAN}, {"fr", XBOX_LANG_FRENCH},
        {"es", XBOX_LANG_SPANISH}, {"it", XBOX_LANG_ITALIAN}, {"ko", XBOX_LANG_KOREAN}, {"zh", XBOX_LANG_CHINESE},
        {"pt", XBOX_LANG_PORTUGUESE}};
    const char *e = getenv("LC_ALL");
    if (!e || !*e) e = getenv("LC_MESSAGES");
    if (!e || !*e) e = getenv("LANG");
    if (!e)
        return 0;
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (!strncmp(e, k[i].p, 2))
            return k[i].l;
    return 0;
#endif
}

const XboxSettings *xbox_settings(void)
{
    if (s_resolved)
        return &s_eff;
    s_eff = s_have_port ? s_port : k_defaults;
    if (s_eff.languages) {
        uint32_t h = host_language();
        if (h && (s_eff.languages & XBOX_LANG_BIT(h)))
            s_eff.language = h;
    }
    env_u32("RECOMP_XBOX_LANGUAGE", &s_eff.language);
    env_u32("RECOMP_XBOX_VIDEO", &s_eff.video);
    env_u32("RECOMP_XBOX_AV_REGION", &s_eff.av_region);
    env_u32("RECOMP_XBOX_GAME_REGION", &s_eff.game_region);
    env_u32("RECOMP_XBOX_DVD_REGION", &s_eff.dvd_region);
    if (s_eff.pal60_only && ((s_eff.av_region >> 8) & 0xFF) == AV_STANDARD_PAL_I)
        s_eff.video |= XBOX_VIDEO_PAL_60HZ;
    s_resolved = 1;
    return &s_eff;
}

uint32_t xbox_settings_avpack_word(void)
{
    const XboxSettings *s = xbox_settings();
    uint32_t standard = (s->av_region >> 8) & 0xFF;
    uint32_t refresh = AV_REFRESH_60Hz;
    if (!standard)
        standard = AV_STANDARD_NTSC_M;        /* region not reported: the North American default, as before */
    else if (standard == AV_STANDARD_PAL_I && !(s->video & XBOX_VIDEO_PAL_60HZ))
        refresh = AV_REFRESH_50Hz;
    /* The console's AV_QUERY_AV_CAPABILITIES carries the user's dashboard video flags from the EEPROM too
     * (AV_FLAGS_WIDESCREEN / LETTERBOX / HDTV_480p / 720p / 1080i - the same bits as XC_VIDEO). D3D's mode check
     * refuses a D3DPRESENTFLAG_WIDESCREEN device unless the capability word has AV_FLAGS_WIDESCREEN (0x10000), and
     * HDTV modes unless their bit is there: Panzer Dragoon Orta with the widescreen setting got CreateDevice
     * E_FAIL (D3D 0x2B21D7) and crashed on the NULL device (2026-11-01). A table without video flags (every
     * port by default) answers as before. */
    return AV_PACK_HDTV | (standard << AV_STANDARD_SHIFT) | refresh
         | (s->video & (XBOX_VIDEO_WIDESCREEN | XBOX_VIDEO_LETTERBOX | XBOX_VIDEO_HDTV_480P
                        | XBOX_VIDEO_HDTV_720P | XBOX_VIDEO_HDTV_1080I));
}

/* First read of each index per path: one bit each (indices >= 512 share the last bit). Two threads reading the
 * same index at once may both log it - harmless, and cheaper than a lock on every settings read. */
static uint8_t s_seen[XBOX_SETTINGS_PATHS][512 / 8];

int xbox_settings_log_read(int path, uint32_t index, uint32_t value, uint32_t status, uint32_t caller)
{
    static const char *const k_path[XBOX_SETTINGS_PATHS] = {
        "ExQueryNonVolatileSetting", "AvSendTVEncoderOption", "HalReadSMBusValue EEPROM"};
    uint32_t bit = index < 512 ? index : 511;
    if (path < 0 || path >= XBOX_SETTINGS_PATHS || (s_seen[path][bit / 8] & (1u << (bit % 8))))
        return 0;
    s_seen[path][bit / 8] |= (uint8_t)(1u << (bit % 8));
    fprintf(stderr, "[SETTINGS] %s index 0x%X = 0x%08X (status 0x%X) caller 0x%08X (first read)\n",
            k_path[path], index, value, status, caller);
    return 1;
}

void xbox_settings_log_reset(void)
{
    memset(s_seen, 0, sizeof s_seen);
}

uint8_t xbox_settings_eeprom_byte(uint32_t offset)
{
    const XboxSettings *s = xbox_settings();
    uint32_t word, base;
    if (offset >= 0x58 && offset < 0x5C)      { word = s->av_region; base = 0x58; }
    else if (offset >= 0x90 && offset < 0x94) { word = s->language; base = 0x90; }
    else if (offset >= 0x94 && offset < 0x98) { word = s->video; base = 0x94; }
    else if (offset >= 0xBC && offset < 0xC0) { word = s->dvd_region; base = 0xBC; }
    else return 0;
    return (uint8_t)(word >> (8 * (offset - base)));
}
