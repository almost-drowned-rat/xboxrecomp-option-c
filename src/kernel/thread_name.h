/* thread_name.h - name the calling host thread (2026-11-15): the runtime's own threads (timer, NV2A ack /
 * flag / tick, APU, OHCI, logger, ...) and the guest threads, so per-thread CPU (G3's threads.csv, a profiler,
 * Process Explorer, top -H) says which thread is which. Diagnostics only; nothing depends on it.
 * Windows: SetThreadDescription (Windows 10 1607+), looked up at run time - older systems just skip it.
 * Linux / Android: prctl(PR_SET_NAME), 15 characters. Elsewhere: nothing.
 *
 * Android also gives the runtime's four busy threads a fast core of their own (2026-11-24; RECOMP_ANDROID_PIN=0
 * turns it off). The phone schedulers (EAS / WALT) packed the timer / DPC thread and the NV2A flag thread onto the same
 * core while the load was low: D3D's interrupt DPC spins until the vblank bit drops, and the flag thread that drops
 * it waited for that core's time slice - DPCs ran up to 25 ms, the title missed a vblank 1-3 times a second (33 ms
 * frames, GAME SPEED 92-98 %). Measured on the Odin 2 (Snapdragon 8 Gen 2) with a recorded pad script, Orta mission 1,
 * 60 s: 64-73 long frames without, 3-4 with; DPC waits of the title 28-31 ms -> 4-9 ms; GPU time unchanged.
 * The cores: those above the slowest cluster by cpuinfo_max_freq, without the single fastest one when five or more
 * are left (the title's own threads like it best); four needed (one per thread), else only the DPC and the flag
 * thread get two of them, else nothing. Desktop Linux spreads such threads over idle cores by itself. */
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
#if defined(__ANDROID__)
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
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
#if defined(__ANDROID__)
    {
        static int cores[4] = { -2, -1, -1, -1 };  /* APU frame, NV2A flags, timer / DPC, NV2A exec; -2 = not read */
        int role = !strcmp(name, "xr apu frame") ? 0 : !strcmp(name, "xr nv2a flags") ? 1
                 : !strcmp(name, "xr timer dpc") ? 2 : !strcmp(name, "xr nv2a exec") ? 3 : -1;
        if (role < 0)
            return;
        if (cores[0] == -2) {
            const char *e = getenv("RECOMP_ANDROID_PIN");
            long f[64], lo = 0, hi = 0;
            int n = 0, c, k = 0, cand[64], top = -1;
            cores[0] = -1;
            if (e && *e == '0')
                return;
            for (c = 0; c < 64; c++) {
                char path[80], buf[32];
                int fd;
                ssize_t r;
                snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
                fd = open(path, O_RDONLY | O_CLOEXEC);
                if (fd < 0)
                    break;
                r = read(fd, buf, sizeof buf - 1);
                close(fd);
                buf[r > 0 ? r : 0] = 0;
                f[c] = atol(buf);
                if (!lo || f[c] < lo) lo = f[c];
                if (f[c] > hi) { hi = f[c]; top = c; }
                n = c + 1;
            }
            for (c = 0; c < n; c++)
                if (f[c] > lo)
                    cand[k++] = c;
            if (k >= 5) {                          /* leave the single fastest core to the title */
                int j = 0;
                for (c = 0; c < k; c++)
                    if (cand[c] != top)
                        cand[j++] = cand[c];
                k = j;
            }
            if (k >= 4) {
                for (c = 0; c < 4; c++)
                    cores[c] = cand[c];            /* ascending: APU, flags, DPC, exec */
            } else if (k >= 2) {
                cores[1] = cand[0];
                cores[2] = cand[1];
            }
            fprintf(stderr, "  [THREADS] Android: %d cores (slowest cluster %ld kHz); runtime threads on cores "
                    "APU %d, NV2A flags %d, timer/DPC %d, NV2A exec %d (-1 = left to the system; RECOMP_ANDROID_PIN=0 "
                    "turns it off)\n", n, lo, cores[0], cores[1], cores[2], cores[3]);
        }
        if (cores[role] >= 0) {
            unsigned long m[16] = { 0 };
            m[cores[role] / (8 * (int)sizeof m[0])] = 1ul << (cores[role] % (8 * (int)sizeof m[0]));
            syscall(__NR_sched_setaffinity, 0, sizeof m, m);
        }
    }
#endif
}
#else
static inline void xr_thread_name(const char *name) { (void)name; }
#endif

#endif
