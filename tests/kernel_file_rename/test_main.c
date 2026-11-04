/*
 * NtSetInformationFile FileRenameInformation (class 10) - xbox_file_rename in kernel_file.c (2026-11-01:
 * Panzer Dragoon Orta writes each cache file as Z:\...\x.tmp and renames it; the class was unhandled).
 *
 * Host-level, in a fresh temp folder: an open file renamed by a bare name (its own directory), by a name relative to
 * a directory handle, refused onto an existing name without ReplaceIfExists, allowed with it; the handle keeps
 * working after the rename. Object paths ("\??\Z:\...") go through xbox_translate_path, which the path tests cover.
 */
#include "kernel.h"
#include "xbox_memory_layout.h"

#include <stdio.h>
#include <string.h>

recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { (void)xbox_va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

static int failures;
static WCHAR dir[MAX_PATH];

static void check(int ok, const char *what, const char *detail)
{
    printf("  %-62s %s", what, ok ? "PASS" : "FAIL");
    if (!ok && detail) printf(" -- %s", detail);
    putchar('\n');
    if (!ok) failures++;
}

static void path(WCHAR *out, const WCHAR *name) { swprintf(out, MAX_PATH, L"%s\\%s", dir, name); }
static int exists(const WCHAR *name) { WCHAR p[MAX_PATH]; path(p, name); return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

static HANDLE make(const WCHAR *name, const char *text)
{
    WCHAR p[MAX_PATH];
    DWORD w;
    HANDLE h;
    path(p, name);
    /* As the title's open reaches Win32: DELETE in the access (XAPI MoveFile opens with it), share delete. */
    h = CreateFileW(p, GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, CREATE_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE && text)
        WriteFile(h, text, (DWORD)strlen(text), &w, NULL);
    return h;
}

int main(void)
{
    WCHAR tmp[MAX_PATH], sub[MAX_PATH];
    HANDLE a, b, d;
    NTSTATUS st;
    char det[128], buf[16] = {0};
    DWORD r;

    setvbuf(stdout, NULL, _IONBF, 0);
    GetTempPathW(MAX_PATH, tmp);
    swprintf(dir, MAX_PATH, L"%skernel_file_rename_%lu", tmp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryW(dir, NULL);

    /* 1. bare name: the file's own directory */
    a = make(L"x.tmp", "cached");
    st = xbox_file_rename(a, NULL, "pdx01.cdx", 0);
    snprintf(det, sizeof det, "status 0x%08X", (unsigned)st);
    check(st == 0 && exists(L"pdx01.cdx") && !exists(L"x.tmp"), "bare name: renamed in its own directory", det);
    SetFilePointer(a, 0, NULL, FILE_BEGIN);
    check(ReadFile(a, buf, 6, &r, NULL) && r == 6 && !memcmp(buf, "cached", 6),
          "the handle still reads the file after the rename", NULL);
    CloseHandle(a);

    /* 2. onto an existing name: refused without ReplaceIfExists, done with it */
    b = make(L"y.tmp", "new");
    st = xbox_file_rename(b, NULL, "pdx01.cdx", 0);
    snprintf(det, sizeof det, "status 0x%08X", (unsigned)st);
    check(st == (NTSTATUS)0xC0000035u && exists(L"y.tmp"), "existing target, no replace: OBJECT_NAME_COLLISION", det);
    st = xbox_file_rename(b, NULL, "pdx01.cdx", 1);
    snprintf(det, sizeof det, "status 0x%08X", (unsigned)st);
    check(st == 0 && !exists(L"y.tmp") && exists(L"pdx01.cdx"), "existing target, ReplaceIfExists: replaced", det);
    CloseHandle(b);

    /* 3. relative to a directory handle (RootDirectory) */
    swprintf(sub, MAX_PATH, L"%s\\sprite", dir);
    CreateDirectoryW(sub, NULL);
    d = CreateFileW(sub, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    a = make(L"z.tmp", "spr");
    st = xbox_file_rename(a, d, "pdx01_ge.spr", 0);
    snprintf(det, sizeof det, "status 0x%08X", (unsigned)st);
    check(st == 0 && exists(L"sprite\\pdx01_ge.spr") && !exists(L"z.tmp"), "RootDirectory: renamed into that folder", det);
    CloseHandle(a);
    CloseHandle(d);

    /* 4. bad arguments */
    check(xbox_file_rename(NULL, NULL, "a", 0) == (NTSTATUS)0xC000000Du, "no handle: INVALID_PARAMETER", NULL);

    /* clean up */
    {
        WCHAR p[MAX_PATH];
        path(p, L"pdx01.cdx"); DeleteFileW(p);
        path(p, L"sprite\\pdx01_ge.spr"); DeleteFileW(p);
        RemoveDirectoryW(sub);
        RemoveDirectoryW(dir);
    }
    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
