/*
 * kernel_file.c - Xbox File I/O
 *
 * Implements the Nt*File kernel functions. All Xbox device paths are
 * translated through kernel_path.c before use.
 *
 * The Xbox kernel uses NT-style file I/O with ANSI strings in
 * OBJECT_ATTRIBUTES (unlike Windows NT, which uses Unicode).
 *
 * Two backends:
 *   _WIN32  -> Win32 CreateFileW / ReadFile / FindFirstFileW ...
 *   POSIX   -> open / read / write / stat / opendir ...
 * The Xbox semantics (disposition mapping, IO_STATUS_BLOCK, info classes)
 * are identical on both; only the host syscalls differ.
 */

#ifndef _GNU_SOURCE   /* HotPathLogging.cmake sets it for the whole library on POSIX */
#define _GNU_SOURCE   /* FNM_CASEFOLD */
#endif
#include "kernel.h"
#include "xbox_memory_layout.h"
#include "frame_time.h"           /* xbox_ft_io: file reads in the [hitch] lines */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "log_async.h"   /* xbox_log_drain; also force-included, but tests/kernel_directory includes this file */

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <fnmatch.h>
#endif

/* Get the ANSI path from OBJECT_ATTRIBUTES (platform-independent). */
static const char* get_xbox_path(PXBOX_OBJECT_ATTRIBUTES ObjectAttributes)
{
    if (!ObjectAttributes || !ObjectAttributes->ObjectName ||
        !ObjectAttributes->ObjectName->Buffer)
        return NULL;
    return ObjectAttributes->ObjectName->Buffer;
}

/* Xbox volume geometry.
 *
 * FATX uses 16 KB clusters: 512-byte sectors, 32 sectors per cluster. That is
 * not cosmetic. A title's CRT startup asks for FileFsSizeInformation and
 * multiplies SectorsPerAllocationUnit by BytesPerSector, then *requires* the
 * product to equal the cluster size it was built for. Half-Life 2 checks for
 * 0x4000 and returns STATUS_DEVICE_NOT_READY (0xC000014F) otherwise, which
 * aborts CRT init before main ever runs -- the process then exits cleanly,
 * which reads as a title that did nothing rather than one that failed.
 *
 * Reporting the host's PC-typical 4 KB cluster (512 x 8) fails that check. */
#define XBOX_BYTES_PER_SECTOR       512u
#define XBOX_SECTORS_PER_CLUSTER    32u      /* 512 * 32 = 16384 */

/* ======================================================================== */
/* Shared by both backends. kernel_bridge.c calls xbox_LastFileError()
 * unconditionally, so defining it only under _WIN32 leaves every POSIX link
 * with an undefined symbol -- invisible until something links an executable,
 * because a static archive never resolves its own references.
 *
 * The POSIX backend does not populate this yet, so it reads 0 there; that is a
 * missing detail rather than a missing symbol, and it is visible here. */
uint32_t g_xbox_last_file_error;

uint32_t xbox_LastFileError(void)
{
    return g_xbox_last_file_error;
}

/* IO_STATUS_BLOCK.Information after a successful NtCreateFile/NtOpenFile:
 * FILE_SUPERSEDED 0, FILE_OPENED 1, FILE_CREATED 2, FILE_OVERWRITTEN 3. */
static ULONG_PTR xbox_create_information(ULONG Disposition, int existed)
{
    switch (Disposition) {
        case XBOX_FILE_SUPERSEDE:    return existed ? 0 : 2;
        case XBOX_FILE_CREATE:       return 2;
        case XBOX_FILE_OPEN_IF:      return existed ? 1 : 2;
        case XBOX_FILE_OVERWRITE:    return 3;
        case XBOX_FILE_OVERWRITE_IF: return existed ? 3 : 2;
        default:                     return 1;
    }
}

#if defined(_WIN32)
/* ====================  Win32 backend  =================================== */
/* ======================================================================== */

/* Convert Xbox create disposition to Win32 */
static DWORD xbox_disposition_to_win32(ULONG Disposition)
{
    switch (Disposition) {
        case XBOX_FILE_SUPERSEDE:    return CREATE_ALWAYS;
        case XBOX_FILE_OPEN:         return OPEN_EXISTING;
        case XBOX_FILE_CREATE:       return CREATE_NEW;
        case XBOX_FILE_OPEN_IF:      return OPEN_ALWAYS;
        case XBOX_FILE_OVERWRITE:    return TRUNCATE_EXISTING;
        case XBOX_FILE_OVERWRITE_IF: return CREATE_ALWAYS;
        default:                     return OPEN_EXISTING;
    }
}

/* Convert Xbox access mask to Win32 */
static DWORD xbox_access_to_win32(ACCESS_MASK Access)
{
    DWORD result = 0;
    if (Access & XBOX_GENERIC_READ)           result |= GENERIC_READ;
    if (Access & XBOX_GENERIC_WRITE)          result |= GENERIC_WRITE;
    if (Access & XBOX_GENERIC_ALL)            result |= GENERIC_ALL;
    if (Access & XBOX_FILE_READ_DATA)         result |= FILE_READ_DATA;
    if (Access & XBOX_FILE_WRITE_DATA)        result |= FILE_WRITE_DATA;
    if (Access & XBOX_FILE_APPEND_DATA)       result |= FILE_APPEND_DATA;
    if (Access & XBOX_FILE_READ_ATTRIBUTES)   result |= FILE_READ_ATTRIBUTES;
    if (Access & XBOX_FILE_WRITE_ATTRIBUTES)  result |= FILE_WRITE_ATTRIBUTES;
    if (Access & XBOX_SYNCHRONIZE)            result |= SYNCHRONIZE;
    if (Access & XBOX_DELETE)                  result |= DELETE;
    if (result == 0 || result == SYNCHRONIZE)
        result |= GENERIC_READ;
    return result;
}

/* Convert Xbox share access to Win32 */
static DWORD xbox_share_to_win32(ULONG Share)
{
    DWORD result = 0;
    if (Share & 0x01) result |= FILE_SHARE_READ;
    if (Share & 0x02) result |= FILE_SHARE_WRITE;
    if (Share & 0x04) result |= FILE_SHARE_DELETE;
    return result;
}

/* ── Atomic save writes ──────────────────────────────────────────────────
 *
 * A save file opened for writing is not written in place: the handle the
 * title gets is a copy in save_dir\.pending\, and closing it moves the copy
 * over the original (MoveFileEx with REPLACE_EXISTING | WRITE_THROUGH). A run
 * killed mid-save -- power loss, a crash, the window closed -- leaves the
 * previous save as it was; the unfinished copy is dropped at the next start
 * (kernel_path.c). On the console the same interruption corrupts the save,
 * which the title then offers to delete.
 *
 * Only files under save_dir, only for write access, never directories or
 * delete-on-close opens. Contents the title keeps (FILE_OPEN, FILE_OPEN_IF)
 * are copied first, so in-place updates behave as before. */
typedef struct {
    HANDLE h;
    int    delete_final;
    int    delete_old;                  /* renamed while open: old_final goes when the copy is committed */
    WCHAR  tmp[XBOX_HOST_PATH];
    WCHAR  final_path[XBOX_HOST_PATH];
    WCHAR  old_final[XBOX_HOST_PATH];
} SaveShadow;

#define SAVE_SHADOW_MAX 64
static SaveShadow s_shadow[SAVE_SHADOW_MAX];
static SRWLOCK    s_shadow_lock = SRWLOCK_INIT;
static LONG       s_shadow_seq;

static SaveShadow *shadow_find(HANDLE h)
{
    int i;
    for (i = 0; i < SAVE_SHADOW_MAX; i++)
        if (s_shadow[i].h == h && h)
            return &s_shadow[i];
    return NULL;
}

/* Open win_path for writing through a pending copy. Returns the handle, or
 * INVALID_HANDLE_VALUE with GetLastError set (the caller maps it), or NULL
 * when the open should go the normal way. */
static HANDLE shadow_open(const WCHAR *win_path, DWORD access, DWORD share,
                          ULONG disposition, DWORD flags)
{
    DWORD attr = GetFileAttributesW(win_path);
    int exists = attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    int keep = disposition == XBOX_FILE_OPEN || disposition == XBOX_FILE_OPEN_IF;
    const WCHAR *name = wcsrchr(win_path, L'\\');
    SaveShadow *sh = NULL;
    HANDLE h;
    int i;

    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        return NULL;
    if (!exists && (disposition == XBOX_FILE_OPEN || disposition == XBOX_FILE_OVERWRITE))
        return NULL;                        /* the normal open reports "not found" */
    if (exists && disposition == XBOX_FILE_CREATE) {
        SetLastError(ERROR_ALREADY_EXISTS);
        return INVALID_HANDLE_VALUE;
    }

    AcquireSRWLockExclusive(&s_shadow_lock);
    for (i = 0; i < SAVE_SHADOW_MAX && !sh; i++)
        if (!s_shadow[i].h)
            sh = &s_shadow[i];
    if (!sh) {
        ReleaseSRWLockExclusive(&s_shadow_lock);
        return NULL;                        /* table full: write in place */
    }
    swprintf_s(sh->tmp, XBOX_HOST_PATH, L"%s\\%ld_%s", xbox_path_pending_dir(),
               InterlockedIncrement(&s_shadow_seq), name ? name + 1 : L"save");
    wcscpy_s(sh->final_path, XBOX_HOST_PATH, win_path);
    sh->delete_final = 0;
    sh->delete_old = 0;
    sh->old_final[0] = 0;
    if (keep && exists && !CopyFileW(win_path, sh->tmp, FALSE)) {
        ReleaseSRWLockExclusive(&s_shadow_lock);
        return NULL;
    }
    h = CreateFileW(sh->tmp, access | GENERIC_READ, share, NULL,
                    keep && exists ? OPEN_EXISTING : CREATE_ALWAYS, flags, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DeleteFileW(sh->tmp);
        ReleaseSRWLockExclusive(&s_shadow_lock);
        return NULL;
    }
    sh->h = h;
    ReleaseSRWLockExclusive(&s_shadow_lock);
    return h;
}

/* A delete-disposition set on a pending copy deletes the save on close. */
static int shadow_mark_delete(HANDLE h, int del)
{
    SaveShadow *sh;
    AcquireSRWLockExclusive(&s_shadow_lock);
    sh = shadow_find(h);
    if (sh) sh->delete_final = del;
    ReleaseSRWLockExclusive(&s_shadow_lock);
    return sh != NULL;
}

void xbox_file_close_host(void *host_handle)
{
    HANDLE h = (HANDLE)host_handle;
    SaveShadow done;
    SaveShadow *sh;

    if (!h || h == INVALID_HANDLE_VALUE)
        return;
    xbox_dir_context_drop(h);
    AcquireSRWLockExclusive(&s_shadow_lock);
    sh = shadow_find(h);
    if (sh) {
        done = *sh;
        sh->h = NULL;
    }
    ReleaseSRWLockExclusive(&s_shadow_lock);
    if (sh)
        FlushFileBuffers(h);
    CloseHandle(h);
    if (!sh)
        return;
    /* RECOMP_SAVE_CRASH_TEST=<text>: end the process the moment a save write
     * whose path contains <text> (case-insensitive) is about to be committed
     * -- a deterministic "killed mid-save" for the storage tests (the old save
     * must survive, the copy is dropped at the next start). A text, not a
     * flag: titles also write small probe files (THPS 2X: this.sux) at boot. */
    if (!done.delete_final) {
        static int init;
        static WCHAR needle[64];
        if (!init) {
            const char *e = getenv("RECOMP_SAVE_CRASH_TEST");
            init = 1;
            if (e && *e)
                MultiByteToWideChar(CP_UTF8, 0, e, -1, needle, 64);
            _wcsupr_s(needle, 64);
        }
        if (needle[0]) {
            WCHAR up[XBOX_HOST_PATH];
            wcscpy_s(up, XBOX_HOST_PATH, done.final_path);
            _wcsupr_s(up, XBOX_HOST_PATH);
            if (wcsstr(up, needle)) {
                fprintf(stderr, "  [STORAGE] RECOMP_SAVE_CRASH_TEST: terminating before %S is committed\n",
                        done.final_path);
                xbox_log_drain();   /* TerminateProcess kills the logger thread (log_async.h) */
                TerminateProcess(GetCurrentProcess(), 3);
            }
        }
    }
    if (done.delete_final) {
        DeleteFileW(done.tmp);
        DeleteFileW(done.final_path);
    } else if (!MoveFileExW(done.tmp, done.final_path,
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_FILE,
                 "save write not committed: %S -> %S (err=%u); the old save is kept",
                 done.tmp, done.final_path, GetLastError());
    } else if (done.delete_old) {
        DeleteFileW(done.old_final);      /* renamed while open: the old name is gone now */
    }
}

