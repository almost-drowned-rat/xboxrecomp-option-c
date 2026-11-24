/*
 * host_vblank.c - wait for the display's vertical blank (see host_vblank.h)
 *
 * Windows: D3DKMTOpenAdapterFromHdc on the window's monitor, then
 * D3DKMTWaitForVerticalBlankEvent on its VidPn source -- both exported by
 * gdi32 (kernel thunks; no D3D runtime, no DXGI), loaded at run time so a
 * system without them simply has no display pacing. The structs are declared
 * here (d3dkmthk.h is a WDK header).
 *
 * Android: the Choreographer's frame callbacks; Linux: DRM's vblank wait on the
 * first active CRTC (both 2026-11-24, from option-p's platform layer;
 * before them the 60 Hz timer drifted against a 60 Hz panel and showed a
 * picture twice every few frames).
 */

#include "host_vblank.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>

typedef LONG kmt_status;
typedef UINT kmt_handle;

typedef struct {
    HDC        hDc;
    kmt_handle hAdapter;
    LUID       AdapterLuid;
    UINT       VidPnSourceId;
} KmtOpenAdapterFromHdc;

typedef struct {
    kmt_handle hAdapter;
    kmt_handle hDevice;
    UINT       VidPnSourceId;
} KmtWaitForVerticalBlankEvent;

typedef struct {
    kmt_handle hAdapter;
} KmtCloseAdapter;

typedef kmt_status (APIENTRY *PfnOpen)(KmtOpenAdapterFromHdc *);
typedef kmt_status (APIENTRY *PfnWait)(const KmtWaitForVerticalBlankEvent *);
typedef kmt_status (APIENTRY *PfnClose)(const KmtCloseAdapter *);

static PfnWait  s_wait;
static PfnClose s_close;
static KmtWaitForVerticalBlankEvent s_vb;
static int s_open;

int host_vblank_open(void *native_window)
{
    HMODULE gdi;
    PfnOpen open_fn;
    HMONITOR mon;
    MONITORINFOEXW mi;
    KmtOpenAdapterFromHdc oa;
    kmt_status st;

    host_vblank_close();
    gdi = GetModuleHandleW(L"gdi32.dll");
    if (!gdi)
        gdi = LoadLibraryW(L"gdi32.dll");
    if (!gdi)
        return 0;
    open_fn = (PfnOpen)(void *)GetProcAddress(gdi, "D3DKMTOpenAdapterFromHdc");
    s_wait  = (PfnWait)(void *)GetProcAddress(gdi, "D3DKMTWaitForVerticalBlankEvent");
    s_close = (PfnClose)(void *)GetProcAddress(gdi, "D3DKMTCloseAdapter");
    if (!open_fn || !s_wait) {
        fprintf(stderr, "  display vblank: gdi32 has no D3DKMT vblank wait\n");
        return 0;
    }
    mon = MonitorFromWindow((HWND)native_window, MONITOR_DEFAULTTOPRIMARY);
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    if (!mon || !GetMonitorInfoW(mon, (MONITORINFO *)&mi))
        return 0;
    memset(&oa, 0, sizeof oa);
    oa.hDc = CreateDCW(NULL, mi.szDevice, NULL, NULL);
    if (!oa.hDc)
        return 0;
    st = open_fn(&oa);
    DeleteDC(oa.hDc);
    if (st != 0) {
        fprintf(stderr, "  display vblank: D3DKMTOpenAdapterFromHdc failed (0x%08lX)\n", (unsigned long)st);
        return 0;
    }
    s_vb.hAdapter = oa.hAdapter;
    s_vb.hDevice = 0;
    s_vb.VidPnSourceId = oa.VidPnSourceId;
    s_open = 1;
    return 1;
}

int host_vblank_wait(void)
{
    return s_open && s_wait(&s_vb) == 0;
}

void host_vblank_close(void)
{
    if (s_open && s_close) {
        KmtCloseAdapter ca;
        ca.hAdapter = s_vb.hAdapter;
        s_close(&ca);
    }
    s_open = 0;
}

#elif defined(__ANDROID__)

/* Android (2026-11-24; option-p's platform_linux.cpp as the model): the Choreographer's frame callbacks
 * on a thread of its own (its looper). Each callback = one display vblank; host_vblank_wait blocks for the next.
 * No callback for 100 ms (the app in the background holds them) = 0, the caller falls back to its timer. */
