/*
 * kernel_xbox.c - Xbox Identity & Hardware Stubs
 *
 * Provides Xbox-specific exported data (hardware info, kernel version,
 * keys, image filename) and section loading stubs.
 *
 * Most Xbox-specific hardware data is stubbed with plausible retail
 * values. Crypto keys are zeroed since we don't need Xbox Live or
 * EEPROM-based encryption on PC.
 */

#include "kernel.h"
#include "xbox_settings.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 1 while the APU's 5.1 output runs (set by apu_xaudio2.c's xa2_init): XC_AUDIO then reports Dolby Digital */
volatile int g_xbox_audio_surround;

/* ============================================================================
 * Exported Data Objects
 *
 * These are global variables exported by the Xbox kernel at known ordinals.
 * Game code accesses them directly via the thunk table.
 * ============================================================================ */

/* Hardware info - default report is a standard retail Xbox.
 * A target may override via xbox_kernel_set_version() or by patching the
 * global directly; no title-specific values are baked in. */
XBOX_HARDWARE_INFO xbox_HardwareInfo = {
    .Flags       = 0x00000020,  /* Retail Xbox */
    .GpuRevision = 0xD2,       /* NV2A D2 revision */
    .McpRevision = 0xD4,       /* MCPX D4 revision */
    .Reserved    = {0, 0}
};

/* Kernel version - default is a stock retail value. Titles built against a
 * different XDK can report their own through xbox_kernel_set_version(). */
XBOX_KRNL_VERSION xbox_KrnlVersion = {
    .Major = 1,
    .Minor = 0,
    .Build = 5849,
    .Qfe   = 1
};

/* Override the kernel version reported to game code (ordinal 324).
 * Match the XDK revision the target title was built with if it checks it. */
void xbox_kernel_set_version(USHORT major, USHORT minor, USHORT build, USHORT qfe)
{
    xbox_KrnlVersion.Major = major;
    xbox_KrnlVersion.Minor = minor;
    xbox_KrnlVersion.Build = build;
    xbox_KrnlVersion.Qfe   = qfe;
}

/* Crypto keys - zeroed, not needed for PC operation.
 * These are unique per-console on real hardware. */
UCHAR xbox_EEPROMKey[16]              = {0};
UCHAR xbox_HDKey[16]                  = {0};
UCHAR xbox_SignatureKey[16]           = {0};
UCHAR xbox_LANKey[16]                 = {0};
UCHAR xbox_AlternateSignatureKeys[16][16] = {{0}};

/* The console identity saves depend on.
 *
 * XAPI signs non-roamable saves with XboxHDKey (XCalculateSignature) and some
 * titles also store the disk serial or check it. On a console these are fixed
 * per unit, so a save stays valid for the life of the console. Here they are
 * fixed for the life of the port: built-in defaults that must never change
 * (a different default would invalidate every existing save), never random.
 * <save_dir>\console.txt can override them -- in saves, because saves depend
 * on it -- for a player bringing saves from a real console:
 *
 *   hd_key = 00112233445566778899AABBCCDDEEFF
 *   serial = 123456789012
 */
unsigned char g_xbox_identity_hd_key[16];          /* default: all zero */
char g_xbox_identity_serial[13] = "XR0000000000";

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void xbox_identity_load(const char *path_utf8)
{
    char line[256];
    FILE *f = NULL;
#if defined(_WIN32)
    WCHAR wpath[XBOX_HOST_PATH];
    if (MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, wpath, XBOX_HOST_PATH))
        f = _wfopen(wpath, L"r");
#else
    f = fopen(path_utf8, "r");
