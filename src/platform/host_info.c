/*
 * host_info.c - version and host lines for the log (see host_info.h).
 */
#include "host_info.h"

#include "xr_build_info.h"                        /* generated: XR_GAME_COMMIT, XR_TOOLKIT_COMMIT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/utsname.h>
#endif

/* the first line of <parent of game_dir>/tools/release.txt (the payload unpacked there: Windows), else of
 * <parent>/data/release.txt (written by the launcher when its tools are a read-only bundle: an AppImage), or "" */
static void release_name(const char *game_dir, char *out, size_t n)
{
    char path[1100];
    size_t len = game_dir ? strlen(game_dir) : 0;
    FILE *f;
    out[0] = 0;
    while (len && (game_dir[len - 1] == '/' || game_dir[len - 1] == '\\'))
        len--;
    while (len && game_dir[len - 1] != '/' && game_dir[len - 1] != '\\')
        len--;
    snprintf(path, sizeof path, "%.*stools/release.txt", (int)len, len ? game_dir : "");
    f = fopen(path, "rb");
    if (!f) {
        snprintf(path, sizeof path, "%.*sdata/release.txt", (int)len, len ? game_dir : "");
        f = fopen(path, "rb");
    }
    if (!f)
        return;
    if (fgets(out, (int)n, f)) {
        size_t m = strlen(out);
        while (m && (out[m - 1] == '\n' || out[m - 1] == '\r' || out[m - 1] == ' '))
            out[--m] = 0;
    }
    fclose(f);
}

void host_version_text(const char *game_dir, char *buf, size_t n)
{
    char rel[64];
    const char *game = XR_GAME_COMMIT;
#ifdef __ANDROID__
    /* this library is prebuilt in the runtime SDK (tools/android/make_runtime_sdk.ps1): XR_GAME_COMMIT is the
     * SDK's, not the game's, which is built on the device later - the app names the game's commit instead */
    const char *g = getenv("XR_GAME_COMMIT");
    if (g && *g)
        game = g;
#endif
    release_name(game_dir, rel, sizeof rel);
    snprintf(buf, n, "%s (game %s, toolkit %s)", rel[0] ? rel : "dev build", game, XR_TOOLKIT_COMMIT);
}

/* what the environment says about the machine around a Wine / Linux process */
static void env_notes(char *buf, size_t n)
{
    const char *deck = getenv("SteamDeck");
    const char *gs = getenv("GAMESCOPE_WAYLAND_DISPLAY");
    const char *de = getenv("XDG_CURRENT_DESKTOP");
    size_t k = strlen(buf);
    int first = 1;
    if (deck && !strcmp(deck, "1")) {
        k += (size_t)snprintf(buf + k, k < n ? n - k : 0, "%sSteam Deck", first ? "; " : ", ");
        first = 0;
    }
    if (gs && *gs) {
        k += (size_t)snprintf(buf + k, k < n ? n - k : 0, "%sGamescope", first ? "; " : ", ");
        first = 0;
    } else if (de && *de) {
        k += (size_t)snprintf(buf + k, k < n ? n - k : 0, "%sdesktop %s", first ? "; " : ", ", de);
        first = 0;
    }
}

#if defined(_WIN32)
/* a REG_SZ under HKLM, "" when missing (advapi32 loaded by name: nothing extra to link) */
static void reg_text(const char *key, const char *value, char *out, DWORD n)
{
    typedef LONG (WINAPI *reg_get_fn)(HKEY, LPCSTR, LPCSTR, DWORD, LPDWORD, PVOID, LPDWORD);
    HMODULE adv = LoadLibraryA("advapi32.dll");
    reg_get_fn get = adv ? (reg_get_fn)(void *)GetProcAddress(adv, "RegGetValueA") : NULL;
    DWORD sz = n;
    out[0] = 0;
    if (!get || get(HKEY_LOCAL_MACHINE, key, value, 0x00000002 /* RRF_RT_REG_SZ */, NULL, out, &sz) != 0)
        out[0] = 0;
    out[n - 1] = 0;
}

/* Proton (Steam): the compatibility tool's folder name ("Proton 9.0 (Beta)", "GE-Proton9-20"), "" when none */
static void proton_name(char *out, size_t n)
{
    const char *tools = getenv("STEAM_COMPAT_TOOL_PATHS");
    const char *data = getenv("STEAM_COMPAT_DATA_PATH");
    const char *end, *start;
    out[0] = 0;
    if (!tools || !*tools) {
        if (data && *data)
            snprintf(out, n, "Proton");
        return;
    }
    end = strchr(tools, ':');
    if (!end)
        end = tools + strlen(tools);
    while (end > tools && (end[-1] == '/' || end[-1] == '\\'))
        end--;
    start = end;
    while (start > tools && start[-1] != '/' && start[-1] != '\\')
        start--;
    snprintf(out, n, "%.*s", (int)(end - start), start);
}
#endif