#include <android/choreographer.h>
#include <android/looper.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

static pthread_mutex_t s_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_cv;
static uint64_t        s_count;
static int             s_running;

static void frame_cb(int64_t frame_ns, void *data)
{
    (void)frame_ns;
    pthread_mutex_lock(&s_mx);
    s_count++;
    pthread_cond_broadcast(&s_cv);
    pthread_mutex_unlock(&s_mx);
    AChoreographer_postFrameCallback64((AChoreographer *)data, frame_cb, data);
}

static void *vsync_thread(void *unused)
{
    AChoreographer *c;
    (void)unused;
    pthread_setname_np(pthread_self(), "xr display vsync");
    ALooper_prepare(0);
    c = AChoreographer_getInstance();
    if (!c) {
        fprintf(stderr, "  display vblank: no Choreographer\n");
        return NULL;
    }
    AChoreographer_postFrameCallback64(c, frame_cb, c);
    for (;;)
        ALooper_pollOnce(-1, NULL, NULL, NULL);
    return NULL;
}

int host_vblank_open(void *native_window)
{
    (void)native_window;
    pthread_mutex_lock(&s_mx);
    if (!s_running) {
        pthread_condattr_t a;
        pthread_t t;
        pthread_condattr_init(&a);
        pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
        pthread_cond_init(&s_cv, &a);
        pthread_condattr_destroy(&a);
        if (pthread_create(&t, NULL, vsync_thread, NULL) == 0) {
            pthread_detach(t);
            s_running = 1;
        }
    }
    pthread_mutex_unlock(&s_mx);
    return s_running;
}

int host_vblank_wait(void)
{
    struct timespec ts;
    uint64_t seen;
    int r = 0;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_nsec += 100000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&s_mx);
    seen = s_count;
    while (s_count == seen && r != ETIMEDOUT)
        r = pthread_cond_timedwait(&s_cv, &s_mx, &ts);
    r = s_count != seen;
    pthread_mutex_unlock(&s_mx);
    return r;
}

void host_vblank_close(void)
{
    /* the looper thread stays (one per process); a later open uses it again */
}

#elif defined(__has_include) && !__has_include(<drm/drm.h>)

/* a Linux build without the kernel's DRM headers (a bare sysroot): no display vblank - the timer (2026-11-24) */
int host_vblank_open(void *native_window)
{
    (void)native_window;
    fprintf(stderr, "  display vblank: not in this build (no DRM headers) -> the timer\n");
    return 0;
}
int host_vblank_wait(void) { return 0; }
void host_vblank_close(void) {}

#else

/* Linux (2026-11-24; option-p's platform_linux.cpp as the model): DRM_IOCTL_WAIT_VBLANK on the first CRTC
 * that answers, on the primary node (no DRM master needed). The window is not used: the first active display. */
#include <drm/drm.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int      s_fd = -1;
static unsigned s_pipe;

static int drm_wait(int fd, unsigned pipe)
{
    union drm_wait_vblank w;
    int r;
    memset(&w, 0, sizeof w);
    w.request.type = (enum drm_vblank_seq_type)(_DRM_VBLANK_RELATIVE
                     | ((pipe << _DRM_VBLANK_HIGH_CRTC_SHIFT) & _DRM_VBLANK_HIGH_CRTC_MASK));
    w.request.sequence = 1;
    do
        r = ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &w);
    while (r != 0 && errno == EINTR);
    return r == 0;
}

int host_vblank_open(void *native_window)
{
    int card;
    (void)native_window;
    if (s_fd >= 0)
        return 1;
    for (card = 0; card < 8; card++) {
        char path[32];
        unsigned pipe;
        int fd;
        snprintf(path, sizeof path, "/dev/dri/card%d", card);
        fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0)
            continue;
        for (pipe = 0; pipe < 8; pipe++) {
            if (!drm_wait(fd, pipe))
                continue;
            s_fd = fd;
            s_pipe = pipe;
            fprintf(stderr, "  display vblank: %s, CRTC %u\n", path, pipe);
            return 1;
        }
        close(fd);
    }
    fprintf(stderr, "  display vblank: no DRM vblank on this host\n");
    return 0;
}

int host_vblank_wait(void)
{
    return s_fd >= 0 && drm_wait(s_fd, s_pipe);
}

void host_vblank_close(void)
{
    if (s_fd >= 0)
        close(s_fd);
    s_fd = -1;
}

#endif