#endif
    if (!f) {
        memcpy(xbox_HDKey, g_xbox_identity_hd_key, 16);
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char *v = strchr(line, '='), *e;
        if (line[0] == '#' || !v)
            continue;
        *v++ = 0;
        while (*v == ' ' || *v == '\t') v++;
        for (e = v + strlen(v); e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '); e--)
            e[-1] = 0;
        if (!strncmp(line, "hd_key", 6)) {
            unsigned char key[16];
            int i, ok = strlen(v) == 32;
            for (i = 0; ok && i < 16; i++) {
                int hi = hexval(v[2 * i]), lo = hexval(v[2 * i + 1]);
                if (hi < 0 || lo < 0) ok = 0;
                else key[i] = (unsigned char)(hi << 4 | lo);
            }
            if (ok) memcpy(g_xbox_identity_hd_key, key, 16);
            else fprintf(stderr, "  [IDENTITY] hd_key in %s is not 32 hex digits, ignored\n", path_utf8);
        } else if (!strncmp(line, "serial", 6)) {
            if (strlen(v) == 12) memcpy(g_xbox_identity_serial, v, 13);
            else fprintf(stderr, "  [IDENTITY] serial in %s is not 12 characters, ignored\n", path_utf8);
        }
    }
    fclose(f);
    memcpy(xbox_HDKey, g_xbox_identity_hd_key, 16);
    fprintf(stderr, "  [IDENTITY] console identity from %s (serial %s)\n", path_utf8,
            g_xbox_identity_serial);
}

/* Public key data for Xbox Live signature verification - not needed */
UCHAR xbox_XePublicKeyData[284] = {0};

/* Image filename - the XBE path as seen by the kernel */
static char g_image_filename[] = "\\Device\\CdRom0\\default.xbe";
XBOX_ANSI_STRING xbox_XeImageFileName = {
    .Length        = sizeof(g_image_filename) - 1,
    .MaximumLength = sizeof(g_image_filename),
    .Buffer        = g_image_filename
};

/* Launch data page - used for title-to-title launches (e.g., Xbox Dashboard → game).
 * Allocated dynamically and zeroed for normal game boot. */
static XBOX_LAUNCH_DATA_PAGE g_launch_data_page = {0};
XBOX_LAUNCH_DATA_PAGE* xbox_LaunchDataPage = &g_launch_data_page;

/* ============================================================================
 * Section Loading
 *
 * XeLoadSection/XeUnloadSection manage on-demand loading of XBE sections.
 * On Xbox, some sections are demand-paged from disc. In our recompilation,
 * the entire executable is loaded into memory, so these are reference-counting
 * no-ops.
 * ============================================================================ */

NTSTATUS __stdcall xbox_XeLoadSection(PXBE_SECTION_HEADER Section)
{
    if (!Section)
        return STATUS_INVALID_PARAMETER;

    InterlockedIncrement(&Section->SectionReferenceCount);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_XBOX,
        "XeLoadSection: '%s' at %p (size=%u, refcount=%d)",
        Section->SectionName ? Section->SectionName : "<null>",
        Section->VirtualAddress, Section->VirtualSize,
        Section->SectionReferenceCount);

    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_XeUnloadSection(PXBE_SECTION_HEADER Section)
{
    if (!Section)
        return STATUS_INVALID_PARAMETER;

    LONG new_count = InterlockedDecrement(&Section->SectionReferenceCount);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_XBOX,
        "XeUnloadSection: '%s' (refcount=%d)",
        Section->SectionName ? Section->SectionName : "<null>",
        new_count);

    return STATUS_SUCCESS;
}

/* ============================================================================
 * EEPROM / Non-Volatile Settings
 *
 * ExQueryNonVolatileSetting / ExSaveNonVolatileSetting read and write EEPROM
 * settings. On real hardware these are stored in the 256-byte EEPROM on the
 * SMBus. Here they come from the port's settings table (xbox_settings.h), the
 * same one the HAL's AV pack and raw SMBus EEPROM reads use; without a port
 * table the defaults are the old answers: English, no video flags as a title
 * sees them, NTSC-M 60 Hz, region fields not reported, DVD region 1. Audio:
 * stereo PCM.
 * ============================================================================ */

/* Present the logical surface at 16:9 instead of 4:3 (nv2a_backend.h).
 * Off by default: 4:3 is the original. RECOMP_WIDESCREEN=1 turns it on.
 * Only the back end reads it; the title is not told (burnout3-sw also
 * reports it through XC_VIDEO_FLAGS -- not ported). */
void (*g_xbox_screenshot_hook)(const char *name) = NULL;
void (*volatile g_xbox_nv2a_idle_hook)(void) = NULL;

