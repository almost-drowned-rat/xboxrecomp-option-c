/*
 * xinput_record.c -- RECOMP_PAD_RECORD: a play session becomes a pad script (2026-11-01).
 *
 * The maintainer plays; the recording replays as RECOMP_PAD_SCRIPT=@file (syntax: xinput_script.c). What is
 * recorded is the state the title READS - xbox_InputGetState after the host pad, the keyboard (through its binding)
 * and any script are merged - so a real controller and the keyboard record the same way, through the USB pad model
 * and the XInput HLE alike.
 *
 * One line per held state, written when it ends, with its start and its length in the chosen time base:
 *
 *   r1234:p1-a+lx=-12000+ly=3000:18     # f1240 ms20667
 *
 * RECOMP_PAD_RECORD_BASE picks the base (default r):
 *   r  reads   - pad reads of that port by the title, from its first read. A title that polls once per game frame
 *                and not while it loads replays independent of load times.
 *   f  frames  - guest vblanks (xbox_GuestVblankCount) from the first read: 60 per second, also during loads;
 *                or, with RECOMP_PAD_GAME_FRAME=<hex VA>, the title's own frame counter there (xbox_InputFrameClock),
 *                which stands still through a hitch as the game does.
 *   ms         - milliseconds from the first read (the script's original base).
 * The comment carries the other two stamps, so a recording can be re-based by hand.
 *
 * Analog buttons are written with their value unless fully pressed ("a=128"); sticks with their value
 * ("lx=-12000"), with a band of +-RECORD_DEADBAND around a held value so stick noise does not split one held state
 * into hundreds; a stick inside XInput's dead zone reads as centred. A state with nothing held is not written.
 *
 * The caller can be the USB DPC (a hot path: no blocking I/O, toolkit rule): lines go into a buffer, and a writer
 * thread of its own appends them to the file every 250 ms; the rest is written at exit. The default file is
 * <data dir>\pad_record.txt next to the port's other data - never outside the port folder; RECOMP_PAD_RECORD=1 picks
 * it, any other value is the path.
 */
#include "xinput_xbox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECORD_DEADBAND 512
#define RECORD_BUF      (256 * 1024)

extern const char *xbox_getenv_utf8(const char *name);
extern FILE *xbox_fopen_utf8(const char *path, const char *mode);
extern uint64_t xbox_InputFrameClock(void);   /* xinput_script.c: vblanks, or RECOMP_PAD_GAME_FRAME's counter */

enum { BASE_READS, BASE_FRAMES, BASE_MS };

typedef struct {
    int           active;      /* something held */
    XBOX_GAMEPAD  g;           /* the state as recorded (sticks after the deadband) */
    unsigned long start_r, start_ms;
    uint64_t      start_f;
} HeldState;

static int s_on = -1, s_base = BASE_READS;
static char s_path[600];
static CRITICAL_SECTION s_lock;
static char *s_buf;
static size_t s_len;
static HANDLE s_writer;
static unsigned long s_t0;
static uint64_t s_f0;
static HeldState s_held[XBOX_MAX_CONTROLLERS];

static void flush_locked_copy(void)
{
    char *copy = NULL;
    size_t n;
    FILE *f;

    EnterCriticalSection(&s_lock);
    n = s_len;
    if (n) {
        copy = (char *)malloc(n);
        if (copy)
            memcpy(copy, s_buf, n);
        s_len = 0;
    }
    LeaveCriticalSection(&s_lock);
    if (!copy)
        return;
    f = xbox_fopen_utf8(s_path, "ab");
    if (f) {
        fwrite(copy, 1, n, f);
        fclose(f);
    }
    free(copy);
}

static DWORD WINAPI writer_main(LPVOID unused)
{
    (void)unused;
    for (;;) {
        Sleep(250);
        flush_locked_copy();
    }
    return 0;
}

static void finish_all(void);

static void record_atexit(void)
{
    finish_all();
    flush_locked_copy();
}

/* End every held state and write everything out now (shutdown paths that skip exit(), tests). */
void xbox_InputRecordFlush(void)
{
    if (s_on == 1)
        record_atexit();
}