/* FileRenameInformation (2026-11-01). Panzer Dragoon Orta writes each cached file as Z:\...\x.tmp and
 * renames it into place; the class was unhandled, so every cache file stayed .tmp and the title copied again.
 * `xbox_name` is the title's name: an object path ("\??\Z:\...", "Z:\..."), or a bare name relative to `root`
 * (a directory handle) or, with no root, to the file's own directory. */
static BOOL rename_target(HANDLE file, HANDLE root, const char *xbox_name, WCHAR *out, DWORD n)
{
    HANDLE base = root;
    DWORD used;
    WCHAR *slash;

    if (xbox_name[0] == '\\' || (xbox_name[0] && xbox_name[1] == ':'))
        return xbox_translate_path(xbox_name, out, n);
    if (!base)
        base = file;
    used = GetFinalPathNameByHandleW(base, out, n, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!used || used + 2 >= n)
        return FALSE;
    if (!root) {                                    /* the file's directory */
        slash = wcsrchr(out, L'\\');
        if (!slash)
            return FALSE;
        used = (DWORD)(slash - out);
        out[used] = 0;
    }
    if (out[used - 1] != L'\\')
        out[used++] = L'\\';
    return MultiByteToWideChar(1252, 0, xbox_name, -1, out + used, (int)(n - used)) != 0;   /* Xbox: Latin-1 */
}

NTSTATUS xbox_file_dismount(HANDLE h)
{
    WCHAR p[XBOX_HOST_PATH];
    if (h && h != INVALID_HANDLE_VALUE
            && GetFinalPathNameByHandleW(h, p, XBOX_HOST_PATH, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS))
        xbox_path_utility_formatted(p);
    return STATUS_SUCCESS;
}

NTSTATUS xbox_file_rename(HANDLE file, HANDLE root, const char *xbox_name, int replace)
{
    WCHAR target[XBOX_HOST_PATH], source[XBOX_HOST_PATH];
    DWORD attr;
    SaveShadow *sh;

    if (!file || file == INVALID_HANDLE_VALUE || !xbox_name || !xbox_name[0])
        return STATUS_INVALID_PARAMETER;
    if (!rename_target(file, root, xbox_name, target, XBOX_HOST_PATH))
        return STATUS_OBJECT_PATH_NOT_FOUND;
    attr = GetFileAttributesW(target);
    if (attr != INVALID_FILE_ATTRIBUTES && !replace)
        return STATUS_OBJECT_NAME_COLLISION;

    /* A save written through a pending copy: the commit at close goes to the new name, the old one is removed. */
    AcquireSRWLockExclusive(&s_shadow_lock);
    sh = shadow_find(file);
    if (sh) {
        if (GetFileAttributesW(sh->final_path) != INVALID_FILE_ATTRIBUTES && !sh->delete_old) {
            wcscpy_s(sh->old_final, XBOX_HOST_PATH, sh->final_path);
            sh->delete_old = 1;
        }
        wcscpy_s(sh->final_path, XBOX_HOST_PATH, target);
        ReleaseSRWLockExclusive(&s_shadow_lock);
        return STATUS_SUCCESS;
    }
    ReleaseSRWLockExclusive(&s_shadow_lock);

    /* An open file: by handle when the open had DELETE access, else by name (the open shares delete). */
    {
        size_t len = wcslen(target);
        size_t sz = sizeof(FILE_RENAME_INFO) + len * sizeof(WCHAR);
        FILE_RENAME_INFO *ri = (FILE_RENAME_INFO *)calloc(1, sz);
        BOOL ok = FALSE;
        if (ri) {
            ri->ReplaceIfExists = replace ? TRUE : FALSE;
            ri->RootDirectory = NULL;
            ri->FileNameLength = (DWORD)(len * sizeof(WCHAR));
            memcpy(ri->FileName, target, (len + 1) * sizeof(WCHAR));
            ok = SetFileInformationByHandle(file, FileRenameInfo, ri, (DWORD)sz);
            free(ri);
        }
        if (!ok && GetFinalPathNameByHandleW(file, source, XBOX_HOST_PATH, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS))
            ok = MoveFileExW(source, target, replace ? MOVEFILE_REPLACE_EXISTING : 0);
        if (!ok) {
            DWORD err = GetLastError();
            fprintf(stderr, "  [FILE] rename to %S refused by the host (err=%lu)\n", target, (unsigned long)err);
            switch (err) {
            case ERROR_ALREADY_EXISTS:
            case ERROR_FILE_EXISTS:    return STATUS_OBJECT_NAME_COLLISION;
            case ERROR_PATH_NOT_FOUND: return STATUS_OBJECT_PATH_NOT_FOUND;
            case ERROR_ACCESS_DENIED:  return STATUS_ACCESS_DENIED;
            default:                   return STATUS_UNSUCCESSFUL;
            }
        }
    }
    return STATUS_SUCCESS;
}

