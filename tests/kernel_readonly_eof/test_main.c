/*
 * Setting the end of file or the allocation on a read-only handle is denied (upstream sp00nz v0.17.0 #245,
 * BearddOddity d8611d8; option-c has no kernel_fatx_overwrite test, so it gets this one, 2026-11-19, U2).
 *
 * A 3588-byte file opened with GENERIC_READ: FileEndOfFileInformation and FileAllocationInformation answer
 * STATUS_ACCESS_DENIED and the file keeps its bytes. Opened with GENERIC_WRITE the same calls still work.
 */
#include "kernel.h"
#include "xbox_memory_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

recomp_func_t recomp_lookup(uint32_t a);
recomp_func_t recomp_lookup(uint32_t a) { (void)a; abort(); }
recomp_func_t recomp_lookup_manual(uint32_t a);
recomp_func_t recomp_lookup_manual(uint32_t a) { (void)a; abort(); }

static int failures;

static void check(int ok, const char *what)
{
    printf("  %-62s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

static NTSTATUS open_path(const char *xbox_path, ACCESS_MASK access, HANDLE *h)
{
    XBOX_ANSI_STRING name;
    XBOX_OBJECT_ATTRIBUTES attrs;
    XBOX_IO_STATUS_BLOCK ios;
    name.Length = name.MaximumLength = (USHORT)strlen(xbox_path);
    name.Buffer = (PCHAR)xbox_path;
    attrs.RootDirectory = NULL;
    attrs.ObjectName = &name;
    attrs.Attributes = 0;
    *h = NULL;
    return xbox_NtCreateFile(h, access, &attrs, &ios, NULL, 0, 1, XBOX_FILE_OPEN, XBOX_FILE_NON_DIRECTORY_FILE);
}

static NTSTATUS set_size(HANDLE h, int cls, LONGLONG size)
{
    XBOX_IO_STATUS_BLOCK ios;
    XBOX_FILE_END_OF_FILE_INFORMATION info;
    info.EndOfFile.QuadPart = size;
    return xbox_NtSetInformationFile(h, &ios, &info, sizeof info, cls);
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

int main(void)
{
    char root[MAX_PATH], dir[MAX_PATH], file[MAX_PATH];
    HANDLE h;
    FILE *f;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    GetTempPathA(MAX_PATH, root);
    sprintf(dir, "%sxrecomp-ro-eof-%lu", root, GetCurrentProcessId());
    sprintf(file, "%s\\save.dat", dir);
    if (!CreateDirectoryA(dir, NULL)) return 10;
    f = fopen(file, "wb");
    if (!f) return 11;
    for (i = 0; i < 3588; i++) fputc(i & 0xFF, f);
    fclose(f);
    xbox_path_init(dir, NULL);

    check(open_path("D:\\save.dat", GENERIC_READ, &h) == 0, "open read-only");
    check(set_size(h, XboxFileEndOfFileInformation, 100) == STATUS_ACCESS_DENIED, "end of file on a read-only handle: denied");
    check(set_size(h, XboxFileAllocationInformation, 100) == STATUS_ACCESS_DENIED, "allocation on a read-only handle: denied");
    xbox_NtClose(h);
    check(file_size(file) == 3588, "the file keeps its 3588 bytes");

    check(open_path("D:\\save.dat", GENERIC_READ | GENERIC_WRITE, &h) == 0, "open for writing");
    check(set_size(h, XboxFileEndOfFileInformation, 2000) == 0, "end of file on a writable handle: done");
    xbox_NtClose(h);
    check(file_size(file) == 2000, "the file is 2000 bytes then");

    DeleteFileA(file);
    RemoveDirectoryA(dir);
    printf("%s (%d failure%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