static int record_on(void)
{
    const char *spec, *base;
    FILE *f;

    if (s_on >= 0)
        return s_on;
    s_on = 0;
    spec = xbox_getenv_utf8("RECOMP_PAD_RECORD");
    if (!spec || !*spec || !strcmp(spec, "0"))
        return 0;
    if (!strcmp(spec, "1")) {
        extern const char *xbox_path_data_dir_utf8(void);
        const char *d = xbox_path_data_dir_utf8();
        snprintf(s_path, sizeof s_path, "%s%spad_record.txt", d ? d : "", d && *d ? "/" : "");
    } else {
        snprintf(s_path, sizeof s_path, "%s", spec);
    }
    base = xbox_getenv_utf8("RECOMP_PAD_RECORD_BASE");
    if (base && (base[0] == 'f' || base[0] == 'F'))
        s_base = BASE_FRAMES;
    else if (base && (base[0] == 'm' || base[0] == 'M'))
        s_base = BASE_MS;
    s_buf = (char *)malloc(RECORD_BUF);
    if (!s_buf)
        return 0;
    InitializeCriticalSection(&s_lock);
    f = xbox_fopen_utf8(s_path, "wb");              /* a fresh recording */
    if (!f) {
        fprintf(stderr, "  PAD: cannot write the recording %s\n", s_path);
        return 0;
    }
    fprintf(f, "# pad recording (RECOMP_PAD_RECORD), base %s; replay with RECOMP_PAD_SCRIPT=@<this file>%s\n",
            s_base == BASE_READS ? "r (pad reads)" : s_base == BASE_FRAMES ? "f (guest vblanks)" : "ms",
            s_base == BASE_MS ? "" : " RECOMP_PAD_TIME is not needed: the base is in every entry");
    fclose(f);
    s_writer = CreateThread(NULL, 0, writer_main, NULL, 0, NULL);
    atexit(record_atexit);
    fprintf(stderr, "  PAD: recording to %s\n", s_path);
    s_on = 1;
    return 1;
}

/* XInput's default dead zones (XINPUT_GAMEPAD_LEFT/RIGHT_THUMB_DEADZONE). A stick at rest is not at 0: the
 * maintainer's pad rested at ~642 on three axes, above the +-512 noise band, and a 200-s playtest recorded 6,488 lines
 * of stick noise (2026-11-01). Anything inside the dead zone records as centred; titles apply a dead zone
 * of their own on top. */
#define DEADZONE_LEFT  7849
#define DEADZONE_RIGHT 8689

static int16_t deadband(int16_t now, int16_t held, int was_active, int zone)
{
    if (now > -zone && now < zone)
        return 0;                                    /* at rest */
    if (was_active && held && abs((int)now - (int)held) < RECORD_DEADBAND)
        return held;                                 /* noise around a held value */
    return now;
}

static int is_neutral(const XBOX_GAMEPAD *g)
{
    int j;
    if (g->wButtons || g->sThumbLX || g->sThumbLY || g->sThumbRX || g->sThumbRY)
        return 0;
    for (j = 0; j < 8; j++)
        if (g->bAnalogButtons[j] > XBOX_ANALOG_BUTTON_THRESHOLD)
            return 0;
    return 1;
}

static void append(const char *line)
{
    size_t n = strlen(line);
    EnterCriticalSection(&s_lock);
    if (s_len + n <= RECORD_BUF) {
        memcpy(s_buf + s_len, line, n);
        s_len += n;
    }
    LeaveCriticalSection(&s_lock);
}