void host_describe(char *buf, size_t n)
{
#if defined(_WIN32)
    typedef const char *(*wine_str_fn)(void);
    typedef void (*wine_host_fn)(const char **sysname, const char **release);
    typedef LONG (WINAPI *rtl_ver_fn)(OSVERSIONINFOW *);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    wine_str_fn wver = nt ? (wine_str_fn)(void *)GetProcAddress(nt, "wine_get_version") : NULL;
    if (wver) {
        wine_str_fn wbuild = (wine_str_fn)(void *)GetProcAddress(nt, "wine_get_build_id");
        wine_host_fn whost = (wine_host_fn)(void *)GetProcAddress(nt, "wine_get_host_version");
        const char *sys = NULL, *rel = NULL;
        char proton[96];
        if (whost)
            whost(&sys, &rel);
        proton_name(proton, sizeof proton);
        snprintf(buf, n, "Wine %s (%s)%s%s on %s %s", wver(), wbuild ? wbuild() : "?", proton[0] ? ", " : "", proton,
                 sys ? sys : "?", rel ? rel : "");
        env_notes(buf, n);
        return;
    }
    {
        OSVERSIONINFOW v;
        rtl_ver_fn rtl = nt ? (rtl_ver_fn)(void *)GetProcAddress(nt, "RtlGetVersion") : NULL;
        SYSTEM_INFO si;
        char disp[64], ed[96];
        memset(&v, 0, sizeof v);
        v.dwOSVersionInfoSize = sizeof v;
        GetNativeSystemInfo(&si);
        reg_text("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "DisplayVersion", disp, sizeof disp);
        reg_text("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "EditionID", ed, sizeof ed);
        if (!strcmp(ed, "Core"))                     /* the registry's names for Home / Pro */
            strcpy(ed, "Home");
        else if (!strcmp(ed, "Professional"))
            strcpy(ed, "Pro");
        if (rtl && rtl(&v) == 0)
            /* "Windows 11 Core 25H2 (10.0.26300) x64": 11 from build 22000 (the registry's ProductName says 10) */
            snprintf(buf, n, "Windows %s%s%s%s%s (%lu.%lu.%lu) %s",
                     v.dwMajorVersion == 10 && v.dwBuildNumber >= 22000 ? "11" : v.dwMajorVersion == 10 ? "10" : "",
                     ed[0] ? " " : "", ed, disp[0] ? " " : "", disp,
                     (unsigned long)v.dwMajorVersion, (unsigned long)v.dwMinorVersion, (unsigned long)v.dwBuildNumber,
                     si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "arm64" : "x64");
        else
            snprintf(buf, n, "Windows (version unknown)");
    }
#else
    struct utsname u;
    if (uname(&u) == 0)
        snprintf(buf, n, "%s %s %s", u.sysname, u.release, u.machine);
    else
        snprintf(buf, n, "unknown");
    env_notes(buf, n);
#endif
}

void host_describe_machine(char *cpu, size_t ncpu, char *ram, size_t nram, char *power, size_t npower)
{
    cpu[0] = ram[0] = power[0] = 0;
#if defined(_WIN32)
    {
        char name[128];
        SYSTEM_INFO si;
        DWORD len = 0, cores = 0;
        MEMORYSTATUSEX ms;
        reg_text("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString", name, sizeof name);
        GetNativeSystemInfo(&si);
        /* physical cores: the RelationProcessorCore records */
        GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
        if (len) {
            char *p = (char *)malloc(len);
            if (p && GetLogicalProcessorInformationEx(RelationProcessorCore,
                                                      (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)p, &len)) {
                DWORD off = 0;
                while (off < len) {
                    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *e = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)(p + off);
                    cores++;
                    off += e->Size;
                }
            }
            free(p);
        }
        {
            char *s = name;                                /* the registry pads some names with spaces */
            size_t m;
            while (*s == ' ')
                s++;
            m = strlen(s);
            while (m && s[m - 1] == ' ')
                s[--m] = 0;
            snprintf(cpu, ncpu, "%s, %lu cores / %lu threads", *s ? s : "unknown",
                     (unsigned long)cores, (unsigned long)si.dwNumberOfProcessors);
        }
        ms.dwLength = sizeof ms;
        if (GlobalMemoryStatusEx(&ms))
            snprintf(ram, nram, "%.1f GB (%.1f GB free)", (double)ms.ullTotalPhys / 1073741824.0,
                     (double)ms.ullAvailPhys / 1073741824.0);
    }
    {
        /* the active power plan's name (powrprof loaded by name); under Wine usually none */
        typedef DWORD (WINAPI *active_fn)(HKEY, GUID **);
        typedef DWORD (WINAPI *name_fn)(HKEY, const GUID *, const GUID *, const GUID *, UCHAR *, DWORD *);
        HMODULE pp = LoadLibraryA("powrprof.dll");
        active_fn act = pp ? (active_fn)(void *)GetProcAddress(pp, "PowerGetActiveScheme") : NULL;
        name_fn nm = pp ? (name_fn)(void *)GetProcAddress(pp, "PowerReadFriendlyName") : NULL;
        GUID *g = NULL;
        if (act && nm && act(NULL, &g) == 0 && g) {
            DWORD sz = 0;
            /* the size first: names run longer than they look (258 bytes for "Höchstleistung" on Windows 11) */
            if (nm(NULL, g, NULL, NULL, NULL, &sz) == 0 && sz) {
                WCHAR *w = (WCHAR *)calloc(1, sz + sizeof(WCHAR));
                if (w && nm(NULL, g, NULL, NULL, (UCHAR *)w, &sz) == 0)
                    WideCharToMultiByte(CP_UTF8, 0, w, -1, power, (int)npower, NULL, NULL);
                free(w);
            }
            LocalFree(g);
        }
        if (!power[0])
            snprintf(power, npower, "unknown");
    }
#else
    /* native Linux / Android: still to come (cpufreq governor / power-profiles-daemon, distro, device) */
    (void)ncpu; (void)nram; (void)npower;
#endif
}

void host_log_version_and_host(const char *game_dir)
{
    char v[160], h[320], cpu[200], ram[64], pw[128];
    host_version_text(game_dir, v, sizeof v);
    host_describe(h, sizeof h);
    printf("version: %s\nhost: %s\n", v, h);
    host_describe_machine(cpu, sizeof cpu, ram, sizeof ram, pw, sizeof pw);
    if (cpu[0])
        printf("cpu: %s\n", cpu);
    if (ram[0])
        printf("ram: %s\n", ram);
    if (pw[0])
        printf("power plan: %s\n", pw);
}