/* The gameplay aspect ratio (THPS 2X maintainer, 2026-11-07: 16:10 and 21:9
 * next to 4:3 / 16:9; every port): RECOMP_WIDESCREEN 0 / 4:3 = 4:3, 1 or
 * 16:9, 16:10, 21:9 (= 64:27, the 2560x1080 / 5120x2160 panels; 3440x1440 is
 * 43:18, under 1 % narrower).
 * option-c (2026-11-01; maintainer: a title's own 16:9 wins over any
 * patch): with RECOMP_WIDESCREEN unset, a title TOLD widescreen - the
 * widescreen bit of the console settings' video word (XC_VIDEO,
 * xbox_settings.h), which also reaches D3D through the AV capability word -
 * renders 640x480 anamorphic, and the window shows that at 16:9. A port that
 * widens such a title further (Panzer Dragoon Orta 16:10 / 21:9) sets
 * RECOMP_WIDESCREEN to the value as well. */
double xbox_video_aspect(void)
{
    static double a = 0.0;
    if (a == 0.0) {
        const char *e = getenv("RECOMP_WIDESCREEN");
        if (!e || !*e)
            a = (xbox_settings()->video & XBOX_VIDEO_WIDESCREEN) ? 16.0 / 9.0 : 4.0 / 3.0;
        else if (!strcmp(e, "0") || !strcmp(e, "4:3"))
            a = 4.0 / 3.0;
        else if (!strcmp(e, "16:10"))
            a = 16.0 / 10.0;
        else if (!strcmp(e, "21:9"))
            a = 64.0 / 27.0;
        else
            a = 16.0 / 9.0;                       /* "1", "16:9" */
    }
    return a;
}

int xbox_video_widescreen(void)
{
    return xbox_video_aspect() > 4.0 / 3.0 + 0.01;
}

/* A title without native 16:9 (aspect-ratio rule, porting_guide.md): only
 * gameplay is shown at 16:9, menus, loading screens and movies stay 4:3,
 * pillarboxed. The port says which one the current frame is; until it says
 * anything (-1) a widescreen title is shown at 16:9 throughout, as before. */
static volatile int s_present_wide = -1;

void xbox_SetPresentWide(int wide)
{
    s_present_wide = wide ? 1 : 0;
}

int xbox_present_wide(void)
{
    return xbox_video_widescreen() && s_present_wide != 0;
}

/* The port's mark alone, whatever RECOMP_WIDESCREEN says: 1 gameplay (16:9),
 * 0 a 4:3 frame, -1 never marked. The render scale's X follows it. */
int xbox_present_wide_mark(void)
{
    return s_present_wide;
}