/* The held state of a port ended at (r, f, ms): write it. */
static void finish(int port, unsigned long r, uint64_t f, unsigned long ms)
{
    static const char *const dig[8] = {"up", "down", "left", "right", "start", "back", "lthumb", "rthumb"};
    static const char *const ana[8] = {"a", "b", "x", "y", "black", "white", "lt", "rt"};
    HeldState *h = &s_held[port];
    char line[512];
    size_t o;
    int j, first = 1;
    unsigned long at, len;

    if (!h->active)
        return;
    h->active = 0;
    if (s_base == BASE_READS) {
        at = h->start_r;
        len = r - h->start_r;
    } else if (s_base == BASE_FRAMES) {
        at = (unsigned long)h->start_f;
        len = (unsigned long)(f - h->start_f);
    } else {
        at = h->start_ms;
        len = ms - h->start_ms;
    }
    if (!len)
        len = 1;
    o = (size_t)snprintf(line, sizeof line, "%s%lu:p%d-", s_base == BASE_READS ? "r" : s_base == BASE_FRAMES ? "f" : "",
                         at, port + 1);
#define SEP() do { if (!first) line[o++] = '+'; first = 0; } while (0)
    for (j = 0; j < 8 && o < sizeof line - 64; j++)
        if (h->g.wButtons & (1u << j)) {
            SEP();
            o += (size_t)snprintf(line + o, sizeof line - o, "%s", dig[j]);
        }
    for (j = 0; j < 8 && o < sizeof line - 64; j++)
        if (h->g.bAnalogButtons[j] > XBOX_ANALOG_BUTTON_THRESHOLD) {
            SEP();
            if (h->g.bAnalogButtons[j] == 0xFF)
                o += (size_t)snprintf(line + o, sizeof line - o, "%s", ana[j]);
            else
                o += (size_t)snprintf(line + o, sizeof line - o, "%s=%u", ana[j], h->g.bAnalogButtons[j]);
        }
    if (h->g.sThumbLX) { SEP(); o += (size_t)snprintf(line + o, sizeof line - o, "lx=%d", h->g.sThumbLX); }
    if (h->g.sThumbLY) { SEP(); o += (size_t)snprintf(line + o, sizeof line - o, "ly=%d", h->g.sThumbLY); }
    if (h->g.sThumbRX) { SEP(); o += (size_t)snprintf(line + o, sizeof line - o, "rx=%d", h->g.sThumbRX); }
    if (h->g.sThumbRY) { SEP(); o += (size_t)snprintf(line + o, sizeof line - o, "ry=%d", h->g.sThumbRY); }
#undef SEP
    if (first)
        o += (size_t)snprintf(line + o, sizeof line - o, "connect");
    snprintf(line + o, sizeof line - o, ":%lu    # r%lu f%llu ms%lu\n", len, h->start_r,
             (unsigned long long)h->start_f, h->start_ms);
    append(line);
}

static void finish_all(void)
{
    unsigned long ms = (unsigned long)GetTickCount() - s_t0;
    uint64_t f = xbox_InputFrameClock() - s_f0;
    int p;
    if (s_on != 1)
        return;
    for (p = 0; p < XBOX_MAX_CONTROLLERS; p++)
        finish(p, xbox_InputReadNow(p) + 1, f, ms);
}

void xbox_InputRecordFeed(int port, const XBOX_GAMEPAD *in)
{
    XBOX_GAMEPAD g;
    HeldState *h;
    unsigned long r, ms;
    uint64_t f;

    if (xbox_input_host_read || port < 0 || port >= XBOX_MAX_CONTROLLERS || !record_on())
        return;
    if (!s_t0) {
        s_t0 = (unsigned long)GetTickCount();
        s_f0 = xbox_InputFrameClock();
    }
    r = xbox_InputReadNow(port);         /* the read clock the script uses too (xinput_script.c) */
    ms = (unsigned long)GetTickCount() - s_t0;
    f = xbox_InputFrameClock() - s_f0;
    h = &s_held[port];

    g = *in;
    g.sThumbLX = deadband(in->sThumbLX, h->g.sThumbLX, h->active, DEADZONE_LEFT);
    g.sThumbLY = deadband(in->sThumbLY, h->g.sThumbLY, h->active, DEADZONE_LEFT);
    g.sThumbRX = deadband(in->sThumbRX, h->g.sThumbRX, h->active, DEADZONE_RIGHT);
    g.sThumbRY = deadband(in->sThumbRY, h->g.sThumbRY, h->active, DEADZONE_RIGHT);

    if (h->active && !memcmp(&g, &h->g, sizeof g))
        return;                                      /* the same state goes on */
    finish(port, r, f, ms);
    if (is_neutral(&g))
        return;
    h->active = 1;
    h->g = g;
    h->start_r = r;
    h->start_f = f;
    h->start_ms = ms;
}