/* Translate an Xbox OBJECT_ATTRIBUTES path to a Win32 wide path */
static BOOL translate_obj_path(PXBOX_OBJECT_ATTRIBUTES ObjectAttributes,
                               WCHAR* win_path, DWORD buf_size)
{
    const char* xbox_path = get_xbox_path(ObjectAttributes);
    if (!xbox_path)
        return FALSE;
    if (ObjectAttributes->RootDirectory && xbox_path[0] != '\\' &&
        !(xbox_path[0] && xbox_path[1] == ':')) {
        /* XDeleteSaveGame opens each child relative to its save directory. */
        DWORD used = GetFinalPathNameByHandleW(ObjectAttributes->RootDirectory,
            win_path, buf_size, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (!used || used >= buf_size || used + 1 >= buf_size) return FALSE;
        if (win_path[used - 1] != L'\\') win_path[used++] = L'\\';
        int count = MultiByteToWideChar(1252, 0, xbox_path, -1,   /* Xbox names: Latin-1 */
            win_path + used, (int)(buf_size - used));
        return count != 0;
    }
    return xbox_translate_path(xbox_path, win_path, buf_size);
}

NTSTATUS __stdcall xbox_NtCreateFile(
    PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
    PXBOX_OBJECT_ATTRIBUTES ObjectAttributes, PXBOX_IO_STATUS_BLOCK IoStatusBlock,
    PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess,
    ULONG CreateDisposition, ULONG CreateOptions)
{
    WCHAR win_path[XBOX_HOST_PATH];
    HANDLE h;
    DWORD flags_and_attrs = FILE_ATTRIBUTE_NORMAL;
    (void)AllocationSize;

    if (!FileHandle || !ObjectAttributes)
        return STATUS_INVALID_PARAMETER;

    if (!translate_obj_path(ObjectAttributes, win_path, XBOX_HOST_PATH)) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_FILE, "NtCreateFile: path translation failed");
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }

    /* A partition device opened as a directory.
     *
     * The path layer maps \Device\Harddisk0\PartitionN to a PartitionN.img
     * backing file, but a title asking for free space opens the partition with
     * FILE_DIRECTORY_FILE | FILE_OPEN_FOR_FREE_SPACE_QUERY -- it wants the
     * volume, not the bytes. Opening a regular file as a directory fails, and
     * the title reads STATUS_OBJECT_PATH_NOT_FOUND as "no such volume".
     *
     * Half-Life 2's CRT probes partition0 this way during startup and treats
     * the failure as fatal. Redirecting to the directory that holds the image
     * gives a handle that is valid for exactly what the caller is going to do
     * with it, which is NtQueryVolumeInformationFile. */
    if ((CreateOptions & XBOX_FILE_DIRECTORY_FILE) &&
        GetFileAttributesW(win_path) != INVALID_FILE_ATTRIBUTES &&
        !(GetFileAttributesW(win_path) & FILE_ATTRIBUTE_DIRECTORY)) {
        WCHAR *slash = wcsrchr(win_path, L'\\');
        if (slash && slash != win_path) {
            *slash = 0;
            xbox_log(XBOX_LOG_INFO, XBOX_LOG_FILE,
                     "NtCreateFile: directory open of a device image, "
                     "using its containing directory instead");
        }
    }

    /* Existence before the open decides the NT result: FILE_CREATE on an
     * existing name is a collision (also for directories -- CreateDirectoryW's
     * ERROR_ALREADY_EXISTS used to be ignored, so a title's "create the save
     * folder, fail if it exists" check always succeeded), and Information
     * says created / opened / overwritten. */
    int existed = GetFileAttributesW(win_path) != INVALID_FILE_ATTRIBUTES;

    if (CreateOptions & XBOX_FILE_DIRECTORY_FILE) {
        if (existed && CreateDisposition == XBOX_FILE_CREATE) {
            g_xbox_last_file_error = ERROR_ALREADY_EXISTS;
            XBOX_TRACE(XBOX_LOG_FILE, "NtCreateFile: %S exists (FILE_CREATE)", win_path);
            if (IoStatusBlock) {
                IoStatusBlock->Status = STATUS_OBJECT_NAME_COLLISION;
                IoStatusBlock->Information = 4; /* FILE_EXISTS */
            }
            return STATUS_OBJECT_NAME_COLLISION;
        }
        if (!existed && (CreateDisposition == XBOX_FILE_CREATE || CreateDisposition == XBOX_FILE_OPEN_IF))
            CreateDirectoryW(win_path, NULL);
        h = CreateFileW(win_path, xbox_access_to_win32(DesiredAccess),
            xbox_share_to_win32(ShareAccess), NULL, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, NULL);
    } else {
        /* NT opens an existing directory without FILE_DIRECTORY_FILE too (only FILE_NON_DIRECTORY_FILE, 0x40,
         * refuses it); Win32 needs FILE_FLAG_BACKUP_SEMANTICS for that. XAPI's free-space query opens a volume
         * root this way ("\Device\Harddisk0\Partition5\", now a folder rather than the partition image). */
        if (existed && (GetFileAttributesW(win_path) & FILE_ATTRIBUTE_DIRECTORY) && !(CreateOptions & 0x40))
            flags_and_attrs |= FILE_FLAG_BACKUP_SEMANTICS;
        if (CreateOptions & XBOX_FILE_NO_INTERMEDIATE_BUFFERING)
            flags_and_attrs |= FILE_FLAG_NO_BUFFERING;
        if (FileAttributes & XBOX_FILE_ATTRIBUTE_READONLY)
            flags_and_attrs |= FILE_ATTRIBUTE_READONLY;
        DWORD access = xbox_access_to_win32(DesiredAccess);
        h = NULL;
        if ((access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA))
            && !(CreateOptions & XBOX_FILE_DELETE_ON_CLOSE)
            && xbox_path_in_save_dir(win_path))
            h = shadow_open(win_path, access, xbox_share_to_win32(ShareAccess),
                            CreateDisposition, flags_and_attrs);
        if (!h)
            h = CreateFileW(win_path, access,
                xbox_share_to_win32(ShareAccess), NULL,
                xbox_disposition_to_win32(CreateDisposition), flags_and_attrs, NULL);
    }

    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        /* Kept for the caller's trace. An NTSTATUS says "it did not open";
         * only the Win32 error distinguishes a title probing for a file that
         * is genuinely absent from one it cannot open because this runtime
         * already holds it open with a share mode the second open forbids --
         * and those need opposite responses. */
        g_xbox_last_file_error = (uint32_t)err;
        /* A warning, not a compiled-out trace: a failed open is how a title
         * silently decides a volume or asset is missing, and in a Release
         * build that decision was invisible. */
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_FILE,
                 "NtCreateFile FAILED: %S (err=%u)", win_path, err);
        if (IoStatusBlock) {
            IoStatusBlock->Status = STATUS_OBJECT_NAME_NOT_FOUND;
            IoStatusBlock->Information = 0;
        }
        switch (err) {
            case ERROR_FILE_NOT_FOUND: return STATUS_OBJECT_NAME_NOT_FOUND;
            case ERROR_PATH_NOT_FOUND: return STATUS_OBJECT_PATH_NOT_FOUND;
            case ERROR_ACCESS_DENIED:  return STATUS_ACCESS_DENIED;
            case ERROR_ALREADY_EXISTS:
            case ERROR_FILE_EXISTS:    return STATUS_OBJECT_NAME_COLLISION;
            default:                   return STATUS_UNSUCCESSFUL;
        }
    }

    *FileHandle = h;
    if (IoStatusBlock) {
        IoStatusBlock->Status = STATUS_SUCCESS;
        IoStatusBlock->Information = xbox_create_information(CreateDisposition, existed);
    }
    XBOX_TRACE(XBOX_LOG_FILE, "NtCreateFile: %S -> handle=%p", win_path, h);
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtReadFile(
    HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
    PXBOX_IO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length,
    PLARGE_INTEGER ByteOffset)
{
    DWORD bytes_read = 0;
    BOOL result;
    OVERLAPPED ov;
    long long t0 = xbox_ft_on() ? xbox_ft_now() : 0;   /* the renderer's [hitch] lines */
    (void)ApcRoutine; (void)ApcContext;

    if (!IoStatusBlock)
        return STATUS_INVALID_PARAMETER;
    /* The OS writes the buffer: a page armed by page tracking would fail the
     * read (an error, not a fault) -- release it first. */
    xbox_PageTrackHostWrite(Buffer, Length);

    if (ByteOffset && ByteOffset->QuadPart >= 0) {
        memset(&ov, 0, sizeof(ov));
        ov.Offset = ByteOffset->LowPart;
        ov.OffsetHigh = ByteOffset->HighPart;
        result = ReadFile(FileHandle, Buffer, Length, &bytes_read, &ov);
    } else {
        result = ReadFile(FileHandle, Buffer, Length, &bytes_read, NULL);
    }
    if (t0)
        xbox_ft_io(xbox_ft_ms(t0));

    if (result || GetLastError() == ERROR_HANDLE_EOF) {
        IoStatusBlock->Information = bytes_read;
        if (bytes_read == 0 && Length > 0) {
            IoStatusBlock->Status = STATUS_END_OF_FILE;
            return STATUS_END_OF_FILE;
        }
        IoStatusBlock->Status = STATUS_SUCCESS;
        if (Event) SetEvent(Event);
        return STATUS_SUCCESS;
    }

    XBOX_TRACE(XBOX_LOG_FILE, "NtReadFile(handle=%p, len=%u) failed err=%u",
               FileHandle, Length, GetLastError());
    IoStatusBlock->Status = STATUS_UNSUCCESSFUL;
    IoStatusBlock->Information = 0;
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS __stdcall xbox_NtWriteFile(
    HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
    PXBOX_IO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length,
    PLARGE_INTEGER ByteOffset)
{
    DWORD bytes_written = 0;
    BOOL result;
    OVERLAPPED ov;
    (void)ApcRoutine; (void)ApcContext;

    if (!IoStatusBlock)
        return STATUS_INVALID_PARAMETER;

    if (ByteOffset && ByteOffset->QuadPart >= 0) {
        memset(&ov, 0, sizeof(ov));
        ov.Offset = ByteOffset->LowPart;
        ov.OffsetHigh = ByteOffset->HighPart;
        result = WriteFile(FileHandle, Buffer, Length, &bytes_written, &ov);
    } else {
        result = WriteFile(FileHandle, Buffer, Length, &bytes_written, NULL);
    }

    if (result) {
        IoStatusBlock->Status = STATUS_SUCCESS;
        IoStatusBlock->Information = bytes_written;
        if (Event) SetEvent(Event);
        return STATUS_SUCCESS;
    }

    XBOX_TRACE(XBOX_LOG_FILE, "NtWriteFile(handle=%p, len=%u) failed err=%u",
               FileHandle, Length, GetLastError());
    IoStatusBlock->Status = STATUS_UNSUCCESSFUL;
    IoStatusBlock->Information = 0;
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS __stdcall xbox_NtClose(HANDLE Handle)
{
    XBOX_TRACE(XBOX_LOG_FILE, "NtClose(handle=%p)", Handle);
    if (Handle && Handle != INVALID_HANDLE_VALUE) {
        xbox_file_close_host(Handle);
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_HANDLE;
}

NTSTATUS __stdcall xbox_NtDeleteFile(PXBOX_OBJECT_ATTRIBUTES ObjectAttributes)
{
    WCHAR win_path[XBOX_HOST_PATH];
    if (!translate_obj_path(ObjectAttributes, win_path, XBOX_HOST_PATH))
        return STATUS_OBJECT_PATH_NOT_FOUND;
    XBOX_TRACE(XBOX_LOG_FILE, "NtDeleteFile: %S", win_path);
    if (DeleteFileW(win_path))    return STATUS_SUCCESS;
    if (RemoveDirectoryW(win_path)) return STATUS_SUCCESS;
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS __stdcall xbox_NtQueryInformationFile(
    HANDLE FileHandle, PXBOX_IO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation, ULONG Length, XBOX_FILE_INFORMATION_CLASS FileInformationClass)
{
    (void)Length;
    if (!IoStatusBlock || !FileInformation)
        return STATUS_INVALID_PARAMETER;

    switch (FileInformationClass) {
        case XboxFileBasicInformation: {
            PXBOX_FILE_BASIC_INFORMATION info = (PXBOX_FILE_BASIC_INFORMATION)FileInformation;
            BY_HANDLE_FILE_INFORMATION fi;
            if (!GetFileInformationByHandle(FileHandle, &fi))
                return STATUS_UNSUCCESSFUL;
            info->CreationTime.LowPart    = fi.ftCreationTime.dwLowDateTime;
            info->CreationTime.HighPart   = fi.ftCreationTime.dwHighDateTime;
            info->LastAccessTime.LowPart  = fi.ftLastAccessTime.dwLowDateTime;
            info->LastAccessTime.HighPart = fi.ftLastAccessTime.dwHighDateTime;
            info->LastWriteTime.LowPart   = fi.ftLastWriteTime.dwLowDateTime;
            info->LastWriteTime.HighPart  = fi.ftLastWriteTime.dwHighDateTime;
            info->ChangeTime = info->LastWriteTime;
            info->FileAttributes = fi.dwFileAttributes;
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(XBOX_FILE_BASIC_INFORMATION);
            return STATUS_SUCCESS;
        }
        case XboxFileStandardInformation: {
            PXBOX_FILE_STANDARD_INFORMATION info = (PXBOX_FILE_STANDARD_INFORMATION)FileInformation;
            BY_HANDLE_FILE_INFORMATION fi;
            if (!GetFileInformationByHandle(FileHandle, &fi))
                return STATUS_UNSUCCESSFUL;
            info->AllocationSize.QuadPart = ((LONGLONG)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
            info->AllocationSize.QuadPart = (info->AllocationSize.QuadPart + 4095) & ~4095LL;
            info->EndOfFile.QuadPart = ((LONGLONG)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
            info->NumberOfLinks = fi.nNumberOfLinks;
            info->DeletePending = FALSE;
            info->Directory = (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE;
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(XBOX_FILE_STANDARD_INFORMATION);
            return STATUS_SUCCESS;
        }
        case XboxFilePositionInformation: {
            PXBOX_FILE_POSITION_INFORMATION info = (PXBOX_FILE_POSITION_INFORMATION)FileInformation;
            LARGE_INTEGER pos, zero;
            zero.QuadPart = 0;
            if (!SetFilePointerEx(FileHandle, zero, &pos, FILE_CURRENT))
                return STATUS_UNSUCCESSFUL;
            info->CurrentByteOffset = pos;
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(XBOX_FILE_POSITION_INFORMATION);
            return STATUS_SUCCESS;
        }
        case XboxFileNetworkOpenInformation: {
            PXBOX_FILE_NETWORK_OPEN_INFORMATION info = (PXBOX_FILE_NETWORK_OPEN_INFORMATION)FileInformation;
            BY_HANDLE_FILE_INFORMATION fi;
            if (!GetFileInformationByHandle(FileHandle, &fi))
                return STATUS_UNSUCCESSFUL;
            info->CreationTime.LowPart    = fi.ftCreationTime.dwLowDateTime;
            info->CreationTime.HighPart   = fi.ftCreationTime.dwHighDateTime;
            info->LastAccessTime.LowPart  = fi.ftLastAccessTime.dwLowDateTime;
            info->LastAccessTime.HighPart = fi.ftLastAccessTime.dwHighDateTime;
            info->LastWriteTime.LowPart   = fi.ftLastWriteTime.dwLowDateTime;
            info->LastWriteTime.HighPart  = fi.ftLastWriteTime.dwHighDateTime;
            info->ChangeTime = info->LastWriteTime;
            info->EndOfFile.QuadPart = ((LONGLONG)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
            info->AllocationSize.QuadPart = (info->EndOfFile.QuadPart + 4095) & ~4095LL;
            info->FileAttributes = fi.dwFileAttributes;
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(XBOX_FILE_NETWORK_OPEN_INFORMATION);
            return STATUS_SUCCESS;
        }
        default:
            xbox_log(XBOX_LOG_WARN, XBOX_LOG_FILE,
                "NtQueryInformationFile: unhandled class %d", FileInformationClass);
            return STATUS_NOT_IMPLEMENTED;
    }
}

NTSTATUS __stdcall xbox_NtSetInformationFile(
    HANDLE FileHandle, PXBOX_IO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation, ULONG Length, XBOX_FILE_INFORMATION_CLASS FileInformationClass)
{
    (void)Length;
    if (!IoStatusBlock || !FileInformation)
        return STATUS_INVALID_PARAMETER;

    switch (FileInformationClass) {
        case XboxFilePositionInformation: {
            PXBOX_FILE_POSITION_INFORMATION info = (PXBOX_FILE_POSITION_INFORMATION)FileInformation;
            if (!SetFilePointerEx(FileHandle, info->CurrentByteOffset, NULL, FILE_BEGIN)) {
                xbox_log(XBOX_LOG_WARN, XBOX_LOG_FILE,
                    "SetFilePosition failed: offset=%lld err=%u",
                    (long long)info->CurrentByteOffset.QuadPart, GetLastError());
                return STATUS_UNSUCCESSFUL;
            }
            IoStatusBlock->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
        case XboxFileEndOfFileInformation: {
            PXBOX_FILE_END_OF_FILE_INFORMATION info = (PXBOX_FILE_END_OF_FILE_INFORMATION)FileInformation;
            LARGE_INTEGER cur, zero = {0};
            SetFilePointerEx(FileHandle, zero, &cur, FILE_CURRENT);
            SetFilePointerEx(FileHandle, info->EndOfFile, NULL, FILE_BEGIN);
            if (!SetEndOfFile(FileHandle)) {
                xbox_log(XBOX_LOG_WARN, XBOX_LOG_FILE,
                    "SetEndOfFile failed: size=%lld err=%u",
                    (long long)info->EndOfFile.QuadPart, GetLastError());
                SetFilePointerEx(FileHandle, cur, NULL, FILE_BEGIN);
                return STATUS_UNSUCCESSFUL;
            }
            if (cur.QuadPart <= info->EndOfFile.QuadPart)
                SetFilePointerEx(FileHandle, cur, NULL, FILE_BEGIN);
            IoStatusBlock->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
        case XboxFileAllocationInformation: {
            /* Reserve space for a file. Halo's save path calls this before
             * writing, and an unimplemented class here returned
             * STATUS_NOT_IMPLEMENTED, which the title turned into DOS error
             * 317 and reported as "couldn't open or create saved game file".
             *
             * Same payload shape as EndOfFile: one LARGE_INTEGER. Windows
             * FileAllocationInfo is the direct equivalent; if the filesystem
             * declines it, fall back to setting the size, since the caller
             * only needs the space to exist. */
            PXBOX_FILE_END_OF_FILE_INFORMATION info =
                (PXBOX_FILE_END_OF_FILE_INFORMATION)FileInformation;
            FILE_ALLOCATION_INFO fai;
            fai.AllocationSize = info->EndOfFile;
            if (!SetFileInformationByHandle(FileHandle, FileAllocationInfo,
                                            &fai, sizeof(fai))) {
                LARGE_INTEGER cur, zero = {0};
                SetFilePointerEx(FileHandle, zero, &cur, FILE_CURRENT);
                SetFilePointerEx(FileHandle, info->EndOfFile, NULL, FILE_BEGIN);
                SetEndOfFile(FileHandle);
                SetFilePointerEx(FileHandle, cur, NULL, FILE_BEGIN);
            }
            IoStatusBlock->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
        case XboxFileDispositionInformation: {
            PXBOX_FILE_DISPOSITION_INFORMATION info = (PXBOX_FILE_DISPOSITION_INFORMATION)FileInformation;
            FILE_DISPOSITION_INFO fdi;
            fdi.DeleteFile = info->DeleteFile;
            shadow_mark_delete(FileHandle, info->DeleteFile != 0);
            if (!SetFileInformationByHandle(FileHandle, FileDispositionInfo, &fdi, sizeof(fdi))) {
                /* stderr: a delete the title believes done (its save folder
                 * still there) surfaces far away as "memory unit removed"
                 * (v0.9.x under Wine: ERROR_DIR_NOT_EMPTY, see DIR_BY_HANDLE). */
                WCHAR p[XBOX_HOST_PATH];
                DWORD err = GetLastError();
                if (!GetFinalPathNameByHandleW(FileHandle, p, XBOX_HOST_PATH, VOLUME_NAME_DOS))
                    wcscpy_s(p, XBOX_HOST_PATH, L"?");
                fprintf(stderr, "  [FILE] delete-on-close refused by the host: %S (err=%lu)\n",
                        p, (unsigned long)err);
            }
            IoStatusBlock->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
        case XboxFileBasicInformation: {
            PXBOX_FILE_BASIC_INFORMATION info = (PXBOX_FILE_BASIC_INFORMATION)FileInformation;
            FILETIME ct, at, wt;
            ct.dwLowDateTime = info->CreationTime.LowPart;
            ct.dwHighDateTime = info->CreationTime.HighPart;
            at.dwLowDateTime = info->LastAccessTime.LowPart;
            at.dwHighDateTime = info->LastAccessTime.HighPart;
            wt.dwLowDateTime = info->LastWriteTime.LowPart;
            wt.dwHighDateTime = info->LastWriteTime.HighPart;
            SetFileTime(FileHandle, &ct, &at, &wt);
            IoStatusBlock->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
        default:
            /* stderr, not xbox_log: WARN is filtered out by default, and an
             * unimplemented info class is exactly the kind of silent gap that
             * surfaces far away. Halo's save path hits one, gets
             * STATUS_NOT_IMPLEMENTED, converts it to DOS error 317 and asserts
             * "couldn't open or create saved game file". */
            fprintf(stderr, "  [FILE] NtSetInformationFile: unhandled class %d\n",
                    (int)FileInformationClass);
            fflush(stderr);
            return STATUS_NOT_IMPLEMENTED;
    }
}

NTSTATUS __stdcall xbox_NtQueryVolumeInformationFile(
    HANDLE FileHandle, PXBOX_IO_STATUS_BLOCK IoStatusBlock,
    PVOID FsInformation, ULONG Length, XBOX_FS_INFORMATION_CLASS FsInformationClass)
{
    (void)FileHandle; (void)Length;
    if (!IoStatusBlock || !FsInformation)
        return STATUS_INVALID_PARAMETER;

    switch (FsInformationClass) {
        case XboxFileFsSizeInformation: {
            PXBOX_FILE_FS_SIZE_INFORMATION info = (PXBOX_FILE_FS_SIZE_INFORMATION)FsInformation;
            ULARGE_INTEGER free_bytes, total_bytes, total_free;
            if (GetDiskFreeSpaceExW(NULL, &free_bytes, &total_bytes, &total_free)) {
                info->BytesPerSector = XBOX_BYTES_PER_SECTOR;
                info->SectorsPerAllocationUnit = XBOX_SECTORS_PER_CLUSTER;
                ULONGLONG cs = (ULONGLONG)info->BytesPerSector * info->SectorsPerAllocationUnit;
                info->TotalAllocationUnits.QuadPart = total_bytes.QuadPart / cs;
                info->AvailableAllocationUnits.QuadPart = free_bytes.QuadPart / cs;
            } else {
                info->BytesPerSector = XBOX_BYTES_PER_SECTOR;
                info->SectorsPerAllocationUnit = XBOX_SECTORS_PER_CLUSTER;
                info->TotalAllocationUnits.QuadPart = 1048576;
                info->AvailableAllocationUnits.QuadPart = 524288;
            }
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(XBOX_FILE_FS_SIZE_INFORMATION);
            return STATUS_SUCCESS;
        }
        default:
            xbox_log(XBOX_LOG_WARN, XBOX_LOG_FILE,
                "NtQueryVolumeInformationFile: unhandled class %d", FsInformationClass);
            return STATUS_NOT_IMPLEMENTED;
    }
}

NTSTATUS __stdcall xbox_NtFlushBuffersFile(HANDLE FileHandle, PXBOX_IO_STATUS_BLOCK IoStatusBlock)
{
    FlushFileBuffers(FileHandle);
    if (IoStatusBlock) {
        IoStatusBlock->Status = STATUS_SUCCESS;
        IoStatusBlock->Information = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtQueryFullAttributesFile(
    PXBOX_OBJECT_ATTRIBUTES ObjectAttributes,
    PXBOX_FILE_NETWORK_OPEN_INFORMATION FileInformation)
{
    WCHAR win_path[XBOX_HOST_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fad;

    if (!FileInformation)
        return STATUS_INVALID_PARAMETER;
    if (!translate_obj_path(ObjectAttributes, win_path, XBOX_HOST_PATH))
        return STATUS_OBJECT_PATH_NOT_FOUND;

    if (!GetFileAttributesExW(win_path, GetFileExInfoStandard, &fad)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            return STATUS_OBJECT_NAME_NOT_FOUND;
        return STATUS_UNSUCCESSFUL;
    }

    FileInformation->CreationTime.LowPart    = fad.ftCreationTime.dwLowDateTime;
    FileInformation->CreationTime.HighPart   = fad.ftCreationTime.dwHighDateTime;
    FileInformation->LastAccessTime.LowPart  = fad.ftLastAccessTime.dwLowDateTime;
    FileInformation->LastAccessTime.HighPart = fad.ftLastAccessTime.dwHighDateTime;
    FileInformation->LastWriteTime.LowPart   = fad.ftLastWriteTime.dwLowDateTime;
    FileInformation->LastWriteTime.HighPart  = fad.ftLastWriteTime.dwHighDateTime;
    FileInformation->ChangeTime = FileInformation->LastWriteTime;
    FileInformation->EndOfFile.QuadPart = ((LONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    FileInformation->AllocationSize.QuadPart = (FileInformation->EndOfFile.QuadPart + 4095) & ~4095LL;
    FileInformation->FileAttributes = fad.dwFileAttributes;
    return STATUS_SUCCESS;
}

#define MAX_DIR_CONTEXTS 64
typedef struct {
    HANDLE file_handle;
    HANDLE find_handle;          /* FindFirstFileW's, or DIR_BY_HANDLE */
    BOOL   first_done;
    WIN32_FIND_DATAW find_data;
    WIN32_FIND_DATAW *list;      /* DIR_BY_HANDLE: the whole listing */
    int    list_n, list_i;
} DIR_CONTEXT;

/* FindFirstFileW opens the folder a second time, by path. Wine refuses that
 * with ERROR_SHARING_VIOLATION while the title holds the folder open with a
 * share mode NTFS lets a listing through (THPS 2X deleting a save: the
 * listing came back empty, nothing was deleted, and every later save said
 * "the memory unit was removed during use" -- v0.9.x on Steam Deck / Proton,
 * reproduced under Wine 9.0). Then the listing is read through the title's
 * own handle instead. */
#define DIR_BY_HANDLE ((HANDLE)(intptr_t)-2)

static int dir_wild_match(const WCHAR *pat, const WCHAR *s)
{
    if (!*pat) return !*s;
    if (*pat == L'*') return dir_wild_match(pat + 1, s) || (*s && dir_wild_match(pat, s + 1));
    if (!*s) return 0;
    if (*pat != L'?' && towlower(*pat) != towlower(*s)) return 0;
    return dir_wild_match(pat + 1, s + 1);
}

/* The listing of the folder open at h, entries matching pat. 0 = nothing read. */
static int dir_list_by_handle(DIR_CONTEXT *ctx, HANDLE h, const WCHAR *pat)
{
    enum { BUF = 64 * 1024 };
    BYTE *buf = malloc(BUF);            /* per call: titles list from several threads */
    FILE_INFO_BY_HANDLE_CLASS cls = FileFullDirectoryRestartInfo;
    int cap = 0;

    ctx->list_n = ctx->list_i = 0;
    while (buf && GetFileInformationByHandleEx(h, cls, buf, BUF)) {
        FILE_FULL_DIR_INFO *e = (FILE_FULL_DIR_INFO *)buf;
        cls = FileFullDirectoryInfo;
        for (;;) {
            WIN32_FIND_DATAW fd;
            size_t n = e->FileNameLength / sizeof(WCHAR);
            memset(&fd, 0, sizeof fd);
            if (n >= MAX_PATH) n = MAX_PATH - 1;
            memcpy(fd.cFileName, e->FileName, n * sizeof(WCHAR));
            if (dir_wild_match(pat, fd.cFileName)) {
                if (ctx->list_n == cap) {
                    WIN32_FIND_DATAW *nl = realloc(ctx->list, (size_t)(cap ? cap * 2 : 16) * sizeof *nl);
                    if (!nl) break;
                    ctx->list = nl;
                    cap = cap ? cap * 2 : 16;
                }
                fd.dwFileAttributes = e->FileAttributes;
                fd.ftCreationTime.dwLowDateTime    = (DWORD)e->CreationTime.QuadPart;
                fd.ftCreationTime.dwHighDateTime   = (DWORD)(e->CreationTime.QuadPart >> 32);
                fd.ftLastAccessTime.dwLowDateTime  = (DWORD)e->LastAccessTime.QuadPart;
                fd.ftLastAccessTime.dwHighDateTime = (DWORD)(e->LastAccessTime.QuadPart >> 32);
                fd.ftLastWriteTime.dwLowDateTime   = (DWORD)e->LastWriteTime.QuadPart;
                fd.ftLastWriteTime.dwHighDateTime  = (DWORD)(e->LastWriteTime.QuadPart >> 32);
                fd.nFileSizeLow  = (DWORD)e->EndOfFile.QuadPart;
                fd.nFileSizeHigh = (DWORD)(e->EndOfFile.QuadPart >> 32);
                ctx->list[ctx->list_n++] = fd;
            }
            if (!e->NextEntryOffset) break;
            e = (FILE_FULL_DIR_INFO *)((BYTE *)e + e->NextEntryOffset);
        }
    }
    free(buf);
    return ctx->list_n;
}

/* FindNextFileW for either kind of search. */
static BOOL dir_next(DIR_CONTEXT *ctx)
{
    if (ctx->find_handle != DIR_BY_HANDLE)
        return FindNextFileW(ctx->find_handle, &ctx->find_data);
    if (ctx->list_i >= ctx->list_n)
        return FALSE;
    ctx->find_data = ctx->list[ctx->list_i++];
    return TRUE;
}

/* FindClose for either kind of search. */
static void dir_close(DIR_CONTEXT *ctx)
{
    if (ctx->find_handle == DIR_BY_HANDLE) {
        free(ctx->list);
        ctx->list = NULL;
        ctx->list_n = ctx->list_i = 0;
    } else if (ctx->find_handle && ctx->find_handle != INVALID_HANDLE_VALUE) {
        FindClose(ctx->find_handle);
    }
    ctx->find_handle = NULL;
}

static DIR_CONTEXT s_dir_contexts[MAX_DIR_CONTEXTS];
static CRITICAL_SECTION s_dir_cs;
static BOOL s_dir_cs_init = FALSE;

static DIR_CONTEXT* find_or_create_dir_context(HANDLE FileHandle, BOOL create)
{
    if (!s_dir_cs_init) { InitializeCriticalSection(&s_dir_cs); s_dir_cs_init = TRUE; }
    EnterCriticalSection(&s_dir_cs);
    for (int i = 0; i < MAX_DIR_CONTEXTS; i++) {
        if (s_dir_contexts[i].file_handle == FileHandle && s_dir_contexts[i].find_handle != NULL) {
            LeaveCriticalSection(&s_dir_cs);
            return &s_dir_contexts[i];
        }
    }
    if (!create) { LeaveCriticalSection(&s_dir_cs); return NULL; }
    for (int i = 0; i < MAX_DIR_CONTEXTS; i++) {
        if (s_dir_contexts[i].find_handle == NULL) {
            s_dir_contexts[i].file_handle = FileHandle;
            s_dir_contexts[i].first_done = FALSE;
            LeaveCriticalSection(&s_dir_cs);
            return &s_dir_contexts[i];
        }
    }
    LeaveCriticalSection(&s_dir_cs);
    return NULL;
}

/* A closed directory handle takes its enumeration with it. Host handle values
 * are reused, and a search abandoned part-way (a title that stops at the first
 * match) otherwise carried on under the next directory opened at the same
 * value: its first answer was the old search's next entry. */
void xbox_dir_context_drop(HANDLE FileHandle)
{
    if (!s_dir_cs_init)
        return;
    EnterCriticalSection(&s_dir_cs);
    for (int i = 0; i < MAX_DIR_CONTEXTS; i++) {
        if (s_dir_contexts[i].file_handle == FileHandle) {
            dir_close(&s_dir_contexts[i]);
            s_dir_contexts[i].file_handle = NULL;
            s_dir_contexts[i].first_done = FALSE;
        }
    }
    LeaveCriticalSection(&s_dir_cs);
}

NTSTATUS __stdcall xbox_NtQueryDirectoryFile(
    HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
    PXBOX_IO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length,
    XBOX_FILE_INFORMATION_CLASS FileInformationClass,
    PXBOX_ANSI_STRING FileName, BOOLEAN RestartScan)
{
    DIR_CONTEXT* ctx;
    PXBOX_FILE_DIRECTORY_INFORMATION entry;
    (void)Event; (void)ApcRoutine; (void)ApcContext;

    if (!IoStatusBlock || !FileInformation)
        return STATUS_INVALID_PARAMETER;
    IoStatusBlock->Information = 0;
    if (FileInformationClass != XboxFileDirectoryInformation) {
        IoStatusBlock->Status = STATUS_INVALID_INFO_CLASS;
        return STATUS_INVALID_INFO_CLASS;
    }

    ctx = find_or_create_dir_context(FileHandle, TRUE);
    if (!ctx)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (RestartScan || !ctx->first_done) {
        dir_close(ctx);
        WCHAR search_path[XBOX_HOST_PATH];
        WCHAR dir_path[XBOX_HOST_PATH];
        DWORD path_len = GetFinalPathNameByHandleW(FileHandle, dir_path, XBOX_HOST_PATH,
                                                   FILE_NAME_NORMALIZED);
        if (path_len == 0 || path_len >= XBOX_HOST_PATH) {
            IoStatusBlock->Status = STATUS_UNSUCCESSFUL;
            return STATUS_UNSUCCESSFUL;
        }
        WCHAR* clean_path = dir_path;
        /* Keep the \\?\ form for a long path: without it FindFirstFileW
         * is limited to MAX_PATH. */
        if (wcsncmp(clean_path, L"\\\\?\\", 4) == 0 && wcslen(clean_path) < MAX_PATH - 12)
            clean_path += 4;
        /* An empty mask means every entry, as no mask does (NT). Building "<dir>\" from it made FindFirstFileW
         * fail, so the first query said STATUS_NO_MORE_FILES: Panzer Dragoon Orta's FindFirstFile("d:\*.*") of the
         * disc root came back empty, its Z: mirror pass stopped there, and no Z: folder was ever created
         * (2026-11-01). */
        if (FileName && FileName->Buffer && FileName->Length) {
            WCHAR pattern_wide[XBOX_HOST_PATH];
            MultiByteToWideChar(1252, 0, FileName->Buffer, FileName->Length,
                                pattern_wide, XBOX_HOST_PATH);
            pattern_wide[FileName->Length] = L'\0';
            swprintf_s(search_path, XBOX_HOST_PATH, L"%s\\%s", clean_path, pattern_wide);
        } else {
            swprintf_s(search_path, XBOX_HOST_PATH, L"%s\\*", clean_path);
        }
        ctx->find_handle = FindFirstFileW(search_path, &ctx->find_data);
        if (ctx->find_handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION) {
            const WCHAR *pat = wcsrchr(search_path, L'\\');
            ctx->find_handle = DIR_BY_HANDLE;
            if (!dir_list_by_handle(ctx, FileHandle, pat ? pat + 1 : L"*") || !dir_next(ctx)) {
                dir_close(ctx);
                ctx->find_handle = INVALID_HANDLE_VALUE;
            }
        }
        if (ctx->find_handle == INVALID_HANDLE_VALUE) {
            ctx->find_handle = NULL;
            IoStatusBlock->Status = STATUS_NO_MORE_FILES;
            return STATUS_NO_MORE_FILES;
        }
        ctx->first_done = TRUE;
    } else {
        if (!dir_next(ctx)) {
            dir_close(ctx);
            ctx->file_handle = NULL;
            IoStatusBlock->Status = STATUS_NO_MORE_FILES;
            return STATUS_NO_MORE_FILES;
        }
    }

    /* FATX enumeration never exposes the host's dot directories. */
    while (!wcscmp(ctx->find_data.cFileName, L".") ||
           !wcscmp(ctx->find_data.cFileName, L"..")) {
        if (!dir_next(ctx)) {
            dir_close(ctx);
            ctx->file_handle = NULL;
            IoStatusBlock->Status = STATUS_NO_MORE_FILES;
            return STATUS_NO_MORE_FILES;
        }
    }

    entry = (PXBOX_FILE_DIRECTORY_INFORMATION)FileInformation;
    memset(entry, 0, Length);
    char filename_ansi[XBOX_HOST_PATH];
    int name_len = WideCharToMultiByte(1252, 0, ctx->find_data.cFileName, -1,
                                       filename_ansi, XBOX_HOST_PATH, NULL, NULL);
    if (name_len > 0) name_len--;

    entry->NextEntryOffset = 0;
    entry->FileIndex = 0;
    entry->CreationTime.LowPart   = ctx->find_data.ftCreationTime.dwLowDateTime;
    entry->CreationTime.HighPart  = ctx->find_data.ftCreationTime.dwHighDateTime;
    entry->LastAccessTime.LowPart = ctx->find_data.ftLastAccessTime.dwLowDateTime;
    entry->LastAccessTime.HighPart = ctx->find_data.ftLastAccessTime.dwHighDateTime;
    entry->LastWriteTime.LowPart  = ctx->find_data.ftLastWriteTime.dwLowDateTime;
    entry->LastWriteTime.HighPart = ctx->find_data.ftLastWriteTime.dwHighDateTime;
    entry->ChangeTime = entry->LastWriteTime;
    entry->EndOfFile.QuadPart = ((LONGLONG)ctx->find_data.nFileSizeHigh << 32) | ctx->find_data.nFileSizeLow;
    entry->AllocationSize.QuadPart = (entry->EndOfFile.QuadPart + 4095) & ~4095LL;
    entry->FileAttributes = ctx->find_data.dwFileAttributes;
    entry->FileNameLength = name_len;
    {
        ULONG header_size = (ULONG)((ULONG_PTR)&((PXBOX_FILE_DIRECTORY_INFORMATION)0)->FileName);
        if (name_len > 0 && (header_size + name_len) <= Length)
            memcpy(entry->FileName, filename_ansi, name_len);
        IoStatusBlock->Status = STATUS_SUCCESS;
        IoStatusBlock->Information = header_size + name_len;
    }
    return STATUS_SUCCESS;
}

/* ======================================================================== */
#else /* !_WIN32 */

/* ── Atomic save writes, POSIX (the same contract as the Win32 side above) ──
 *
 * A save opened for writing gets a copy in save_dir/.pending/ as its handle; closing it fsyncs the copy, renames
 * it over the save (rename(2) is atomic on one filesystem, and .pending lies inside save_dir) and fsyncs the
 * folder, so the new name survives a power loss too. A run killed mid-save leaves the old save as it was; the
 * copy is dropped at the next start (kernel_path.c pending_cleanup). Contents the title keeps (FILE_OPEN,
 * FILE_OPEN_IF) are copied first. Only under save_dir, only for write access, never directories or
 * delete-on-close opens (real saves on Linux, 2026-11-11). */
#include <pthread.h>

typedef struct {
    HANDLE h;
    int    delete_final;
    int    delete_old;                  /* renamed while open: old_final goes when the copy is committed */
    char   tmp[XBOX_HOST_PATH];
    char   final_path[XBOX_HOST_PATH];
    char   old_final[XBOX_HOST_PATH];
} SaveShadow;

#define SAVE_SHADOW_MAX 64
static SaveShadow      s_shadow[SAVE_SHADOW_MAX];
static pthread_mutex_t s_shadow_lock = PTHREAD_MUTEX_INITIALIZER;
static long            s_shadow_seq;

static SaveShadow *shadow_find(HANDLE h)
{
    int i;
    for (i = 0; i < SAVE_SHADOW_MAX; i++)
        if (h && s_shadow[i].h == h)
            return &s_shadow[i];
    return NULL;
}

static int copy_file_posix(const char *from, const char *to)
{
    char buf[65536];
    int in = open(from, O_RDONLY), out;
    ssize_t n;
    if (in < 0)
        return 0;
    out = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        close(in);
        return 0;
    }
    while ((n = read(in, buf, sizeof buf)) > 0) {
        char *p = buf;
        while (n > 0) {
            ssize_t w = write(out, p, (size_t)n);
            if (w <= 0) {
                close(in);
                close(out);
                unlink(to);
                return 0;
            }
            p += w;
            n -= w;
        }
    }
    close(in);
    if (n < 0 || close(out) != 0) {
        unlink(to);
        return 0;
    }
    return 1;
}

/* the folder holding p, fsynced: a rename is durable only once its directory is */
static void fsync_parent(const char *p)
{
    char dir[XBOX_HOST_PATH];
    char *slash;
    int fd;
    snprintf(dir, sizeof dir, "%s", p);
    slash = strrchr(dir, '/');
    if (!slash)
        return;
    *slash = 0;
    if ((fd = open(dir[0] ? dir : "/", O_RDONLY | O_DIRECTORY)) >= 0) {
        fsync(fd);
        close(fd);
    }
}

/* Open host_path for writing through a pending copy: the fd, -1 with errno (the caller maps it), or -2 when the
 * open should go the normal way. *sh_out is the claimed slot (its handle filled in by the caller). */
static int shadow_open(const char *host_path, int flags, ULONG disposition, SaveShadow **sh_out)
{
    struct stat st;
    int exists = stat(host_path, &st) == 0;
    int keep = disposition == XBOX_FILE_OPEN || disposition == XBOX_FILE_OPEN_IF;
    const char *name = strrchr(host_path, '/');
    const char *pending = xbox_path_pending_dir();
    SaveShadow *sh = NULL;
    int fd, i;

    *sh_out = NULL;
    if (exists && S_ISDIR(st.st_mode))
        return -2;
    if (!exists && (disposition == XBOX_FILE_OPEN || disposition == XBOX_FILE_OVERWRITE))
        return -2;                          /* the normal open reports "not found" */
    if (exists && disposition == XBOX_FILE_CREATE) {
        errno = EEXIST;
        return -1;
    }
    if (!pending || !pending[0])
        return -2;
    mkdir(pending, 0755);                   /* made at path init; a title may have removed save_dir since */
    pthread_mutex_lock(&s_shadow_lock);
    for (i = 0; i < SAVE_SHADOW_MAX && !sh; i++)
        if (!s_shadow[i].h && !s_shadow[i].tmp[0])
            sh = &s_shadow[i];
    if (!sh) {
        pthread_mutex_unlock(&s_shadow_lock);
        return -2;                          /* table full: write in place */
    }
    snprintf(sh->tmp, sizeof sh->tmp, "%s/%ld_%s", pending, ++s_shadow_seq, name ? name + 1 : "save");
    snprintf(sh->final_path, sizeof sh->final_path, "%s", host_path);
    sh->delete_final = 0;
    sh->delete_old = 0;
    sh->old_final[0] = 0;
    pthread_mutex_unlock(&s_shadow_lock);   /* the slot is ours (tmp set); copy without holding the lock */

    if (keep && exists && !copy_file_posix(host_path, sh->tmp)) {
        sh->tmp[0] = 0;
        return -2;
    }
    flags &= ~(O_EXCL | O_TRUNC | O_ACCMODE);
    flags |= O_RDWR | O_CREAT | (keep && exists ? 0 : O_TRUNC);
    fd = open(sh->tmp, flags, 0644);
    if (fd < 0) {
        unlink(sh->tmp);
        sh->tmp[0] = 0;
        return -2;
    }
    *sh_out = sh;
    return fd;
}

/* A delete disposition set on a pending copy deletes the save on close. */
static int shadow_mark_delete(HANDLE h, int del)
{
    SaveShadow *sh;
    pthread_mutex_lock(&s_shadow_lock);
    sh = shadow_find(h);
    if (sh)
        sh->delete_final = del;
    pthread_mutex_unlock(&s_shadow_lock);
    return sh != NULL;
}

/* RECOMP_SAVE_CRASH_TEST=<text>, as on Windows: end the process the moment a save write whose path contains
 * <text> (case-insensitive) is about to be committed - a deterministic "killed mid-save" for storage tests. */
static void crash_test_point(const char *final_path)
{
    /* read at every commit (a save commit is no hot path): a forked test child sets it after its parent
     * already committed once */
    const char *needle = getenv("RECOMP_SAVE_CRASH_TEST");
    if (needle && *needle && strcasestr(final_path, needle)) {
        fprintf(stderr, "  [STORAGE] RECOMP_SAVE_CRASH_TEST: terminating before %s is committed\n", final_path);
        xbox_log_drain();
        _exit(3);                           /* no atexit, no flush: as a kill */
    }
}

void xbox_file_close_host(void *host_handle)
{
    HANDLE h = (HANDLE)host_handle;
    SaveShadow done;
    SaveShadow *sh;
    int fd;

    if (!h)
        return;
    xbox_dir_context_drop(h);               /* before the value can be reused (see below) */
    pthread_mutex_lock(&s_shadow_lock);
    sh = shadow_find(h);
    if (sh) {
        done = *sh;
        sh->h = NULL;                       /* tmp stays set until the commit is through: the slot is not free */
    }
    pthread_mutex_unlock(&s_shadow_lock);
    if (sh && (fd = w32_handle_fd(h)) >= 0)
        fsync(fd);
    CloseHandle(h);
    if (!sh)
        return;
    if (!done.delete_final)
        crash_test_point(done.final_path);
    if (done.delete_final) {
        unlink(done.tmp);
        unlink(done.final_path);
        fsync_parent(done.final_path);
    } else if (rename(done.tmp, done.final_path) != 0) {
        fprintf(stderr, "  [STORAGE] save write not committed: %s -> %s (errno %d); the old save is kept\n",
                done.tmp, done.final_path, errno);
        unlink(done.tmp);
    } else {
        fsync_parent(done.final_path);
        if (done.delete_old) {
            unlink(done.old_final);         /* renamed while open: the old name is gone now */
            fsync_parent(done.old_final);
        }
    }
    pthread_mutex_lock(&s_shadow_lock);
    sh->tmp[0] = 0;                         /* the slot is free again */
    pthread_mutex_unlock(&s_shadow_lock);
}
/* ====================  POSIX backend  =================================== */
/* ======================================================================== */

/* Convert Xbox access mask + disposition to POSIX open() flags. */
static int posix_open_flags(ACCESS_MASK access, ULONG disposition)
{
    int wantWrite = (access & (XBOX_GENERIC_WRITE | XBOX_GENERIC_ALL |
                               XBOX_FILE_WRITE_DATA | XBOX_FILE_APPEND_DATA)) != 0;
    int rw = wantWrite ? O_RDWR : O_RDONLY;
    int extra;

    switch (disposition) {
        case XBOX_FILE_SUPERSEDE:    extra = O_CREAT | O_TRUNC; break;
        case XBOX_FILE_OPEN:         extra = 0;                 break;
        case XBOX_FILE_CREATE:       extra = O_CREAT | O_EXCL;  break;
        case XBOX_FILE_OPEN_IF:      extra = O_CREAT;           break;
        case XBOX_FILE_OVERWRITE:    extra = O_TRUNC;           break;
        case XBOX_FILE_OVERWRITE_IF: extra = O_CREAT | O_TRUNC; break;
        default:                     extra = 0;                 break;
    }
    /* O_TRUNC / O_CREAT imply write intent */
    if ((extra & (O_TRUNC | O_CREAT)) && rw == O_RDONLY)
        rw = O_RDWR;
    if (access & XBOX_FILE_APPEND_DATA)
        extra |= O_APPEND;
    return rw | extra;
}

static void unix_to_filetime(time_t sec, long nsec, LARGE_INTEGER* out)
{
    /* 100-ns ticks since 1601-01-01 */
    ULONGLONG t = 116444736000000000ULL
                + (ULONGLONG)sec * 10000000ULL
                + (ULONGLONG)nsec / 100ULL;
    out->LowPart  = (DWORD)(t & 0xFFFFFFFFULL);
    out->HighPart = (LONG)(t >> 32);
}

static ULONG mode_to_xbox_attrs(mode_t m)
{
    ULONG a = 0;
    if (S_ISDIR(m))      a |= XBOX_FILE_ATTRIBUTE_DIRECTORY;
    if (!(m & S_IWUSR))  a |= XBOX_FILE_ATTRIBUTE_READONLY;
    if (a == 0)          a = XBOX_FILE_ATTRIBUTE_NORMAL;
    return a;
}

static NTSTATUS errno_to_status(int e)
{
    switch (e) {
        case ENOENT:  return STATUS_OBJECT_NAME_NOT_FOUND;
        case ENOTDIR: return STATUS_OBJECT_PATH_NOT_FOUND;
        case EACCES:
        case EPERM:   return STATUS_ACCESS_DENIED;
        case EEXIST:  return STATUS_OBJECT_NAME_COLLISION;
        case ENOMEM:  return STATUS_NO_MEMORY;
        default:      return STATUS_UNSUCCESSFUL;
    }
}

NTSTATUS __stdcall xbox_NtCreateFile(
    PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
    PXBOX_OBJECT_ATTRIBUTES ObjectAttributes, PXBOX_IO_STATUS_BLOCK IoStatusBlock,
    PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess,
    ULONG CreateDisposition, ULONG CreateOptions)
{
    char host_path[XBOX_HOST_PATH], shadow_tmp[XBOX_HOST_PATH];
    SaveShadow *shadow = NULL;
    (void)AllocationSize; (void)FileAttributes; (void)ShareAccess;

    if (!FileHandle || !ObjectAttributes)
        return STATUS_INVALID_PARAMETER;

    const char* xbox_path = get_xbox_path(ObjectAttributes);
    if (!xbox_path || !xbox_translate_path(xbox_path, host_path, XBOX_HOST_PATH)) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_FILE, "NtCreateFile: path translation failed");
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }

    int fd;
    int existed = access(host_path, F_OK) == 0;
    if (CreateOptions & XBOX_FILE_DIRECTORY_FILE) {
        if (CreateDisposition == XBOX_FILE_CREATE || CreateDisposition == XBOX_FILE_OPEN_IF) {
            if (mkdir(host_path, 0755) != 0 && errno == EEXIST
                    && CreateDisposition == XBOX_FILE_CREATE) {
                if (IoStatusBlock) {
                    IoStatusBlock->Status = STATUS_OBJECT_NAME_COLLISION;
                    IoStatusBlock->Information = 4; /* FILE_EXISTS */
                }
                return STATUS_OBJECT_NAME_COLLISION;
            }
        }
        fd = open(host_path, O_RDONLY | O_DIRECTORY);
    } else {
        int flags = posix_open_flags(DesiredAccess, CreateDisposition);
        fd = -2;
        if ((flags & O_ACCMODE) != O_RDONLY && !(CreateOptions & XBOX_FILE_DELETE_ON_CLOSE)
                && xbox_path_in_save_dir(host_path)) {
            fd = shadow_open(host_path, flags, CreateDisposition, &shadow);
            if (fd >= 0)
                snprintf(shadow_tmp, sizeof shadow_tmp, "%s", shadow->tmp);
        }
        if (fd == -2)
            fd = open(host_path, flags, 0644);
    }

    if (fd < 0) {
        int e = errno;
        NTSTATUS st = errno_to_status(e);
        /* ENOENT covers both a missing file and a missing folder on the way; Windows (and the console) answer the
         * second with STATUS_OBJECT_PATH_NOT_FOUND, and titles branch on the difference (create the folder, or not). */
        if (e == ENOENT) {
            char parent[XBOX_HOST_PATH];
            char *slash;
            struct stat ps;
            snprintf(parent, sizeof parent, "%s", host_path);
            slash = strrchr(parent, '/');
            if (slash && slash != parent) {
                *slash = '\0';
                if (stat(parent, &ps) != 0 || !S_ISDIR(ps.st_mode))
                    st = STATUS_OBJECT_PATH_NOT_FOUND;
            }
        }
        XBOX_TRACE(XBOX_LOG_FILE, "NtCreateFile FAILED: %s (errno=%d)", host_path, e);
        if (IoStatusBlock) {
            IoStatusBlock->Status = st;
            IoStatusBlock->Information = 0;
        }
        return st;
    }

    /* a pending copy is recorded under its own path (what the handle really is); the save's name is in the slot */
    *FileHandle = w32_open_handle(fd, shadow ? shadow_tmp : host_path);
    if (shadow) {
        pthread_mutex_lock(&s_shadow_lock);
        shadow->h = *FileHandle;
        pthread_mutex_unlock(&s_shadow_lock);
    }
    if (IoStatusBlock) {
        IoStatusBlock->Status = STATUS_SUCCESS;
        IoStatusBlock->Information = xbox_create_information(CreateDisposition, existed);
    }
    XBOX_TRACE(XBOX_LOG_FILE, "NtCreateFile: %s -> handle=%p%s", host_path, *FileHandle, shadow ? " (pending copy)" : "");
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtReadFile(
    HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
    PXBOX_IO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length,
    PLARGE_INTEGER ByteOffset)
{
    (void)ApcRoutine; (void)ApcContext;
    if (!IoStatusBlock)
        return STATUS_INVALID_PARAMETER;
    /* The OS writes the buffer: a page armed by page tracking would fail the
     * read (an error, not a fault) -- release it first. */
    xbox_PageTrackHostWrite(Buffer, Length);

    int fd = w32_handle_fd(FileHandle);
    if (fd < 0) {
        IoStatusBlock->Status = STATUS_INVALID_HANDLE;
        return STATUS_INVALID_HANDLE;
    }

    if (ByteOffset && ByteOffset->QuadPart >= 0)
        lseek(fd, (off_t)ByteOffset->QuadPart, SEEK_SET);

    long long t0 = xbox_ft_on() ? xbox_ft_now() : 0;   /* the renderer's [hitch] lines */
    ssize_t n = read(fd, Buffer, Length);
    if (t0)
        xbox_ft_io(xbox_ft_ms(t0));
    if (n < 0) {
        XBOX_TRACE(XBOX_LOG_FILE, "NtReadFile(handle=%p) errno=%d", FileHandle, errno);
        IoStatusBlock->Status = STATUS_UNSUCCESSFUL;
        IoStatusBlock->Information = 0;
        return STATUS_UNSUCCESSFUL;
    }

    IoStatusBlock->Information = (ULONG_PTR)n;
    if (n == 0 && Length > 0) {
        IoStatusBlock->Status = STATUS_END_OF_FILE;
        return STATUS_END_OF_FILE;
    }
    IoStatusBlock->Status = STATUS_SUCCESS;
    if (Event) SetEvent(Event);
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtWriteFile(
    HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
    PXBOX_IO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length,
    PLARGE_INTEGER ByteOffset)
{
    (void)ApcRoutine; (void)ApcContext;
    if (!IoStatusBlock)
        return STATUS_INVALID_PARAMETER;

    int fd = w32_handle_fd(FileHandle);
    if (fd < 0) {
        IoStatusBlock->Status = STATUS_INVALID_HANDLE;
        return STATUS_INVALID_HANDLE;
    }

    if (ByteOffset && ByteOffset->QuadPart >= 0)
        lseek(fd, (off_t)ByteOffset->QuadPart, SEEK_SET);

    ssize_t n = write(fd, Buffer, Length);
    if (n < 0) {
        XBOX_TRACE(XBOX_LOG_FILE, "NtWriteFile(handle=%p) errno=%d", FileHandle, errno);
        IoStatusBlock->Status = STATUS_UNSUCCESSFUL;
        IoStatusBlock->Information = 0;
        return STATUS_UNSUCCESSFUL;
    }

    IoStatusBlock->Status = STATUS_SUCCESS;
    IoStatusBlock->Information = (ULONG_PTR)n;
    if (Event) SetEvent(Event);
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtClose(HANDLE Handle)
{
    XBOX_TRACE(XBOX_LOG_FILE, "NtClose(handle=%p)", Handle);
    if (Handle && Handle != INVALID_HANDLE_VALUE) {
        xbox_file_close_host(Handle);       /* as on Windows: commits a pending save, drops a directory search */
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_HANDLE;
}

NTSTATUS __stdcall xbox_NtDeleteFile(PXBOX_OBJECT_ATTRIBUTES ObjectAttributes)
{
    char host_path[XBOX_HOST_PATH];
    const char* xbox_path = get_xbox_path(ObjectAttributes);
    if (!xbox_path || !xbox_translate_path(xbox_path, host_path, XBOX_HOST_PATH))
        return STATUS_OBJECT_PATH_NOT_FOUND;
    XBOX_TRACE(XBOX_LOG_FILE, "NtDeleteFile: %s", host_path);
    if (unlink(host_path) == 0) return STATUS_SUCCESS;
    if (rmdir(host_path)  == 0) return STATUS_SUCCESS;
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS __stdcall xbox_NtQueryInformationFile(
    HANDLE FileHandle, PXBOX_IO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation, ULONG Length, XBOX_FILE_INFORMATION_CLASS FileInformationClass)
{
    (void)Length;
    if (!IoStatusBlock || !FileInformation)
        return STATUS_INVALID_PARAMETER;

    int fd = w32_handle_fd(FileHandle);
    if (fd < 0)
        return STATUS_INVALID_HANDLE;

    struct stat st;
    if (FileInformationClass != XboxFilePositionInformation) {
        if (fstat(fd, &st) != 0)
            return STATUS_UNSUCCESSFUL;
    }

    switch (FileInformationClass) {
        case XboxFileBasicInformation: {
            PXBOX_FILE_BASIC_INFORMATION info = (PXBOX_FILE_BASIC_INFORMATION)FileInformation;
            unix_to_filetime(st.st_ctime, 0, &info->CreationTime);
            unix_to_filetime(st.st_atime, 0, &info->LastAccessTime);
            unix_to_filetime(st.st_mtime, 0, &info->LastWriteTime);
            info->ChangeTime = info->LastWriteTime;
            info->FileAttributes = mode_to_xbox_attrs(st.st_mode);
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(XBOX_FILE_BASIC_INFORMATION);
            return STATUS_SUCCESS;
        }
        case XboxFileStandardInformation: {
            PXBOX_FILE_STANDARD_INFORMATION info = (PXBOX_FILE_STANDARD_INFORMATION)FileInformation;
            info->EndOfFile.QuadPart = st.st_size;
            info->AllocationSize.QuadPart = (st.st_size + 4095) & ~4095LL;
            info->NumberOfLinks = (ULONG)st.st_nlink;
            info->DeletePending = FALSE;
            info->Directory = S_ISDIR(st.st_mode) ? TRUE : FALSE;
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(XBOX_FILE_STANDARD_INFORMATION);
            return STATUS_SUCCESS;
        }
        case XboxFilePositionInformation: {
            PXBOX_FILE_POSITION_INFORMATION info = (PXBOX_FILE_POSITION_INFORMATION)FileInformation;
            off_t pos = lseek(fd, 0, SEEK_CUR);
            if (pos < 0) return STATUS_UNSUCCESSFUL;
            info->CurrentByteOffset.QuadPart = pos;
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(XBOX_FILE_POSITION_INFORMATION);
            return STATUS_SUCCESS;
        }
        case XboxFileNetworkOpenInformation: {
            PXBOX_FILE_NETWORK_OPEN_INFORMATION info = (PXBOX_FILE_NETWORK_OPEN_INFORMATION)FileInformation;
            unix_to_filetime(st.st_ctime, 0, &info->CreationTime);
            unix_to_filetime(st.st_atime, 0, &info->LastAccessTime);
            unix_to_filetime(st.st_mtime, 0, &info->LastWriteTime);
            info->ChangeTime = info->LastWriteTime;
            info->EndOfFile.QuadPart = st.st_size;
            info->AllocationSize.QuadPart = (st.st_size + 4095) & ~4095LL;
            info->FileAttributes = mode_to_xbox_attrs(st.st_mode);
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(XBOX_FILE_NETWORK_OPEN_INFORMATION);
            return STATUS_SUCCESS;
        }
        default:
            xbox_log(XBOX_LOG_WARN, XBOX_LOG_FILE,
                "NtQueryInformationFile: unhandled class %d", FileInformationClass);
            return STATUS_NOT_IMPLEMENTED;
    }
}

NTSTATUS __stdcall xbox_NtSetInformationFile(
    HANDLE FileHandle, PXBOX_IO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation, ULONG Length, XBOX_FILE_INFORMATION_CLASS FileInformationClass)
{
    (void)Length;
    if (!IoStatusBlock || !FileInformation)
        return STATUS_INVALID_PARAMETER;

    int fd = w32_handle_fd(FileHandle);
    if (fd < 0)
        return STATUS_INVALID_HANDLE;

    switch (FileInformationClass) {
        case XboxFilePositionInformation: {
            PXBOX_FILE_POSITION_INFORMATION info = (PXBOX_FILE_POSITION_INFORMATION)FileInformation;
            if (lseek(fd, (off_t)info->CurrentByteOffset.QuadPart, SEEK_SET) < 0)
                return STATUS_UNSUCCESSFUL;
            IoStatusBlock->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
        case XboxFileEndOfFileInformation: {
            PXBOX_FILE_END_OF_FILE_INFORMATION info = (PXBOX_FILE_END_OF_FILE_INFORMATION)FileInformation;
            if (ftruncate(fd, (off_t)info->EndOfFile.QuadPart) != 0)
                return STATUS_UNSUCCESSFUL;
            IoStatusBlock->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
        case XboxFileAllocationInformation: {
            /* Reserve space (see the Windows one: Halo's save path, Orta's Z: cache .tmp files with
             * OVERWRITE_IF). NT semantics: a size below the end of file cuts the file there; a larger one
             * only reserves, the end of file stays. Linux: fallocate KEEP_SIZE; a filesystem without it
             * (EOPNOTSUPP: tmpfs on old kernels, 9p/drvfs under WSL) still succeeds - the reservation is
             * a hint, the later writes grow the file. */
            PXBOX_FILE_END_OF_FILE_INFORMATION info = (PXBOX_FILE_END_OF_FILE_INFORMATION)FileInformation;
            off_t want = (off_t)info->EndOfFile.QuadPart;
            struct stat st;
            if (want < 0 || fstat(fd, &st) != 0)
                return STATUS_INVALID_PARAMETER;
            if (want < st.st_size) {
                if (ftruncate(fd, want) != 0)
                    return STATUS_UNSUCCESSFUL;
            }
#if defined(__linux__)
            else if (want > st.st_size)
                (void)fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, want);
#endif
            IoStatusBlock->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
        case XboxFileDispositionInformation: {
            PXBOX_FILE_DISPOSITION_INFORMATION info = (PXBOX_FILE_DISPOSITION_INFORMATION)FileInformation;
            /* A pending save copy: the save goes at close (the copy with it). */
            if (shadow_mark_delete(FileHandle, info->DeleteFile != 0)) {
                IoStatusBlock->Status = STATUS_SUCCESS;
                return STATUS_SUCCESS;
            }
            /* POSIX: unlinking an open file removes it on last close -- this
             * matches NT "delete on close" semantics exactly. */
            if (info->DeleteFile) {
                const char* p = w32_handle_path(FileHandle);
                if (p) unlink(p);
            }
            IoStatusBlock->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        }
        case XboxFileBasicInformation:
            /* Setting file times is non-essential for the game; accept it. */
            IoStatusBlock->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        default:
            /* stderr, not xbox_log: WARN is filtered out by default, and an
             * unimplemented info class is exactly the kind of silent gap that
             * surfaces far away. Halo's save path hits one, gets
             * STATUS_NOT_IMPLEMENTED, converts it to DOS error 317 and asserts
             * "couldn't open or create saved game file". */
            fprintf(stderr, "  [FILE] NtSetInformationFile: unhandled class %d\n",
                    (int)FileInformationClass);
            fflush(stderr);
            return STATUS_NOT_IMPLEMENTED;
    }
}

NTSTATUS xbox_file_dismount(HANDLE h)
{
    const char *p = h ? w32_handle_path(h) : NULL;   /* the partition image the device opened (kernel_path.c) */
    if (p)
        xbox_path_utility_formatted(p);
    return STATUS_SUCCESS;
}

/* FileRenameInformation, POSIX (see the Windows one): rename(2) of the handle's path. The handle keeps working
 * (POSIX renames an open file in place); its recorded path stays the old one, which only names log lines.
 * A save written through a pending copy is renamed at its commit (close) instead. */
NTSTATUS xbox_file_rename(HANDLE file, HANDLE root, const char *xbox_name, int replace)
{
    char target[XBOX_HOST_PATH], save_name[XBOX_HOST_PATH] = "";
    const char *src = w32_handle_path(file);
    struct stat st;

    if (!src || !xbox_name || !xbox_name[0])
        return STATUS_INVALID_PARAMETER;
    {   /* a pending save copy lives in .pending: a bare name is relative to the SAVE's folder, not the copy's */
        SaveShadow *sh;
        pthread_mutex_lock(&s_shadow_lock);
        if ((sh = shadow_find(file)) != NULL)
            snprintf(save_name, sizeof save_name, "%s", sh->final_path);
        pthread_mutex_unlock(&s_shadow_lock);
    }
    if (xbox_name[0] == '\\' || (xbox_name[0] && xbox_name[1] == ':')) {
        if (!xbox_translate_path(xbox_name, target, sizeof target))
            return STATUS_OBJECT_PATH_NOT_FOUND;
    } else {
        const char *base = root ? w32_handle_path(root) : save_name[0] ? save_name : src;
        const char *slash;
        size_t n;
        if (!base)
            return STATUS_OBJECT_PATH_NOT_FOUND;
        slash = root ? NULL : strrchr(base, '/');
        n = slash ? (size_t)(slash - base) : strlen(base);
        if (n + 2 + strlen(xbox_name) >= sizeof target)
            return STATUS_OBJECT_PATH_NOT_FOUND;
        snprintf(target, sizeof target, "%.*s/%s", (int)n, base, xbox_name);
    }
    if (!replace && stat(target, &st) == 0)
        return STATUS_OBJECT_NAME_COLLISION;
    {   /* a save written through a pending copy: the commit at close goes to the new name, the old one goes */
        SaveShadow *sh;
        pthread_mutex_lock(&s_shadow_lock);
        sh = shadow_find(file);
        if (sh) {
            if (stat(sh->final_path, &st) == 0 && !sh->delete_old) {
                snprintf(sh->old_final, sizeof sh->old_final, "%s", sh->final_path);
                sh->delete_old = 1;
            }
            snprintf(sh->final_path, sizeof sh->final_path, "%s", target);
            pthread_mutex_unlock(&s_shadow_lock);
            return STATUS_SUCCESS;
        }
        pthread_mutex_unlock(&s_shadow_lock);
    }
    if (rename(src, target) != 0) {
        fprintf(stderr, "  [FILE] rename %s -> %s refused (errno %d)\n", src, target, errno);
        return errno == ENOENT ? STATUS_OBJECT_PATH_NOT_FOUND
             : errno == EACCES ? STATUS_ACCESS_DENIED : STATUS_UNSUCCESSFUL;
    }
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtQueryVolumeInformationFile(
    HANDLE FileHandle, PXBOX_IO_STATUS_BLOCK IoStatusBlock,
    PVOID FsInformation, ULONG Length, XBOX_FS_INFORMATION_CLASS FsInformationClass)
{
    (void)Length;
    if (!IoStatusBlock || !FsInformation)
        return STATUS_INVALID_PARAMETER;

    switch (FsInformationClass) {
        case XboxFileFsSizeInformation: {
            PXBOX_FILE_FS_SIZE_INFORMATION info = (PXBOX_FILE_FS_SIZE_INFORMATION)FsInformation;
            struct statvfs vfs;
            int fd = w32_handle_fd(FileHandle);
            /* Xbox geometry, not the host's -- see the note on
             * XBOX_SECTORS_PER_CLUSTER above. */
            info->BytesPerSector = XBOX_BYTES_PER_SECTOR;
            info->SectorsPerAllocationUnit = XBOX_SECTORS_PER_CLUSTER;
            if (fd >= 0 && fstatvfs(fd, &vfs) == 0) {
                ULONGLONG cs = (ULONGLONG)info->BytesPerSector * info->SectorsPerAllocationUnit;
                ULONGLONG total = (ULONGLONG)vfs.f_blocks * vfs.f_frsize;
                ULONGLONG avail = (ULONGLONG)vfs.f_bavail * vfs.f_frsize;
                info->TotalAllocationUnits.QuadPart = total / cs;
                info->AvailableAllocationUnits.QuadPart = avail / cs;
            } else {
                info->TotalAllocationUnits.QuadPart = 1048576;
                info->AvailableAllocationUnits.QuadPart = 524288;
            }
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(XBOX_FILE_FS_SIZE_INFORMATION);
            return STATUS_SUCCESS;
        }
        default:
            xbox_log(XBOX_LOG_WARN, XBOX_LOG_FILE,
                "NtQueryVolumeInformationFile: unhandled class %d", FsInformationClass);
            return STATUS_NOT_IMPLEMENTED;
    }
}

NTSTATUS __stdcall xbox_NtFlushBuffersFile(HANDLE FileHandle, PXBOX_IO_STATUS_BLOCK IoStatusBlock)
{
    int fd = w32_handle_fd(FileHandle);
    if (fd >= 0) fsync(fd);
    if (IoStatusBlock) {
        IoStatusBlock->Status = STATUS_SUCCESS;
        IoStatusBlock->Information = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtQueryFullAttributesFile(
    PXBOX_OBJECT_ATTRIBUTES ObjectAttributes,
    PXBOX_FILE_NETWORK_OPEN_INFORMATION FileInformation)
{
    char host_path[XBOX_HOST_PATH];
    struct stat st;

    if (!FileInformation)
        return STATUS_INVALID_PARAMETER;
    const char* xbox_path = get_xbox_path(ObjectAttributes);
    if (!xbox_path || !xbox_translate_path(xbox_path, host_path, XBOX_HOST_PATH))
        return STATUS_OBJECT_PATH_NOT_FOUND;
    if (stat(host_path, &st) != 0)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    unix_to_filetime(st.st_ctime, 0, &FileInformation->CreationTime);
    unix_to_filetime(st.st_atime, 0, &FileInformation->LastAccessTime);
    unix_to_filetime(st.st_mtime, 0, &FileInformation->LastWriteTime);
    FileInformation->ChangeTime = FileInformation->LastWriteTime;
    FileInformation->EndOfFile.QuadPart = st.st_size;
    FileInformation->AllocationSize.QuadPart = (st.st_size + 4095) & ~4095LL;
    FileInformation->FileAttributes = mode_to_xbox_attrs(st.st_mode);
    return STATUS_SUCCESS;
}

/* Directory enumeration state, keyed by the directory's Nt handle. */
#define MAX_DIR_CONTEXTS 64
typedef struct {
    HANDLE handle;
    DIR*   dir;
    char   pattern[64];
} DIR_CONTEXT;

static DIR_CONTEXT s_dir_contexts[MAX_DIR_CONTEXTS];
static CRITICAL_SECTION s_dir_cs;
static BOOL s_dir_cs_init = FALSE;

/* As on Windows: a closed directory handle takes its enumeration with it. The compat layer reuses handle values,
 * and a search the title abandons at its first match (Orta's Z: cache check: open "Partition5\", query
 * "laser.txb", close) otherwise went on under the next directory opened at the same value - its first answer was
 * the OLD search's next entry under the old pattern, so every cache hit after the first one read as a miss and the
 * file was copied from the disc again (an Orta run under WSL, 2026-11-10: rename onto the existing name ->
 * NAME_COLLISION, .tmp files left behind). */
void xbox_dir_context_drop(HANDLE FileHandle)
{
    if (!s_dir_cs_init || !FileHandle)
        return;
    EnterCriticalSection(&s_dir_cs);
    for (int i = 0; i < MAX_DIR_CONTEXTS; i++) {
        if (s_dir_contexts[i].handle == FileHandle) {
            if (s_dir_contexts[i].dir)
                closedir(s_dir_contexts[i].dir);
            s_dir_contexts[i].dir = NULL;
            s_dir_contexts[i].handle = NULL;
        }
    }
    LeaveCriticalSection(&s_dir_cs);
}

/* NT masks: "*.*" is every name (with or without a dot), as "*" (FsRtlIsNameInExpression's DOS rule the FATX
 * driver follows); everything else is matched case-insensitively. */
static int dir_name_matches(const char *pattern, const char *name)
{
    if (!strcmp(pattern, "*") || !strcmp(pattern, "*.*"))
        return 1;
    return fnmatch(pattern, name, FNM_CASEFOLD | FNM_NOESCAPE) == 0;
}

NTSTATUS __stdcall xbox_NtQueryDirectoryFile(
    HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
    PXBOX_IO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length,
    XBOX_FILE_INFORMATION_CLASS FileInformationClass,
    PXBOX_ANSI_STRING FileName, BOOLEAN RestartScan)
{
    (void)Event; (void)ApcRoutine; (void)ApcContext;
    if (!IoStatusBlock || !FileInformation)
        return STATUS_INVALID_PARAMETER;
    IoStatusBlock->Information = 0;
    if (FileInformationClass != XboxFileDirectoryInformation) {
        IoStatusBlock->Status = STATUS_INVALID_INFO_CLASS;
        return STATUS_INVALID_INFO_CLASS;
    }

    if (!s_dir_cs_init) { InitializeCriticalSection(&s_dir_cs); s_dir_cs_init = TRUE; }
    EnterCriticalSection(&s_dir_cs);

    /* Locate or create the per-handle enumeration context. */
    DIR_CONTEXT* ctx = NULL;
    for (int i = 0; i < MAX_DIR_CONTEXTS; i++)
        if (s_dir_contexts[i].handle == FileHandle) { ctx = &s_dir_contexts[i]; break; }
    if (!ctx) {
        for (int i = 0; i < MAX_DIR_CONTEXTS; i++)
            if (s_dir_contexts[i].handle == NULL) { ctx = &s_dir_contexts[i]; break; }
        if (!ctx) { LeaveCriticalSection(&s_dir_cs); return STATUS_INSUFFICIENT_RESOURCES; }
        ctx->handle = FileHandle;
        ctx->dir = NULL;
    }

    if (RestartScan || ctx->dir == NULL) {
        if (ctx->dir) { closedir(ctx->dir); ctx->dir = NULL; }
        const char* dpath = w32_handle_path(FileHandle);
        if (!dpath) { LeaveCriticalSection(&s_dir_cs); return STATUS_UNSUCCESSFUL; }
        ctx->dir = opendir(dpath);
        if (!ctx->dir) {
            LeaveCriticalSection(&s_dir_cs);
            IoStatusBlock->Status = STATUS_NO_MORE_FILES;
            return STATUS_NO_MORE_FILES;
        }
        if (FileName && FileName->Buffer && FileName->Length > 0) {
            USHORT n = FileName->Length;
            if (n >= sizeof(ctx->pattern)) n = sizeof(ctx->pattern) - 1;
            memcpy(ctx->pattern, FileName->Buffer, n);
            ctx->pattern[n] = '\0';
        } else {
            strcpy(ctx->pattern, "*");
        }
    }

    /* Advance to the next entry matching the search pattern. */
    struct dirent* de;
    const char* dpath = w32_handle_path(FileHandle);
    struct stat st;
    for (;;) {
        de = readdir(ctx->dir);
        if (!de) {
            closedir(ctx->dir);
            ctx->dir = NULL;
            ctx->handle = NULL;
            LeaveCriticalSection(&s_dir_cs);
            IoStatusBlock->Status = STATUS_NO_MORE_FILES;
            return STATUS_NO_MORE_FILES;
        }
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        if (dir_name_matches(ctx->pattern, de->d_name))
            break;
    }

    char full[MAX_PATH];
    snprintf(full, sizeof(full), "%s/%s", dpath ? dpath : ".", de->d_name);
    if (stat(full, &st) != 0)
        memset(&st, 0, sizeof(st));
    LeaveCriticalSection(&s_dir_cs);

    PXBOX_FILE_DIRECTORY_INFORMATION entry = (PXBOX_FILE_DIRECTORY_INFORMATION)FileInformation;
    memset(entry, 0, Length);

    int name_len = (int)strlen(de->d_name);
    entry->NextEntryOffset = 0;
    entry->FileIndex = 0;
    unix_to_filetime(st.st_ctime, 0, &entry->CreationTime);
    unix_to_filetime(st.st_atime, 0, &entry->LastAccessTime);
    unix_to_filetime(st.st_mtime, 0, &entry->LastWriteTime);
    entry->ChangeTime = entry->LastWriteTime;
    entry->EndOfFile.QuadPart = st.st_size;
    entry->AllocationSize.QuadPart = (st.st_size + 4095) & ~4095LL;
    entry->FileAttributes = mode_to_xbox_attrs(st.st_mode);
    entry->FileNameLength = name_len;

    ULONG header_size = (ULONG)((ULONG_PTR)&((PXBOX_FILE_DIRECTORY_INFORMATION)0)->FileName);
    if (name_len > 0 && (header_size + (ULONG)name_len) <= Length)
        memcpy(entry->FileName, de->d_name, name_len);
    IoStatusBlock->Status = STATUS_SUCCESS;
    IoStatusBlock->Information = header_size + name_len;
    return STATUS_SUCCESS;
}

#endif /* _WIN32 */

/* ======================================================================== */
/* ====================  Platform-independent  ============================ */
/* ======================================================================== */

NTSTATUS __stdcall xbox_NtOpenFile(
    PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
    PXBOX_OBJECT_ATTRIBUTES ObjectAttributes, PXBOX_IO_STATUS_BLOCK IoStatusBlock,
    ULONG ShareAccess, ULONG OpenOptions)
{
    /* NtOpenFile is NtCreateFile with FILE_OPEN disposition */
    return xbox_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes,
        IoStatusBlock, NULL, 0, ShareAccess, XBOX_FILE_OPEN, OpenOptions);
}

NTSTATUS __stdcall xbox_IoCreateFile(
    PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
    PXBOX_OBJECT_ATTRIBUTES ObjectAttributes, PXBOX_IO_STATUS_BLOCK IoStatusBlock,
    PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess,
    ULONG Disposition, ULONG CreateOptions, ULONG Options)
{
    (void)Options;
    return xbox_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock,
        AllocationSize, FileAttributes, ShareAccess, Disposition, CreateOptions);
}

NTSTATUS __stdcall xbox_NtFsControlFile(
    HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
    PXBOX_IO_STATUS_BLOCK IoStatusBlock, ULONG FsControlCode,
    PVOID InputBuffer, ULONG InputBufferLength,
    PVOID OutputBuffer, ULONG OutputBufferLength)
{
    (void)FileHandle; (void)Event; (void)ApcRoutine; (void)ApcContext;
    (void)InputBuffer; (void)InputBufferLength; (void)OutputBuffer; (void)OutputBufferLength;
    xbox_log(XBOX_LOG_WARN, XBOX_LOG_FILE, "NtFsControlFile(0x%X) - stub", FsControlCode);
    if (IoStatusBlock) {
        IoStatusBlock->Status = STATUS_NOT_IMPLEMENTED;
        IoStatusBlock->Information = 0;
    }
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS __stdcall xbox_NtDeviceIoControlFile(
    HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
    PXBOX_IO_STATUS_BLOCK IoStatusBlock, ULONG IoControlCode,
    PVOID InputBuffer, ULONG InputBufferLength,
    PVOID OutputBuffer, ULONG OutputBufferLength)
{
    (void)FileHandle; (void)Event; (void)ApcRoutine; (void)ApcContext;
    (void)InputBuffer; (void)InputBufferLength; (void)OutputBuffer; (void)OutputBufferLength;
    xbox_log(XBOX_LOG_WARN, XBOX_LOG_FILE, "NtDeviceIoControlFile(0x%X) - stub", IoControlCode);
    if (IoStatusBlock) {
        IoStatusBlock->Status = STATUS_NOT_IMPLEMENTED;
        IoStatusBlock->Information = 0;
    }
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS __stdcall xbox_NtOpenSymbolicLinkObject(
    PHANDLE LinkHandle, PXBOX_OBJECT_ATTRIBUTES ObjectAttributes)
{
    /*
     * Xbox uses symbolic links for drive-letter mapping (D: -> \Device\CdRom0).
     * Path translation handles this transparently, so return a dummy handle.
     */
    if (LinkHandle)
        *LinkHandle = (HANDLE)(ULONG_PTR)0xDEAD0001;
    XBOX_TRACE(XBOX_LOG_FILE, "NtOpenSymbolicLinkObject(%s) - stub",
        get_xbox_path(ObjectAttributes) ? get_xbox_path(ObjectAttributes) : "?");
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtQuerySymbolicLinkObject(
    HANDLE LinkHandle, PXBOX_ANSI_STRING LinkTarget, PULONG ReturnedLength)
{
    (void)LinkHandle;
    const char* target = "\\Device\\CdRom0";
    if (LinkTarget && LinkTarget->Buffer) {
        USHORT len = (USHORT)strlen(target);
        if (len < LinkTarget->MaximumLength) {
            memcpy(LinkTarget->Buffer, target, len + 1);
            LinkTarget->Length = len;
        }
    }
    if (ReturnedLength)
        *ReturnedLength = (ULONG)strlen(target);
    return STATUS_SUCCESS;
}
