/* The settings table (xbox_settings.h) and the three paths that read it: ExQueryNonVolatileSetting, the HAL's AV
 * pack (AvSendTVEncoderOption) and raw SMBus EEPROM reads (HalReadSMBusValue), plus the XC_VIDEO word as
 * XGetVideoFlags sees it (shifted right by 16) and the PAL60 rule. */
#include "kernel.h"
#include "xbox_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This settings-only test must never dispatch guest code. */
void *recomp_lookup(ULONG address) { (void)address; abort(); }
void *recomp_lookup_manual(ULONG address) { (void)address; abort(); }

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
                                       fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void set_env(const char *k, const char *v)
{
#if defined(_WIN32)
    _putenv_s(k, v ? v : "");
#else
    if (v) setenv(k, v, 1); else unsetenv(k);
#endif
}

static ULONG query(ULONG index, ULONG *length)
{
    ULONG value = 0xDEADBEEF, type = 0;
    *length = 0;
    NTSTATUS st = xbox_ExQueryNonVolatileSetting(index, &type, &value, sizeof(value), length);
    CHECK(st == 0 && type == 4, "query 0x%lX: status %lx type %lu", (unsigned long)index, (unsigned long)st,
          (unsigned long)type);
    return value;
}

static ULONG avpack(void)
{
    ULONG r = 0;
    xbox_AvSendTVEncoderOption(NULL, AV_OPTION_QUERY_AVPACK, 0, &r);
    return r;
}

static ULONG eeprom32(UCHAR offset)
{
    ULONG lo = 0, hi = 0;
    xbox_HalReadSMBusValue(EEPROM_SLAVE_ADDRESS, offset, 1, &lo);
    xbox_HalReadSMBusValue(EEPROM_SLAVE_ADDRESS, (UCHAR)(offset + 2), 1, &hi);
    return lo | hi << 16;
}

static ULONG xgetvideoflags(void)   /* what a title sees (XAPI shifts the EEPROM word right by 16) */
{
    ULONG len;
    return query(XC_VIDEO, &len) >> 16;
}

