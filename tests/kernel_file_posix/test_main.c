/*
 * NtSetInformationFile on a POSIX host (2026-11-11: Orta's Z: cache writes each file as x.tmp with
 * OVERWRITE_IF, sets FileAllocationInformation (class 19), writes, renames; class 19 was unhandled on POSIX ->
 * STATUS_NOT_IMPLEMENTED on every cache copy).
 *
 * In a fresh temp folder, through the compat CreateFileA handle the kernel sees:
 *   1. allocation larger than the file: SUCCESS, end of file unchanged
 *   2. allocation smaller than the file: SUCCESS, the file is cut there
 *   3. end of file (class 20) still grows / cuts
 *   4. the Orta sequence: allocate, write, rename .tmp -> final name, content intact
 *   5. NtQueryDirectoryFile, Orta's Z: cache check (an Orta run under WSL, 2026-11-10: every hit after the first read as a
 *      miss): open folder / query one name / close, five times, all found (case-insensitive); "*.*" = every name
 *      incl. a dotless one; no mask; "*.txb"; a missing name gives NO_MORE_FILES
 *   6. atomic save writes (as tests/storage_layout on Windows): a U: save opened for writing goes to a pending
 *      copy (the save keeps the old data until the close commits it), in-place updates keep the rest, a run
 *      killed mid-save (a forked child ended by RECOMP_SAVE_CRASH_TEST at the commit) leaves the old save and the
 *      next start drops the copy, a rename while open commits under the new name and removes the old one, a
 *      delete disposition removes the save at close, Z: (cache) writes are not shadowed
 */
#include "kernel.h"
#include "xbox_memory_layout.h"
#include "win32_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { (void)xbox_va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

static int failures;
static char dir[512];

