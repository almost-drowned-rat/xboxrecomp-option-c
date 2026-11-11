/*
 * host_paths.c - the writable install root and the restart file (see
 * host_paths.h for the rule).
 */

#include "host_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#define HP_MAX 4096

static int is_sep(char c) { return c == '/' || c == '\\'; }

static int is_absolute(const char *p)
{
    if (!p || !*p)
        return 0;
    if (is_sep(p[0]))
        return 1;
    return ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':' && is_sep(p[2]);
}

static void copy(char *dst, size_t n, const char *src)
{
    if (!n)
        return;
    snprintf(dst, n, "%s", src ? src : "");
}

/* the folder of a file path, with its separator; "" when it has none */
static void dir_of(char *dst, size_t n, const char *file)
{
    size_t k = strlen(file);
    while (k > 0 && !is_sep(file[k - 1]))
        k--;
    if (!n)
        return;
    if (k >= n)
        k = n - 1;
    memcpy(dst, file, k);
    dst[k] = 0;
}

/* exe lies under dir (dir with or without its trailing separator) */
static int under(const char *exe, const char *dir)
{
    size_t k = strlen(dir);
    while (k > 1 && is_sep(dir[k - 1]))
        k--;
    if (!k || strncmp(exe, dir, k) != 0)
        return 0;
    return is_sep(exe[k]);
}

int xbox_host_paths_resolve(const char *exe, const char *appimage, const char *appdir,
                            char *root, size_t root_n, char *self, size_t self_n)
{
    exe = exe ? exe : "";
    if (is_absolute(appimage) && is_absolute(appdir) && under(exe, appdir)) {
        dir_of(root, root_n, appimage);
        copy(self, self_n, appimage);
        return 1;
    }
    dir_of(root, root_n, exe);
    copy(self, self_n, exe);
    return 0;
}

static char g_root[HP_MAX], g_self[HP_MAX];

static void compute(void)
{
    char exe[HP_MAX] = "";
#ifdef _WIN32
    static wchar_t w[HP_MAX];
    DWORD k = GetModuleFileNameW(NULL, w, HP_MAX);
    if (k > 0 && k < HP_MAX)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, exe, (int)sizeof exe, NULL, NULL);
    xbox_host_paths_resolve(exe, NULL, NULL, g_root, sizeof g_root, g_self, sizeof g_self);
#else
    ssize_t k;
#ifdef __ANDROID__
    /* the process is the system's app_process64 (/system/bin): the app's loader names its writable folder
     * (filesDir) in XR_HOST_ROOT before any toolkit code runs; self is the root itself (no exe of ours) */
    const char *r = getenv("XR_HOST_ROOT");
    if (is_absolute(r)) {
        copy(exe, sizeof exe, r);
        k = (ssize_t)strlen(exe);
        if (k > 0 && !is_sep(exe[k - 1]) && (size_t)k + 1 < sizeof exe) {
            exe[k] = '/';
            exe[k + 1] = 0;
        }
        copy(g_root, sizeof g_root, exe);
        copy(g_self, sizeof g_self, exe);
        return;
    }
#endif
    k = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[k > 0 ? k : 0] = 0;
    xbox_host_paths_resolve(exe, getenv("APPIMAGE"), getenv("APPDIR"),
                            g_root, sizeof g_root, g_self, sizeof g_self);
#endif
}

#ifdef _WIN32
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK once_cb(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    compute();
    return TRUE;
}
static void ensure(void) { InitOnceExecuteOnce(&g_once, once_cb, NULL, NULL); }
#else
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static void ensure(void) { pthread_once(&g_once, compute); }
#endif

const char *xbox_host_root(void)
{
    ensure();
    return g_root;
}

const char *xbox_host_self(void)
{
    ensure();
    return g_self;
}