int main(void)
{
    ULONG len;

    /* The defaults are the old answers: a port that sets nothing (THPS 2X) sees no difference. */
    xbox_settings_reset();
    CHECK(query(XC_LANGUAGE, &len) == XBOX_LANG_ENGLISH, "default language");
    CHECK(xgetvideoflags() == 0, "default video flags as a title sees them: 0 (as with the old 0x03 word)");
    CHECK(query(XC_DVD_REGION, &len) == 1, "default DVD region");
    CHECK(query(XC_FACTORY_GAME_REGION, &len) == 0, "default: game region not reported (0, as before)");
    CHECK(query(XC_FACTORY_AV_REGION, &len) == 0, "default: AV region not reported (0, as before)");
    CHECK(avpack() == (AV_PACK_HDTV | (AV_STANDARD_NTSC_M << AV_STANDARD_SHIFT) | AV_REFRESH_60Hz),
          "default AV pack NTSC-M 60 Hz: %lx", (unsigned long)avpack());

    /* US disc */
    XboxSettings us = {XBOX_LANG_ENGLISH, XBOX_LANG_BIT(XBOX_LANG_ENGLISH), 0, XBOX_AV_REGION_NTSC_M,
                       XBOX_GAME_REGION_NA, 1, -300, 0};
    xbox_settings_set_port(&us);
    CHECK(query(XC_FACTORY_GAME_REGION, &len) == XBOX_GAME_REGION_NA && len == 4, "US game region");
    CHECK(query(XC_FACTORY_AV_REGION, &len) == XBOX_AV_REGION_NTSC_M, "US AV region");
    CHECK(eeprom32(0x58) == XBOX_AV_REGION_NTSC_M, "SMBus EEPROM AV region = ExQuery: %lx",
          (unsigned long)eeprom32(0x58));
    CHECK(eeprom32(0x90) == XBOX_LANG_ENGLISH, "SMBus EEPROM language = ExQuery");
    CHECK(avpack() == (AV_PACK_HDTV | (AV_STANDARD_NTSC_M << AV_STANDARD_SHIFT) | AV_REFRESH_60Hz), "US AV pack");

    /* XC_VIDEO is the EEPROM word: widescreen reaches the title as bit 0 after the shift */
    us.video = XBOX_VIDEO_WIDESCREEN | XBOX_VIDEO_HDTV_480P;
    xbox_settings_set_port(&us);
    CHECK(xgetvideoflags() == 0x09, "widescreen + 480p as XGetVideoFlags returns them: %lx",
          (unsigned long)xgetvideoflags());
    CHECK(eeprom32(0x94) == us.video, "SMBus EEPROM video word = ExQuery");
    /* ...and in the AV capability word, as the console's AV_QUERY_AV_CAPABILITIES carries them: D3D refuses a
     * widescreen device without AV_FLAGS_WIDESCREEN there (Panzer Dragoon Orta, 2026-11-01). */
    CHECK(avpack() == (AV_PACK_HDTV | (AV_STANDARD_NTSC_M << AV_STANDARD_SHIFT) | AV_REFRESH_60Hz
                       | XBOX_VIDEO_WIDESCREEN | XBOX_VIDEO_HDTV_480P),
          "AV capabilities carry the video flags (widescreen, 480p): %lx", (unsigned long)avpack());
    {
        /* Native 16:9: the settings' widescreen bit turns the back end's 16:9 on too (no RECOMP_WIDESCREEN set). */
        extern int xbox_video_widescreen(void);
        CHECK(xbox_video_widescreen() == 1, "widescreen setting -> back end 16:9");
        /* xbox_settings_set_video at run time (RECOMP OPTIONS: ASPECT RATIO): the title's next query answers the
         * new word, the window keeps the start value. */
        xbox_settings_set_video(0);
        CHECK(xgetvideoflags() == 0, "set_video(0): the title now reads no flags: %lx", (unsigned long)xgetvideoflags());
        CHECK(query(XC_LANGUAGE, &len) != 0, "set_video keeps the rest of the port's values");
        CHECK(xbox_video_widescreen() == 1, "set_video does not reshape the window of this run");
        xbox_settings_set_video(XBOX_VIDEO_WIDESCREEN);
        CHECK(xgetvideoflags() == 0x01, "set_video(widescreen): %lx", (unsigned long)xgetvideoflags());
    }

    /* PAL disc with PAL60 (the rule: never 50 Hz, whatever an override says) */
    XboxSettings pal = {XBOX_LANG_FRENCH, XBOX_LANG_BIT(XBOX_LANG_FRENCH) | XBOX_LANG_BIT(XBOX_LANG_GERMAN)
                        | XBOX_LANG_BIT(XBOX_LANG_ITALIAN), 0, XBOX_AV_REGION_PAL_I, XBOX_GAME_REGION_ROW, 2, 0, 1};
    set_env("RECOMP_XBOX_VIDEO", "0");
    xbox_settings_set_port(&pal);
    CHECK(query(XC_VIDEO, &len) & XBOX_VIDEO_PAL_60HZ, "PAL60 rule: PAL60 kept against RECOMP_XBOX_VIDEO=0");
    CHECK(xgetvideoflags() & 0x40, "PAL60 as a title sees it (bit 6)");
    CHECK(avpack() == (AV_PACK_HDTV | (AV_STANDARD_PAL_I << AV_STANDARD_SHIFT) | AV_REFRESH_60Hz),
          "PAL60 AV pack PAL-I 60 Hz: %lx", (unsigned long)avpack());
    CHECK(query(XC_FACTORY_GAME_REGION, &len) == XBOX_GAME_REGION_ROW, "PAL game region 4");
    CHECK(query(XC_DVD_REGION, &len) == 2, "PAL DVD region 2");
    CHECK(eeprom32(0xBC) == 2, "SMBus EEPROM DVD region = ExQuery");
    {
        ULONG l = query(XC_LANGUAGE, &len);
        CHECK(l == XBOX_LANG_FRENCH || l == XBOX_LANG_GERMAN || l == XBOX_LANG_ITALIAN,
              "PAL A language is one of the disc's (host language or the French fallback): %lu", (unsigned long)l);
    }
    set_env("RECOMP_XBOX_VIDEO", NULL);

    /* A PAL title without PAL60 keeps 50 Hz */
    pal.pal60_only = 0;
    xbox_settings_set_port(&pal);
    CHECK(avpack() == (AV_PACK_HDTV | (AV_STANDARD_PAL_I << AV_STANDARD_SHIFT) | AV_REFRESH_50Hz),
          "PAL without PAL60: 50 Hz");

    /* Overrides */
    set_env("RECOMP_XBOX_LANGUAGE", "3");
    set_env("RECOMP_XBOX_GAME_REGION", "0x2");
    xbox_settings_set_port(&us);
    CHECK(query(XC_LANGUAGE, &len) == XBOX_LANG_GERMAN, "RECOMP_XBOX_LANGUAGE");
    CHECK(query(XC_FACTORY_GAME_REGION, &len) == XBOX_GAME_REGION_JAPAN, "RECOMP_XBOX_GAME_REGION");
    set_env("RECOMP_XBOX_LANGUAGE", NULL);
    set_env("RECOMP_XBOX_GAME_REGION", NULL);

    /* Settings query log: the first read of each index per path, once */
    xbox_settings_log_reset();
    CHECK(xbox_settings_log_read(XBOX_SETTINGS_PATH_EXQUERY, XC_FACTORY_GAME_REGION, 1, 0, 0x00123456) == 1,
          "log: first read of 0x104 is logged");
    CHECK(xbox_settings_log_read(XBOX_SETTINGS_PATH_EXQUERY, XC_FACTORY_GAME_REGION, 1, 0, 0x00123456) == 0,
          "log: the second read of 0x104 is not");
    CHECK(xbox_settings_log_read(XBOX_SETTINGS_PATH_EEPROM, XC_FACTORY_GAME_REGION, 1, 0, 0) == 1,
          "log: the same index on another path is logged separately");
    CHECK(xbox_settings_log_read(XBOX_SETTINGS_PATH_EXQUERY, 0x200, 0, 0, 0) == 1 &&
          xbox_settings_log_read(XBOX_SETTINGS_PATH_EXQUERY, 0x7FFF, 0, 0, 0) == 0,
          "log: indices >= 512 share one bit");
    CHECK(xbox_settings_log_read(XBOX_SETTINGS_PATHS, 1, 0, 0, 0) == 0, "log: an unknown path is ignored");
    xbox_settings_log_reset();
    CHECK(xbox_settings_log_read(XBOX_SETTINGS_PATH_EXQUERY, XC_FACTORY_GAME_REGION, 1, 0, 0) == 1, "log: reset");

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("PASS: one settings table behind ExQueryNonVolatileSetting, the AV pack and SMBus EEPROM reads");
    return 0;
}
