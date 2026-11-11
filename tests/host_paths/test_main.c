/*
 * host_paths test: the root / restart rule (plain, AppImage, a program started
 * from an AppImage, damaged environment) and the live values of this process.
 */

#include "platform/host_paths.h"
#include "platform/host_info.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0755)
#define RMDIR(p) rmdir(p)
#endif

static int fails;

static void expect(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want)) {
        printf("FAIL %s: got \"%s\", want \"%s\"\n", what, got, want);
        fails++;
    }
}

static void rule(const char *name, const char *exe, const char *appimage, const char *appdir,
                 const char *want_root, const char *want_self, int want_ai)
{
    char root[512], self[512], what[128];
    int ai = xbox_host_paths_resolve(exe, appimage, appdir, root, sizeof root, self, sizeof self);
    snprintf(what, sizeof what, "%s root", name);
    expect(what, root, want_root);
    snprintf(what, sizeof what, "%s self", name);
    expect(what, self, want_self);
    if (ai != want_ai) {
        printf("FAIL %s: AppImage rule %d, want %d\n", name, ai, want_ai);
        fails++;
    }
}

int main(void)
{
    struct stat st;
    const char *r, *s;
    char dir[4096];
    size_t k;

    rule("plain", "/home/p/orta/orta", NULL, NULL, "/home/p/orta/", "/home/p/orta/orta", 0);
    rule("windows", "D:\\Games\\Orta\\orta.exe", NULL, NULL, "D:\\Games\\Orta\\", "D:\\Games\\Orta\\orta.exe", 0);
    rule("appimage", "/tmp/.mount_OrtaAbc/usr/bin/orta", "/home/p/Games/Orta.AppImage", "/tmp/.mount_OrtaAbc",
         "/home/p/Games/", "/home/p/Games/Orta.AppImage", 1);
    rule("appdir slash", "/tmp/.mount_OrtaAbc/usr/bin/orta", "/home/p/Orta.AppImage", "/tmp/.mount_OrtaAbc/",
         "/home/p/", "/home/p/Orta.AppImage", 1);
    /* started from an AppImage: inherits both variables, lives elsewhere */
    rule("inherited", "/usr/bin/othertool", "/home/p/Orta.AppImage", "/tmp/.mount_OrtaAbc",
         "/usr/bin/", "/usr/bin/othertool", 0);
    /* a prefix that is not a folder boundary */
    rule("prefix", "/tmp/.mount_OrtaAbcd/usr/bin/orta", "/home/p/Orta.AppImage", "/tmp/.mount_OrtaAbc",
         "/tmp/.mount_OrtaAbcd/usr/bin/", "/tmp/.mount_OrtaAbcd/usr/bin/orta", 0);
    rule("relative appimage", "/tmp/.mount_X/orta", "Orta.AppImage", "/tmp/.mount_X",
         "/tmp/.mount_X/", "/tmp/.mount_X/orta", 0);
    rule("no appdir", "/tmp/.mount_X/orta", "/home/p/Orta.AppImage", NULL, "/tmp/.mount_X/", "/tmp/.mount_X/orta", 0);
    rule("empty exe", "", NULL, NULL, "", "", 0);

    r = xbox_host_root();
    s = xbox_host_self();
    k = strlen(r);
    printf("root: %s\nself: %s\n", r, s);
    if (!k || (r[k - 1] != '/' && r[k - 1] != '\\')) {
        printf("FAIL live root has no trailing separator\n");
        fails++;
    }
    /* the CRT's stat refuses a folder with a trailing separator on Windows */
    snprintf(dir, sizeof dir, "%.*s", k > 1 ? (int)(k - 1) : (int)k, r);
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        printf("FAIL live root is not a folder\n");
        fails++;
    }
    if (stat(s, &st) != 0 || !S_ISREG(st.st_mode)) {
        printf("FAIL live self is not a file\n");
        fails++;
    }
    if (strncmp(s, r, k)) {
        printf("FAIL live self is not in root\n");
        fails++;
    }
    if (xbox_host_root() != r) {
        printf("FAIL root not computed once\n");
        fails++;
    }

    /* the log's release name: tools/release.txt (Windows payload), else data/release.txt (AppImage launcher) */
    {
        char root[4096], game[4200], p[4300], ver[256];
        FILE *f;
        snprintf(root, sizeof root, "%shp_release_test", r);
        snprintf(game, sizeof game, "%s/game", root);
        MKDIR(root); MKDIR(game);
        snprintf(p, sizeof p, "%s/data", root); MKDIR(p);
        host_version_text(game, ver, sizeof ver);
        if (strncmp(ver, "dev build", 9)) { printf("FAIL no release file: \"%s\"\n", ver); fails++; }
        snprintf(p, sizeof p, "%s/data/release.txt", root);
        if ((f = fopen(p, "wb"))) { fputs("1.1.0-test2\n", f); fclose(f); }
        host_version_text(game, ver, sizeof ver);
        if (strncmp(ver, "1.1.0-test2 (", 13)) { printf("FAIL data/release.txt: \"%s\"\n", ver); fails++; }
        snprintf(p, sizeof p, "%s/tools", root); MKDIR(p);
        snprintf(p, sizeof p, "%s/tools/release.txt", root);
        if ((f = fopen(p, "wb"))) { fputs("v9.9\r\n", f); fclose(f); }
        host_version_text(game, ver, sizeof ver);
        if (strncmp(ver, "v9.9 (", 6)) { printf("FAIL tools/release.txt first: \"%s\"\n", ver); fails++; }
        remove(p);
        snprintf(p, sizeof p, "%s/data/release.txt", root); remove(p);
        snprintf(p, sizeof p, "%s/tools", root); RMDIR(p);
        snprintf(p, sizeof p, "%s/data", root); RMDIR(p);
        RMDIR(game); RMDIR(root);
    }
    if (fails)
        return 1;
    puts("PASS: host root / restart rule (plain, Windows, AppImage, inherited env, prefix, damaged env), live values, "
         "release name (tools/ then data/release.txt)");
    return 0;
}