static void check(int ok, const char *what)
{
    printf("  %-62s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

static long long size_of(const char *name)
{
    char p[600];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return stat(p, &st) == 0 ? (long long)st.st_size : -1;
}

static HANDLE make(const char *name, const char *text)
{
    char p[600];
    DWORD w;
    HANDLE h;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    h = CreateFileA(p, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE && text)
        WriteFile(h, text, (DWORD)strlen(text), &w, NULL);
    return h;
}

static NTSTATUS set_size(HANDLE h, int cls, long long n)
{
    XBOX_IO_STATUS_BLOCK io;
    XBOX_FILE_END_OF_FILE_INFORMATION info;
    memset(&io, 0, sizeof io);
    info.EndOfFile.QuadPart = n;
    return xbox_NtSetInformationFile(h, &io, &info, sizeof info, (XBOX_FILE_INFORMATION_CLASS)cls);
}

/* ── case 6 helpers: a portable install in <dir>/inst ── */
static char GAME[600], SAVES[600], DATA[600], SAVE_FILE[700];

static void init_paths(void)
{
    XboxPathConfig c;
    xbox_path_config_default(&c);
    c.game_dir = GAME; c.save_dir = SAVES; c.data_dir = DATA; c.title_id = 0xABCD0001u;
    xbox_path_init_cfg(&c);
}

static int pending_count(void)
{
    char p[700];
    DIR *d;
    struct dirent *e;
    int n = 0;
    snprintf(p, sizeof p, "%s/.pending", SAVES);
    if (!(d = opendir(p)))
        return 0;
    while ((e = readdir(d)) != NULL)
        if (e->d_name[0] != '.')
            n++;
    closedir(d);
    return n;
}

static void read_host(const char *p, char *out, size_t n)
{
    FILE *f = fopen(p, "rb");
    size_t k = 0;
    if (f) {
        k = fread(out, 1, n - 1, f);
        fclose(f);
    }
    out[k] = 0;
}

static HANDLE xopen(const char *xbox, ACCESS_MASK access, ULONG disposition)
{
    XBOX_ANSI_STRING name;
    XBOX_OBJECT_ATTRIBUTES oa;
    XBOX_IO_STATUS_BLOCK io;
    HANDLE h = NULL;
    name.Length = (USHORT)strlen(xbox);
    name.MaximumLength = name.Length + 1;
    name.Buffer = (PCHAR)xbox;
    memset(&oa, 0, sizeof oa);
    oa.ObjectName = &name;
    if (xbox_NtCreateFile(&h, access, &oa, &io, NULL, 0, 3, disposition, 0) != 0)
        return NULL;
    return h;
}

static void xwrite(HANDLE h, const char *text, LONGLONG at)
{
    XBOX_IO_STATUS_BLOCK io;
    LARGE_INTEGER off;
    off.QuadPart = at;
    xbox_NtWriteFile(h, NULL, NULL, NULL, &io, (PVOID)text, (ULONG)strlen(text), &off);
}

static void case_atomic_saves(void)
{
    char b[16], p[700], q[700];
    HANDLE h;
    pid_t pid;
    int status = 0;

    snprintf(GAME, sizeof GAME, "%s/inst/game", dir);
    snprintf(SAVES, sizeof SAVES, "%s/inst/saves", dir);
    snprintf(DATA, sizeof DATA, "%s/inst/data", dir);
    snprintf(p, sizeof p, "%s/inst", dir);
    mkdir(p, 0755);
    mkdir(GAME, 0755);
    init_paths();
    snprintf(SAVE_FILE, sizeof SAVE_FILE, "%s/UDATA/ABCD0001/slot/a.sav", SAVES);
    snprintf(p, sizeof p, "%s/UDATA/ABCD0001/slot", SAVES);
    mkdir(p, 0755);
    {
        FILE *f = fopen(SAVE_FILE, "wb");
        if (f) { fputs("old", f); fclose(f); }
    }

    h = xopen("U:\\slot\\a.sav", XBOX_GENERIC_WRITE, XBOX_FILE_OVERWRITE_IF);
    check(h != NULL, "save opened for overwrite");
    xwrite(h, "new", 0);
    read_host(SAVE_FILE, b, sizeof b);
    check(!strcmp(b, "old"), "while open: the save still holds the old data");
    check(pending_count() == 1, "one pending copy");
    xbox_NtClose(h);
    read_host(SAVE_FILE, b, sizeof b);
    check(!strcmp(b, "new"), "after close: the new data is in place");
    check(pending_count() == 0, "pending copy committed");

    h = xopen("U:\\slot\\a.sav", XBOX_GENERIC_WRITE | XBOX_GENERIC_READ, XBOX_FILE_OPEN);
    xwrite(h, "X", 0);
    xbox_NtClose(h);
    read_host(SAVE_FILE, b, sizeof b);
    check(!strcmp(b, "Xew"), "in-place update keeps the rest of the file");

    /* killed at the commit: a child process, ended by the crash test point (exit 3) */
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        setenv("RECOMP_SAVE_CRASH_TEST", "A.SAV", 1);   /* case-insensitive */
        h = xopen("U:\\slot\\a.sav", XBOX_GENERIC_WRITE, XBOX_FILE_OVERWRITE_IF);
        xwrite(h, "half", 0);
        xbox_NtClose(h);
        _exit(0);                                         /* not reached when the crash point fired */
    }
    waitpid(pid, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 3, "child killed at the commit (exit 3)");
    read_host(SAVE_FILE, b, sizeof b);
    check(!strcmp(b, "Xew"), "killed mid-save: the old save is intact");
    check(pending_count() == 1, "the unfinished copy is still there ...");
    init_paths();                                         /* the next start */
    check(pending_count() == 0, "... and dropped at the next start");

    /* renamed while open: committed under the new name, the old name removed */
    h = xopen("U:\\slot\\a.sav", XBOX_GENERIC_WRITE | XBOX_GENERIC_READ, XBOX_FILE_OPEN);
    xwrite(h, "R", 0);
    check(xbox_file_rename(h, NULL, "b.sav", 1) == STATUS_SUCCESS, "rename a.sav -> b.sav while open");
    snprintf(q, sizeof q, "%s/UDATA/ABCD0001/slot/b.sav", SAVES);
    read_host(SAVE_FILE, b, sizeof b);
    check(!strcmp(b, "Xew"), "until the close: a.sav unchanged");
    xbox_NtClose(h);
    read_host(q, b, sizeof b);
    check(!strcmp(b, "Rew"), "after close: b.sav holds the new data");
    check(access(SAVE_FILE, F_OK) != 0, "a.sav removed");
    check(pending_count() == 0, "nothing left pending");

    /* delete disposition: the save goes at close */
    h = xopen("U:\\slot\\b.sav", XBOX_GENERIC_WRITE | XBOX_DELETE, XBOX_FILE_OPEN);
    {
        XBOX_IO_STATUS_BLOCK io;
        XBOX_FILE_DISPOSITION_INFORMATION di;
        memset(&io, 0, sizeof io);
        di.DeleteFile = TRUE;
        check(xbox_NtSetInformationFile(h, &io, &di, sizeof di, XboxFileDispositionInformation) == STATUS_SUCCESS,
              "delete disposition on a pending copy");
    }
    check(access(q, F_OK) == 0, "until the close: b.sav still there");
    xbox_NtClose(h);
    check(access(q, F_OK) != 0 && pending_count() == 0, "after close: b.sav and its copy gone");

    /* the cache drive is not a save */
    h = xopen("Z:\\z.bin", XBOX_GENERIC_WRITE, XBOX_FILE_OVERWRITE_IF);
    check(h != NULL && pending_count() == 0, "a Z: (cache) write is not shadowed");
    if (h)
        xbox_NtClose(h);
}

static HANDLE open_dir(void)
{
    return CreateFileA(dir, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

static NTSTATUS query(HANDLE d, const char *mask, char *name_out, size_t n)
{
    XBOX_IO_STATUS_BLOCK io;
    XBOX_ANSI_STRING s;
    static ULONG buf[256];
    PXBOX_FILE_DIRECTORY_INFORMATION e = (PXBOX_FILE_DIRECTORY_INFORMATION)buf;
    NTSTATUS st;
    memset(&io, 0, sizeof io);
    s.Buffer = (char *)mask;
    s.Length = mask ? (USHORT)strlen(mask) : 0;
    s.MaximumLength = s.Length;
    st = xbox_NtQueryDirectoryFile(d, NULL, NULL, NULL, &io, buf, sizeof buf, XboxFileDirectoryInformation,
                                   mask ? &s : NULL, FALSE);
    if (st == STATUS_SUCCESS && name_out)
        snprintf(name_out, n, "%.*s", (int)e->FileNameLength, e->FileName);
    return st;
}

/* open the folder, ask for one name, close it the way NtClose does */
static int query_one(const char *name, HANDLE *prev, int *reused)
{
    char got[128] = "";
    HANDLE d = open_dir();
    NTSTATUS st;
    if (d == INVALID_HANDLE_VALUE)
        return 0;
    if (*prev && d == *prev)
        (*reused)++;
    *prev = d;
    st = query(d, name, got, sizeof got);
    xbox_file_close_host(d);
    return st == STATUS_SUCCESS && !strcasecmp(got, name);
}

static int count_all(const char *mask)
{
    HANDLE d = open_dir();
    int k = 0;
    while (k < 64 && query(d, mask, NULL, 0) == STATUS_SUCCESS)
        k++;
    xbox_file_close_host(d);
    return k;
}

int main(void)
{
    HANDLE h;
    NTSTATUS st;
    DWORD w;
    char p[600], buf[32] = {0};
    FILE *f;

    setvbuf(stdout, NULL, _IONBF, 0);
    snprintf(dir, sizeof dir, "/tmp/kernel_file_posix_%ld", (long)getpid());
    mkdir(dir, 0755);

    h = make("a.bin", "0123456789");
    check(h != INVALID_HANDLE_VALUE, "create a.bin (10 bytes)");
    st = set_size(h, XboxFileAllocationInformation, 1 << 20);
    check(st == STATUS_SUCCESS, "allocation 1 MB > size: SUCCESS");
    check(size_of("a.bin") == 10, "allocation 1 MB > size: end of file stays 10");
    st = set_size(h, XboxFileAllocationInformation, 4);
    check(st == STATUS_SUCCESS, "allocation 4 < size: SUCCESS");
    check(size_of("a.bin") == 4, "allocation 4 < size: file cut to 4");
    st = set_size(h, XboxFileAllocationInformation, -1);
    check(st != STATUS_SUCCESS, "allocation -1: refused");
    st = set_size(h, XboxFileEndOfFileInformation, 100);
    check(st == STATUS_SUCCESS && size_of("a.bin") == 100, "end of file 100: grows");
    st = set_size(h, XboxFileEndOfFileInformation, 2);
    check(st == STATUS_SUCCESS && size_of("a.bin") == 2, "end of file 2: cuts");
    CloseHandle(h);

    /* the Orta cache sequence: x.tmp, allocate, write, rename onto the final name */
    h = make("laser.txb.tmp", NULL);
    st = set_size(h, XboxFileAllocationInformation, 4096);
    check(st == STATUS_SUCCESS, "cache .tmp: allocation 4096 SUCCESS");
    WriteFile(h, "LASERDATA", 9, &w, NULL);
    st = xbox_file_rename(h, NULL, "laser.txb", 1);
    check(st == STATUS_SUCCESS, "cache .tmp: rename onto laser.txb");
    CloseHandle(h);
    snprintf(p, sizeof p, "%s/laser.txb", dir);
    if ((f = fopen(p, "rb")) != NULL) {
        fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
    }
    check(!strcmp(buf, "LASERDATA") && size_of("laser.txb") == 9, "cache file: content and size intact");
    check(size_of("laser.txb.tmp") < 0, "cache file: .tmp gone");

    /* The Orta cache check: open the folder, query ONE name, close, next name - every one must be found, also
     * when the compat layer hands the next open the closed handle's value (the old search used to go on). */
    {
        static const char *names[] = { "laser_ref.txb", "RenderSettingBin01.rsb", "shotbb.txb", "nodot" };
        static const char *asks[] = { "laser.txb", "laser_ref.txb", "rendersettingbin01.RSB", "shotbb.txb", "nodot" };
        int i, hits = 0, reused = 0, all = 0;
        HANDLE prev = NULL;
        for (i = 0; i < 4; i++)
            CloseHandle(make(names[i], "x"));
        for (i = 0; i < 5; i++) {
            if (query_one(asks[i], &prev, &reused))
                hits++;
        }
        printf("  (handle value reused by the next open: %d of 4)\n", reused);
        check(hits == 5, "cache check: open/query one name/close x5, all found");
        /* "*.*" is every name, a dotless one too (as FindFirstFileW on the Windows host) */
        all = count_all("*.*");
        check(all == 6, "mask *.*: every entry (6, incl. the dotless one)");
        check(count_all(NULL) == 6, "no mask: every entry (6)");
        check(count_all("*.txb") == 3, "mask *.txb: 3 entries");
        check(!query_one("missing.txb", &prev, &reused), "missing name: NO_MORE_FILES");
        for (i = 0; i < 4; i++) {
            snprintf(p, sizeof p, "%s/%s", dir, names[i]);
            unlink(p);
        }
    }

    snprintf(p, sizeof p, "%s/a.bin", dir);
    unlink(p);
    snprintf(p, sizeof p, "%s/laser.txb", dir);
    unlink(p);

    case_atomic_saves();
    {
        char cmd[700];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);   /* the test's own temp tree */
        if (system(cmd) != 0)
            printf("  (could not remove %s)\n", dir);
    }
    if (failures) {
        printf("FAIL: %d check(s)\n", failures);
        return 1;
    }
    puts("PASS: POSIX NtSetInformationFile allocation, end of file, Orta cache sequence, directory queries, atomic saves");
    return 0;
}