NTSTATUS __stdcall xbox_ExQueryNonVolatileSetting(
    ULONG ValueIndex, PULONG Type, PVOID Value, ULONG ValueLength, PULONG ResultLength)
{
    if (!Value)
        return STATUS_INVALID_PARAMETER;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_XBOX,
        "ExQueryNonVolatileSetting: index=0x%02X len=%u", ValueIndex, ValueLength);

    switch (ValueIndex) {
    case XC_LANGUAGE:
        /* The port's table (xbox_settings.h): its disc's language, the host's where the disc has it */
        if (ValueLength >= sizeof(ULONG)) {
            *(PULONG)Value = xbox_settings()->language;
            if (Type) *Type = 4; /* REG_DWORD */
            if (ResultLength) *ResultLength = sizeof(ULONG);
        }
        break;

    case XC_VIDEO:
        /* The EEPROM word (XBOX_VIDEO_*): XGetVideoFlags shifts it right by 16. It was 0x03 here, which the
         * shift drops - titles saw no flags; the default 0 keeps exactly that (THPS 2X), ports set their own. */
        if (ValueLength >= sizeof(ULONG)) {
            *(PULONG)Value = xbox_settings()->video;
            if (Type) *Type = 4; /* REG_DWORD */
            if (ResultLength) *ResultLength = sizeof(ULONG);
        }
        break;

    case XC_AUDIO:
        /* Stereo PCM matches the two-channel host output. 1 means mono;
         * advertising AC3 requires an encoded-output path.
         * RECOMP_XBOX_AUDIO=<hex> (diagnosis, 2026-11-07): another
         * value, e.g. 10000 = Dolby Digital enabled - a title then mixes its
         * GP image for six speakers (checked with RECOMP_APU_SPKSTATS). */
        /* 5.1 output running (apu_xaudio2.c sets g_xbox_audio_surround once the device has six speakers): the
         * Dolby Digital bit 0x00010000 - the title mixes for six speakers itself (Orta: FC dialogue, SL / SR at
         * front level). */
        if (ValueLength >= sizeof(ULONG)) {
            const char *ea = getenv("RECOMP_XBOX_AUDIO");
            *(PULONG)Value = (ea && *ea) ? (ULONG)strtoul(ea, NULL, 16)
                                         : g_xbox_audio_surround ? 0x00010000u : 0x00000000u;
            if (Type) *Type = 4; /* REG_DWORD */
            if (ResultLength) *ResultLength = sizeof(ULONG);
        }
        break;

    case XC_P_CONTROL_GAMES:
    case XC_P_CONTROL_MOVIES:
        /* Unrestricted. Titles compare their XBE certificate's rating against
         * this and refuse to run (Halo boots to the dashboard) if the console
         * is configured more strictly, so 0 means "no restriction". */
        if (ValueLength >= sizeof(ULONG)) {
            *(PULONG)Value = 0;
            if (Type) *Type = 4;
            if (ResultLength) *ResultLength = sizeof(ULONG);
        }
        break;

    case XC_DVD_REGION:
        /* Region 1 (North America) by default, 2 on the PAL discs */
        if (ValueLength >= sizeof(ULONG)) {
            *(PULONG)Value = xbox_settings()->dvd_region;
            if (Type) *Type = 4;
            if (ResultLength) *ResultLength = sizeof(ULONG);
        }
        break;

    case XC_MISC:
        /* Misc flags: 0 = no special flags */
        if (ValueLength >= sizeof(ULONG)) {
            *(PULONG)Value = 0;
            if (Type) *Type = 4;
            if (ResultLength) *ResultLength = sizeof(ULONG);
        }
        break;

    case XC_TIMEZONE_BIAS:
        /* UTC-5 (Eastern Time) in minutes: -300 */
        if (ValueLength >= sizeof(LONG)) {
            *(PLONG)Value = xbox_settings()->timezone_bias;
            if (Type) *Type = 4;
            if (ResultLength) *ResultLength = sizeof(LONG);
        }
        break;

    case XC_FACTORY_AV_REGION:
    case XC_FACTORY_GAME_REGION:
        /* Reported once the port sets them (0 = not reported: the unhandled answer below, as before) */
        {
            const XboxSettings *s = xbox_settings();
            ULONG v = ValueIndex == XC_FACTORY_AV_REGION ? s->av_region : s->game_region;
            if (v && ValueLength >= sizeof(ULONG)) {
                *(PULONG)Value = v;
                if (Type) *Type = 4;
                if (ResultLength) *ResultLength = sizeof(ULONG);
                break;
            }
        }
        /* fall through */
    default:
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_XBOX,
            "ExQueryNonVolatileSetting: unhandled index 0x%02X", ValueIndex);
        memset(Value, 0, ValueLength);
        if (Type) *Type = 4;
        if (ResultLength) *ResultLength = ValueLength;
        break;
    }

    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_ExSaveNonVolatileSetting(
    ULONG ValueIndex, ULONG Type, PVOID Value, ULONG ValueLength)
{
    (void)Type;
    (void)Value;
    (void)ValueLength;

    xbox_log(XBOX_LOG_INFO, XBOX_LOG_XBOX,
        "ExSaveNonVolatileSetting: index=0x%02X len=%u (ignored - read-only on PC)",
        ValueIndex, ValueLength);

    /* Accept writes silently but don't persist them */
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Network / PHY
 *
 * PhyGetLinkState reports Ethernet link status. Xbox had a built-in 100Mbit
 * NIC. Ports report no link by default (see below).
 * ============================================================================ */

ULONG __stdcall xbox_PhyGetLinkState(BOOLEAN Verify)
{
    (void)Verify;
    /* No cable: ports have no network (System Link/online are not emulated).
     * XNET's XNetGetEthernetLinkStatus is library code on top of this, so the
     * title greys out System Link itself instead of searching forever.
     * RECOMP_NET_LINK=1 reports link up, 100 Mbps, full duplex (the old
     * default) for tests of a title's network menus. */
    const char *e = getenv("RECOMP_NET_LINK");
    if (e && e[0] == '1')
        return 0x01 | 0x02 | 0x08; /* ACTIVE | 100MBPS | FULL_DUPLEX */
    return 0;
}

NTSTATUS __stdcall xbox_PhyInitialize(BOOLEAN ForceReset, PVOID Param2)
{
    (void)ForceReset;
    (void)Param2;
    return STATUS_SUCCESS;
}
