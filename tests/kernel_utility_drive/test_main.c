/*
 * The utility drive (Z:, \Device\Harddisk0\Partition3-5) as the console has it (2026-11-01).
 *
 * Panzer Dragoon Orta mirrors its disc folders onto Z:: for each one it probes FindFirstFile("z:\Sprite") - XAPI
 * opens the parent "\Device\Harddisk0\Partition5\" and queries the name - and calls CreateDirectory when that
 * fails. "Partition5\" was read as the partition DEVICE (the image file), the probe looked in the wrong folder,
 * CreateDirectory never ran and every copy into Z:\Sprite\... failed (~600,000 retries in 3 min). Checked here,
 * host-level, in a fresh portable layout (game\, saves\, data\ in a temp folder):
 *   1. "\Device\Harddisk0\Partition5" (no separator) is the device: data\Partition5.img;
 *   2. "\Device\Harddisk0\Partition5\" is the volume's root folder, data\cache\<title id> - the same folder
 *      "Partition5\Sprite" and "Z:\Sprite" live in; Partition0\ (the whole disk) stays the device;
 *   3. NtCreateFile opens that root folder as a directory and also WITHOUT FILE_DIRECTORY_FILE (XAPI's free-space
 *      query), and creates "Z:\Sprite" as a directory with FILE_CREATE;
 *   4. a format (FSCTL_DISMOUNT_VOLUME on the dismounted partition image) empties data\cache\<title id> and nothing
 *      else; a dismount of anything else empties nothing;
 *   5. nothing is written outside the layout folder (the data folder holds all of it).
 */
#include "kernel.h"
#include "xbox_memory_layout.h"

#include <stdio.h>
#include <string.h>

recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { (void)xbox_va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

#define TITLE 0x4947002Bu

static int failures;
static char base[MAX_PATH];

static void check(int ok, const char *what, const char *detail)
{
    printf("  %-66s %s", what, ok ? "PASS" : "FAIL");
    if (!ok && detail) printf(" -- %s", detail);
    putchar('\n');
    if (!ok) failures++;
}

static int ends_with(const WCHAR *s, const WCHAR *tail)
{
    size_t a = wcslen(s), b = wcslen(tail);
    return a >= b && !_wcsicmp(s + a - b, tail);
}

static NTSTATUS open_path(const char *xbox_path, ULONG disposition, ULONG options, HANDLE *out)
{
    XBOX_ANSI_STRING name;
    XBOX_OBJECT_ATTRIBUTES oa;
    XBOX_IO_STATUS_BLOCK ios;
    memset(&oa, 0, sizeof oa);
    name.Length = (USHORT)strlen(xbox_path);
    name.MaximumLength = (USHORT)(name.Length + 1);
    name.Buffer = (PCHAR)xbox_path;
    oa.ObjectName = &name;
    *out = NULL;
    return xbox_NtCreateFile(out, 0x80100080u /* GENERIC_READ|SYNCHRONIZE|FILE_READ_ATTRIBUTES */, &oa, &ios, NULL,
                             0, 3, disposition, options);
}

int main(void)
{
    char tmp[MAX_PATH], game[MAX_PATH], saves[MAX_PATH], data[MAX_PATH], cache[MAX_PATH], f[MAX_PATH];
    WCHAR host[XBOX_HOST_PATH], img[XBOX_HOST_PATH];
    XboxPathConfig cfg;
    HANDLE h;
    NTSTATUS st;
    char d[256];

    setvbuf(stdout, NULL, _IONBF, 0);
    GetTempPathA(MAX_PATH, tmp);
    snprintf(base, sizeof base, "%skernel_utility_drive_%lu", tmp, (unsigned long)GetCurrentProcessId());
    snprintf(game, sizeof game, "%s\\game", base);
    snprintf(saves, sizeof saves, "%s\\saves", base);
    snprintf(data, sizeof data, "%s\\data", base);
    CreateDirectoryA(base, NULL); CreateDirectoryA(game, NULL); CreateDirectoryA(saves, NULL); CreateDirectoryA(data, NULL);

    xbox_path_config_default(&cfg);
    cfg.game_dir = game; cfg.save_dir = saves; cfg.data_dir = data; cfg.title_id = TITLE;
    xbox_path_init_cfg(&cfg);
    snprintf(cache, sizeof cache, "%s\\cache\\%08X", data, TITLE);

    /* 1 */
    check(xbox_translate_path("\\Device\\Harddisk0\\Partition5", img, XBOX_HOST_PATH) && ends_with(img, L"\\Partition5.img"),
          "Partition5 (no separator) is the device image", NULL);
    /* 2 */
    check(xbox_translate_path("\\Device\\Harddisk0\\Partition5\\", host, XBOX_HOST_PATH) && ends_with(host, L"\\cache\\4947002B"),
          "Partition5\\ is the volume root, data\\cache\\<title id>", NULL);
    check(xbox_translate_path("Z:\\Sprite", host, XBOX_HOST_PATH) && ends_with(host, L"\\cache\\4947002B\\Sprite"),
          "Z:\\Sprite lives under that root", NULL);
    check(xbox_translate_path("\\Device\\Harddisk0\\Partition0\\", host, XBOX_HOST_PATH) && ends_with(host, L"\\Partition0.img"),
          "Partition0\\ (whole disk) stays the device", NULL);

    /* 3 */
    st = open_path("\\Device\\Harddisk0\\Partition5\\", 1 /* FILE_OPEN */, 0x21 /* DIRECTORY | SYNC_ALERT */, &h);
    snprintf(d, sizeof d, "status 0x%08X", (unsigned)st);
    check(st == 0, "the root opens as a directory", d);
    if (h) CloseHandle(h);
    st = open_path("\\Device\\Harddisk0\\Partition5\\", 1, 0x20, &h);
    snprintf(d, sizeof d, "status 0x%08X", (unsigned)st);
    check(st == 0, "...and without FILE_DIRECTORY_FILE (free-space query)", d);
    if (h) CloseHandle(h);
    st = open_path("\\Device\\Harddisk0\\Partition5\\Sprite", 2 /* FILE_CREATE */, 0x21, &h);
    snprintf(f, sizeof f, "%s\\Sprite", cache);
    snprintf(d, sizeof d, "status 0x%08X", (unsigned)st);
    check(st == 0 && (GetFileAttributesA(f) & FILE_ATTRIBUTE_DIRECTORY) && GetFileAttributesA(f) != INVALID_FILE_ATTRIBUTES,
          "CreateDirectory(Z:\\Sprite) makes data\\cache\\<id>\\Sprite", d);
    if (h) CloseHandle(h);

    /* 3b: the mirror pass lists the DISC root first - FindFirstFile("d:\*.*") = NtOpenFile("\Device\CdRom0\") +
     *     NtQueryDirectoryFile("*.*"). It came back STATUS_NO_MORE_FILES on the first query in Orta's run, so
     *     the pass stopped before creating any Z: folder. */
    {
        char gf[MAX_PATH];
        HANDLE w, dh;
        XBOX_IO_STATUS_BLOCK ios;
        XBOX_ANSI_STRING mask;
        union { XBOX_FILE_DIRECTORY_INFORMATION e; char raw[512]; } info;
        snprintf(gf, sizeof gf, "%s\\default.xbe", game);
        w = CreateFileA(gf, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
        snprintf(gf, sizeof gf, "%s\\Sprite", game);
        CreateDirectoryA(gf, NULL);

        st = open_path("\\Device\\CdRom0\\", 1, 0x21, &dh);
        snprintf(d, sizeof d, "status 0x%08X", (unsigned)st);
        check(st == 0, "the disc root opens as a directory", d);
        mask.Buffer = (PCHAR)"*.*"; mask.Length = 3; mask.MaximumLength = 4;
        memset(&ios, 0, sizeof ios);
        st = dh ? xbox_NtQueryDirectoryFile(dh, NULL, NULL, NULL, &ios, &info, sizeof info,
                                            XboxFileDirectoryInformation, &mask, TRUE) : (NTSTATUS)-1;
        snprintf(d, sizeof d, "status 0x%08X", (unsigned)st);
        check(st == 0, "NtQueryDirectoryFile(\"*.*\") on the disc root finds an entry", d);
        /* An EMPTY mask (Length 0, Buffer set) means "everything" as well. */
        mask.Buffer = (PCHAR)""; mask.Length = 0; mask.MaximumLength = 1;
        memset(&ios, 0, sizeof ios);
        st = dh ? xbox_NtQueryDirectoryFile(dh, NULL, NULL, NULL, &ios, &info, sizeof info,
                                            XboxFileDirectoryInformation, &mask, TRUE) : (NTSTATUS)-1;
        snprintf(d, sizeof d, "status 0x%08X", (unsigned)st);
        check(st == 0, "...and with an empty mask (Length 0)", d);
        if (dh) CloseHandle(dh);
    }

    /* 4: put a file into the drive, then dismount something else, then the image */
    {
        char probe[MAX_PATH], other[MAX_PATH];
        HANDLE w, o;
        snprintf(probe, sizeof probe, "%s\\Sprite\\cached.bin", cache);
        w = CreateFileA(probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (w != INVALID_HANDLE_VALUE) CloseHandle(w);

        snprintf(other, sizeof other, "%s\\other.bin", data);
        o = CreateFileA(other, GENERIC_READ | GENERIC_WRITE, 7, NULL, CREATE_ALWAYS, 0, NULL);
        xbox_file_dismount(o);
        check(GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES, "dismounting another file empties nothing", NULL);
        CloseHandle(o);
        DeleteFileA(other);

        o = CreateFileW(img, GENERIC_READ | GENERIC_WRITE, 7, NULL, OPEN_ALWAYS, 0, NULL);
        st = xbox_file_dismount(o);
        check(st == 0 && GetFileAttributesA(probe) == INVALID_FILE_ATTRIBUTES && GetFileAttributesA(f) == INVALID_FILE_ATTRIBUTES
              && GetFileAttributesA(cache) != INVALID_FILE_ATTRIBUTES,
              "format (dismount of Partition5.img) empties the drive's folder", NULL);
        CloseHandle(o);
    }

    /* 5: everything above stayed inside the layout */
    {
        WIN32_FIND_DATAA fd;
        char pat[MAX_PATH];
        HANDLE fh;
        int stray = 0;
        snprintf(pat, sizeof pat, "%s\\*", base);
        fh = FindFirstFileA(pat, &fd);
        if (fh != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..") && strcmp(fd.cFileName, "game")
                        && strcmp(fd.cFileName, "saves") && strcmp(fd.cFileName, "data"))
                    stray = 1;
            } while (FindNextFileA(fh, &fd));
            FindClose(fh);
        }
        check(!stray, "the layout holds only game\\, saves\\, data\\", NULL);
    }

    /* clean up (best effort) */
    {
        char cmd[MAX_PATH * 2];
        snprintf(cmd, sizeof cmd, "rmdir /s /q \"%s\" >nul 2>nul", base);
        system(cmd);
    }
    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
