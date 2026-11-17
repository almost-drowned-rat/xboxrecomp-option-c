/* thread_name.h - name the calling host thread (2026-11-15): the runtime's own threads (timer, NV2A ack /
 * flag / tick, APU, OHCI, logger, ...) and the guest threads, so per-thread CPU (G3's threads.csv, a profiler,
 * Process Explorer, top -H) says which thread is which. Diagnostics only; nothing depends on it.
 * Windows: SetThreadDescription (Windows 10 1607+), looked up at run time - older systems just skip it.
 * Linux / Android: prctl(PR_SET_NAME), 15 characters. Elsewhere: nothing. */
#ifndef XR_THREAD_NAME_H
#define XR_THREAD_NAME_H

#if defined(_WIN32)
#include <windows.h>
static inline void xr_thread_name(const char *name)
{
    typedef HRESULT (WINAPI *set_fn)(HANDLE, PCWSTR);
    set_fn fn = (set_fn)(void (*)(void))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription");
    wchar_t w[64];
    int i;
    if (!fn || !name)
        return;
    for (i = 0; i < 63 && name[i]; i++)
        w[i] = (wchar_t)(unsigned char)name[i];
    w[i] = 0;
    fn(GetCurrentThread(), w);
}
#elif defined(__linux__)
#include <sys/prctl.h>
static inline void xr_thread_name(const char *name)
{
    char b[16];
    int i;
    if (!name)
        return;
    for (i = 0; i < 15 && name[i]; i++)
        b[i] = name[i];
    b[i] = 0;
    prctl(PR_SET_NAME, (unsigned long)b, 0, 0, 0);
}
#else
static inline void xr_thread_name(const char *name) { (void)name; }
#endif

#endif
