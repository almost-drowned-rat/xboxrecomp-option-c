/*
 * xinput_script.c -- scripted and synthetic pad input, for runs with nobody at
 * the controls (moved here from src/usb/usb_gamepad.c so the USB pad model and
 * the XAPI HLE layer drive the title from the same script).
 *
 * Applied on top of the host state in xbox_InputRead (xinput_device.c).
 */
#include "xinput_xbox.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USB_GAMEPAD_MAX XBOX_MAX_CONTROLLERS

/* A synthetic press, for bringing a title up without a pad on the desk.
 *
 * RECOMP_PAD_PRESS=0x10 holds Start for a quarter of a second every two
 * seconds. A title sitting on a "press Start" screen needs an edge, not a
 * level, so this pulses rather than latching -- and it repeats because the
 * moment the title starts reading input is not knowable from here.
 *
 * Bit values are the Xbox digital button mask: 0x01/02/04/08 dpad
 * up/down/left/right, 0x10 Start, 0x20 Back, 0x40/0x80 thumb clicks.
 *
 * This is a bring-up probe and nothing else. It is off unless the variable
 * is set, and a real pad on the host is always the better input.
 */
static uint8_t synthetic_buttons(void)
{
    static int      configured = -1;
    static unsigned mask, period_ms, hold_ms;
    static unsigned long t0;
    unsigned long now, phase;

    if (configured < 0) {
        const char *spec = getenv("RECOMP_PAD_PRESS");
        configured = 0;
        if (spec && *spec) {
            char *end;
            mask = (unsigned)strtoul(spec, &end, 0) & 0xFFu;
            period_ms = (*end == ',') ? (unsigned)strtoul(end + 1, &end, 0)
                                      : 2000u;
            hold_ms   = (*end == ',') ? (unsigned)strtoul(end + 1, &end, 0)
                                      : 250u;
            if (!period_ms) period_ms = 2000u;
            if (!hold_ms || hold_ms >= period_ms) hold_ms = period_ms / 4u;
            if (mask) {
                configured = 1;
                t0 = (unsigned long)GetTickCount();
                fprintf(stderr, "  PAD: synthesising button mask 0x%02X for "
                        "%u ms every %u ms\n", mask, hold_ms, period_ms);
                fflush(stderr);
            }
        }
    }
    if (!configured)
        return 0;
    now = (unsigned long)GetTickCount();
    phase = (now - t0) % period_ms;
    return (uint8_t)((phase < hold_ms) ? mask : 0u);
}

/* A scripted pad, for driving a title through its menus with nobody at it.
 *
 * RECOMP_PAD_PRESS pulses one digital mask forever, which gets past a
 * "press Start" screen and no further: a menu wants A, which is an analog
 * button on this pad, and it wants presses in order. This takes a timeline:
 *
 *   RECOMP_PAD_SCRIPT=40000:start,44000:a,46000:down,47000:a:400
 *
 * Each entry is <ms>:<button>[+<button>...][:<hold ms>], with times measured
 * from the first report the title asks for (so from enumeration, not from
 * process start) and a default hold of 150 ms. RECOMP_PAD_SCRIPT=@file reads
 * the same syntax from a file, one entry per line or comma separated; a '#'
 * where an entry would start comments out the rest of the line.
 *
 * "@include <file> <ms>" on a line of its own reads another script there, its
 * times shifted by <ms> (a path relative to the including file): reusable
 * snippets such as a cheat code entered from a menu.
 *
 * Buttons: up down left right start back lthumb rthumb (the digital byte),
 * a b x y black white lt rt (the analog bytes, reported fully pressed), and
 * lleft lright lup ldown / rleft rright rup rdown (a stick at full throw),
 * and "connect" (nothing pressed). Every controller the script names counts
 * as connected from the start (xbox_InputScriptPorts): "0:p2-connect" is a
 * second player for split-screen tests. Every entry is printed as it fires,
 * so a run log says what was pressed when.
 *
 * RECOMP_PAD_LIVE=<file> is the same thing driven while the title runs: each
 * line appended to the file -- <button>[+<button>...][:<hold ms>], no time --
 * is pressed as soon as it is read. With RECOMP_FB_DUMP and a short
 * RECOMP_PB_REPORT_MS that makes a loop: look at the frame, append a press,
 * look again. A fixed timeline cannot do that on a rasterised run, where
 * how long a screen takes to appear depends on how much the software
 * rasteriser has to draw.
 *
 * Like RECOMP_PAD_PRESS this is a bring-up tool, off unless set. */
/* Long soak runs (10 min of looped input), and whole recorded play sessions with analog sticks: a 6 min
 * DualSense session of Panzer Dragoon Orta is ~10000 states after dropping stick noise (2026-11-03).
 * ~80 bytes a step; every lookup is a linear walk, ~10k steps a pad read is still nothing. */
#define PAD_SCRIPT_MAX 32768

/* Time bases of a step (2026-11-01, for recordings - xinput_record.c): "<n>:" milliseconds from the first
 * pad read (the original), "r<n>:" pad reads of that port by the title from its first read (independent of load
 * times for a title that polls once per frame and not while loading), "f<n>:" guest vblanks from the first read.
 * The hold is in the same unit. Values: "lx=<int16>" ly rx ry set a stick exactly, "a=<0-255>" (any analog button
 * name) an analog button's value; "a" alone is fully pressed, as before. */
enum { STEP_MS, STEP_READS, STEP_FRAMES };

/* The clock of the "f" base, for the script and the recorder alike. Guest vblanks by default. With
 * RECOMP_PAD_GAME_FRAME=<hex guest VA> it is a 32-bit counter of the title's own that counts its frames instead: a
 * vblank clock runs on through a hitch while the game stands still, so a long replay drifts away from the run it
 * was recorded in (Panzer Dragoon Orta's first level, recorded and replayed in vblanks: game over halfway, 
 * 2026-11-03). Orta: D3D's presented-frame count. Read without a lock: an aligned 32-bit load. */
uint64_t xbox_InputFrameClock(void)
{
    extern uint64_t xbox_GuestVblankCount(void);
    extern ptrdiff_t xbox_GetMemoryOffset(void);
    static int init;
    static uint32_t va;
    if (!init) {
        const char *e = getenv("RECOMP_PAD_GAME_FRAME");
        va = e && *e ? (uint32_t)strtoul(e, NULL, 16) & ~3u : 0;
        init = 1;
        if (va)
            fprintf(stderr, "  PAD: frame base \"f\" = the title's counter at 0x%08X (RECOMP_PAD_GAME_FRAME)\n", va);
    }
    if (va)
        return *(volatile uint32_t *)((uintptr_t)xbox_GetMemoryOffset() + va);
    return xbox_GuestVblankCount();
}

typedef struct {
    unsigned long at_ms, hold_ms;   /* in the step's base (at_ms/hold_ms keep their names for the ms scripts) */
    uint8_t base;            /* STEP_MS / STEP_READS / STEP_FRAMES */
    uint8_t digital;         /* report byte 2 */
    uint8_t analog;          /* bit n = report byte 4 + n */
    uint8_t aval[8];         /* its value when set by "name=<v>" (0 = full, 0xFF) */
    uint8_t sx_mask;         /* bit n: sx[n] set by lx/ly/rx/ry=<v> */
    int16_t sx[4];           /* LX LY RX RY */
    uint8_t pad;             /* which controller: "p2-a" is pad 1 */
    uint8_t stick;           /* full deflection: 1 L-left 2 L-right 4 L-up
                                8 L-down, then the same four for the right */
    int announced;
    char shot[32];           /* "shot[=name]": a screenshot, no buttons */
    uint16_t key;            /* "key=<HID usage ID>": a physical key held (through
                                the keyboard's binding, xinput_bind.h) */
    unsigned long period;    /* "<at>/<period>-<until>:...": pressed for `hold` every
                                `period` from `at` up to `until` (0 = a single step) */
    unsigned long until;
} PadStep;

/* 1 while step st is held at time ts (its own base) */
static int step_active(const PadStep *st, unsigned long ts)
{
    if (st->period) {
        if (ts < st->at_ms || ts >= st->until)
            return 0;
        return (ts - st->at_ms) % st->period < st->hold_ms;
    }
    return ts >= st->at_ms && ts < st->at_ms + st->hold_ms;
}

static PadStep s_script[PAD_SCRIPT_MAX];
static int     s_script_len = -1;
static int     s_has_keys;   /* the script holds key steps */

/* Pause switch (2026-11-08; maintainer: the script must be switchable off so the
 * player can step into gameplay directly): while paused no script or live step is pressed - the real pad plays alone. Repeating
 * steps keep their clock, so they go on in phase after "resume"; single steps that fall into the pause are skipped.
 * Toggled by holding BACK on the real pad of port 1 for PAD_PAUSE_HOLD_MS (only while a script or a live file is
 * set), or by a live-file line "pause" / "resume" / "toggle". xbox_InputScriptPaused() lets a port's own helper
 * threads (e.g. a cheat that taps keys) stop too. */
#define PAD_PAUSE_HOLD_MS 1500
static volatile int s_paused;

int xbox_InputScriptPaused(void)
{
    return s_paused;
}

static void pad_pause_set(int on, const char *why)
{
    if (s_paused == on)
        return;
    s_paused = on;
    fprintf(stderr, "  PAD: script %s (%s)\n", on ? "PAUSED - the real pad plays alone" : "RESUMED", why);
    fflush(stderr);
}

static int same_word(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
    return 1;
}

static int pad_button_bits(const char *name, size_t n, uint8_t *dig,
                           uint8_t *ana, uint8_t *stick)
{
    /* Stick directions, for screens and games that read the stick and
     * ignore the d-pad: Burnout 3 steers and picks map locations with it. */
    static const struct { const char *name; uint8_t bit; } sk[] = {
        {"lleft", 0x01}, {"lright", 0x02}, {"lup", 0x04}, {"ldown", 0x08},
        {"rleft", 0x10}, {"rright", 0x20}, {"rup", 0x40}, {"rdown", 0x80},
    };
    static const struct { const char *name; uint8_t dig, ana; } k[] = {
        {"up", 0x01, 0}, {"down", 0x02, 0}, {"left", 0x04, 0},
        {"right", 0x08, 0}, {"start", 0x10, 0}, {"back", 0x20, 0},
        {"lthumb", 0x40, 0}, {"rthumb", 0x80, 0},
        {"a", 0, 0x01}, {"b", 0, 0x02}, {"x", 0, 0x04}, {"y", 0, 0x08},
        {"black", 0, 0x10}, {"white", 0, 0x20}, {"lt", 0, 0x40}, {"rt", 0, 0x80},
    };
    size_t i;
    if (n == 7 && same_word("connect", name, n))
        return 1;
    for (i = 0; i < sizeof sk / sizeof sk[0]; i++) {
        if (strlen(sk[i].name) == n && same_word(sk[i].name, name, n)) {
            *stick |= sk[i].bit;
            return 1;
        }
    }
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        if (strlen(k[i].name) == n && same_word(k[i].name, name, n)) {
            *dig |= k[i].dig;
            *ana |= k[i].ana;
            return 1;
        }
    }
    return 0;
}

static void pad_script_file(const char *path, unsigned long base, int depth);

static void pad_script_parse(const char *text, unsigned long base, const char *dir, int depth)
{
    const char *p = text;

    while (*p && s_script_len < PAD_SCRIPT_MAX) {
        PadStep st;
        char *end;
        const char *b;

        while (*p == ',' || *p == ' ' || *p == '\r' || *p == '\n' || *p == '\t')
            p++;
        if (*p == '#') {             /* comment to the end of the line */
            while (*p && *p != '\n')
                p++;
            continue;
        }
        if (!*p)
            break;
        if (!strncmp(p, "@include", 8) && (p[8] == ' ' || p[8] == '\t')) {
            char name[260], path[600];
            size_t n;
            unsigned long at;
            p += 8;
            while (*p == ' ' || *p == '\t')
                p++;
            n = strcspn(p, " \t\r\n");
            if (n >= sizeof name)
                n = sizeof name - 1;
            memcpy(name, p, n);
            name[n] = 0;
            p += n;
            at = strtoul(p, &end, 0);
            p = end;
            if (name[0] == '/' || name[0] == '\\' || (name[0] && name[1] == ':') || !dir || !*dir)
                snprintf(path, sizeof path, "%s", name);
            else
                snprintf(path, sizeof path, "%s/%s", dir, name);
            if (depth < 4)
                pad_script_file(path, base + at, depth + 1);
            else
                fprintf(stderr, "  PAD: @include nested too deep: %s\n", path);
            continue;
        }
        memset(&st, 0, sizeof st);
        st.hold_ms = 150;
        if ((p[0] == 'r' || p[0] == 'R' || p[0] == 'f' || p[0] == 'F') && p[1] >= '0' && p[1] <= '9') {
            st.base = (p[0] == 'r' || p[0] == 'R') ? STEP_READS : STEP_FRAMES;
            st.hold_ms = 9;                                  /* ~150 ms at 60 per second */
            p++;
        }
        st.at_ms = (st.base == STEP_MS ? base : 0) + strtoul(p, &end, 0);
        if (end != p && *end == '/') {
            /* a repeating step "<at>/<period>-<until>" (2026-11-08: a 10 h autoplay was 180,000 single
             * steps - over PAD_SCRIPT_MAX and 2.8 MB of text) */
            const char *q = end + 1;
            st.period = strtoul(q, &end, 0);
            if (end != q && *end == '-') {
                q = end + 1;
                st.until = (st.base == STEP_MS ? base : 0) + strtoul(q, &end, 0);
            } else
                st.until = (unsigned long)-1;
            if (!st.period) {
                fprintf(stderr, "  PAD: repeating step with period 0 at \"%.20s\"\n", p);
                break;
            }
        }
        if (end == p || *end != ':') {
            fprintf(stderr, "  PAD: script entry without <ms>: at \"%.20s\"\n", p);
            break;
        }
        b = end + 1;
        /* "p2-" .. "p4-": the step is for that controller, not the first. */
        if ((b[0] == 'p' || b[0] == 'P') && b[1] >= '1' && b[1] <= '4'
            && b[2] == '-') {
            st.pad = (uint8_t)(b[1] - '1');
            b += 3;
        }
        for (;;) {
            size_t n = strcspn(b, "+:,\r\n ");
            if (n >= 4 && same_word(b, "shot", 4) && (n == 4 || b[4] == '=')) {
                if (n > 5)
                    snprintf(st.shot, sizeof st.shot, "%.*s", (int)(n - 5), b + 5);
                else
                    snprintf(st.shot, sizeof st.shot, "%05lu", st.at_ms);
            } else if (n > 3 && memchr(b, '=', n) && !same_word(b, "key=", 4) && !same_word(b, "shot", 4)) {
                /* "lx=-12000" (a stick exactly) or "a=128" (an analog button's value) */
                static const char *const sticks[4] = {"lx", "ly", "rx", "ry"};
                static const char *const anas[8] = {"a", "b", "x", "y", "black", "white", "lt", "rt"};
                size_t k = (size_t)((const char *)memchr(b, '=', n) - b);
                long v = strtol(b + k + 1, NULL, 10);
                int j, done = 0;
                for (j = 0; j < 4 && !done; j++)
                    if (k == 2 && same_word(b, sticks[j], 2)) {
                        st.sx[j] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
                        st.sx_mask |= (uint8_t)(1u << j);
                        done = 1;
                    }
                for (j = 0; j < 8 && !done; j++)
                    if (strlen(anas[j]) == k && same_word(b, anas[j], k)) {
                        if (v > 0) {                         /* 0: not pressed */
                            st.analog |= (uint8_t)(1u << j);
                            st.aval[j] = (uint8_t)(v > 255 ? 255 : v);
                        }
                        done = 1;
                    }
                if (!done)
                    fprintf(stderr, "  PAD: unknown value \"%.*s\"\n", (int)n, b);
            } else if (n > 4 && same_word(b, "key=", 4)) {
                unsigned long k = strtoul(b + 4, NULL, 10);
                if (k > 0 && k < 512) {
                    st.key = (uint16_t)k;
                    s_has_keys = 1;
                }
                else
                    fprintf(stderr, "  PAD: bad key \"%.*s\"\n", (int)n, b);
            } else if (!pad_button_bits(b, n, &st.digital, &st.analog, &st.stick))
                fprintf(stderr, "  PAD: unknown button \"%.*s\"\n", (int)n, b);
            b += n;
            if (*b != '+')
                break;
            b++;
        }
        if (*b == ':')
            st.hold_ms = strtoul(b + 1, (char **)&b, 0);
        s_script[s_script_len++] = st;
        p = b;
    }
}

/* A script file; its directory is where its own @include paths start. */
static void pad_script_file(const char *path, unsigned long base, int depth)
{
    extern FILE *xbox_fopen_utf8(const char *path, const char *mode);
    FILE *f = xbox_fopen_utf8(path, "rb");
    char *buf, dir[600];
    const char *slash;
    size_t n;
    if (!f) {
        fprintf(stderr, "  PAD: cannot open script %s\n", path);
        return;
    }
    {
        /* the whole file: it was the first 64 KB only, so a long script stopped mid-line - a 10 h autoplay of
         * Panzer Dragoon Orta ended after 983 s, "script entry without <ms>" (2026-11-08) */
        long sz;
        if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0)
            sz = 65535;
        buf = (char *)malloc((size_t)sz + 1);
        if (!buf) {
            fclose(f);
            return;
        }
        n = fread(buf, 1, (size_t)sz, f);
    }
    fclose(f);
    buf[n] = 0;
    slash = strrchr(path, '/');
    {
        const char *bs = strrchr(path, '\\');
        if (!slash || (bs && bs > slash))
            slash = bs;
    }
    if (slash && (size_t)(slash - path) < sizeof dir) {
        memcpy(dir, path, (size_t)(slash - path));
        dir[slash - path] = 0;
    } else {
        dir[0] = 0;
    }
    pad_script_parse(buf, base, dir, depth);
    free(buf);
}

static void pad_script_load(void)
{
    extern const char *xbox_getenv_utf8(const char *name);
    extern FILE *xbox_fopen_utf8(const char *path, const char *mode);
    const char *spec = xbox_getenv_utf8("RECOMP_PAD_SCRIPT");

    s_script_len = 0;
    if (!spec || !*spec)
        return;
    if (spec[0] == '@')
        pad_script_file(spec + 1, 0, 0);
    else
        pad_script_parse(spec, 0, NULL, 0);
    fprintf(stderr, "  PAD: script of %d step(s)\n", s_script_len);
    fflush(stderr);
}

/* Drop steps that have finished, so a long live session never fills up. */
static void pad_script_compact(unsigned long t)
{
    int i, n = 0;
    for (i = 0; i < s_script_len; i++) {
        const PadStep *st = &s_script[i];
        /* only millisecond steps can be judged by t; repeating ones end at `until` */
        int done = st->base == STEP_MS && (st->period ? t >= st->until : t >= st->at_ms + st->hold_ms);
        if (!done)
            s_script[n++] = *st;
    }
    s_script_len = n;
}

/* Read whatever has been appended to RECOMP_PAD_LIVE since last time, and
 * schedule each complete line at t. Polled at most every 50 ms. */
static void pad_live_poll(unsigned long t)
{
    static const char *path;
    static int checked;
    static long consumed;
    static unsigned long last_poll;
    static char pending[512];
    static size_t npending;
    static unsigned long next_free[USB_GAMEPAD_MAX];
    FILE *f;
    int c;

    if (!checked) {
        checked = 1;
        extern const char *xbox_getenv_utf8(const char *name);
        path = xbox_getenv_utf8("RECOMP_PAD_LIVE");
        if (path && *path) {
            fprintf(stderr, "  PAD: live input from %s\n", path);
            fflush(stderr);
        }
    }
    if (!path || !*path || (t - last_poll < 50 && last_poll))
        return;
    last_poll = t;
    {
        extern FILE *xbox_fopen_utf8(const char *path, const char *mode);
        f = xbox_fopen_utf8(path, "rb");
    }
    if (!f)
        return;
    if (fseek(f, consumed, SEEK_SET) != 0) {
        fclose(f);
        return;
    }
    while ((c = fgetc(f)) != EOF) {
        consumed++;
        if (c == '\n' || c == '\r') {
            if (npending) {
                char line[560];
                /* Several lines in one read go one after another, with a
                 * 200 ms release between: a title polling its pad a few times
                 * a second would otherwise see "down" and "a" in the same
                 * report, which is a different input from "down, then a". */
                /* Serialised per pad: pad 2's throttle must not wait out
                 * pad 1's -- split-screen players press at the same time. */
                int pd = (pending[0] == 'p' || pending[0] == 'P')
                         && pending[1] >= '1' && pending[1] <= '4'
                         && pending[2] == '-' ? pending[1] - '1' : 0;
                unsigned long at = t > next_free[pd] ? t : next_free[pd];
                pending[npending] = 0;
                if (same_word(pending, "pause", 6) || same_word(pending, "resume", 7)
                    || same_word(pending, "toggle", 7)) {
                    pad_pause_set(pending[0] == 'p' || pending[0] == 'P' ? 1
                                  : pending[0] == 'r' || pending[0] == 'R' ? 0 : !s_paused, "live file");
                    npending = 0;
                    continue;
                }
                snprintf(line, sizeof line, "%lu:%s", at, pending);
                if (s_script_len >= PAD_SCRIPT_MAX - 1)
                    pad_script_compact(t);
                pad_script_parse(line, 0, NULL, 0);
                if (s_script_len > 0)
                    next_free[pd] = at + s_script[s_script_len - 1].hold_ms + 200;
                fprintf(stderr, "  PAD: live \"%s\" at t=%lu ms\n", pending, at);
                {
                    /* RECOMP_EVENT_TS: the press on the [EVT] clock (a throttled press starts at - t later) */
                    extern int xbox_event_ms(double *ms);
                    double ms;
                    if (xbox_event_ms(&ms))
                        fprintf(stderr, "  [EVT] %.3f PAD %s\n", ms + (double)(at - t), pending);
                }
                fflush(stderr);
                npending = 0;
            }
        } else if (npending < sizeof pending - 1) {
            pending[npending++] = (char)c;
        }
    }
    fclose(f);
}

/* Ports the script drives ("p2-a" is port 1), so a scripted second player is
 * a connected controller without a pad on the desk. */
static unsigned s_script_ports;

static void script_ports_update(void)
{
    int i;
    for (i = 0; i < s_script_len; i++)
        s_script_ports |= 1u << s_script[i].pad;
}

static unsigned long s_t0;   /* the script's clock: the first pad read */

/* The read clock ("r<n>:" steps, the recorder's r base). By default every xbox_InputGetState of a port is one read -
 * exact for titles on the XInput HLE (xdk_xinput), where that IS the title's read. Under the USB pad model the
 * OHCI controller fetches reports on its own schedule, also during loads, and XAPI's XInputGetState only reads its
 * buffer in guest code; a port that wants the title's own reads calls xbox_InputTitleRead(port) from its override of
 * XInputGetState, and from the first such call that port's clock counts only those. Runtime samples (movie skip:
 * xbox_input_host_read) never count. */
static unsigned long s_reads[USB_GAMEPAD_MAX], s_read_now[USB_GAMEPAD_MAX];
static int s_title_ticks[USB_GAMEPAD_MAX];

void xbox_InputTitleRead(int port)
{
    if (port >= 0 && port < USB_GAMEPAD_MAX) {
        s_title_ticks[port] = 1;
        s_reads[port]++;
    }
}

unsigned long xbox_InputReadNow(int port)
{
    return (port >= 0 && port < USB_GAMEPAD_MAX) ? s_read_now[port] : 0;
}

/* Once per xbox_InputGetState, before the script and the recorder look at it. */
static void read_clock_step(int port)
{
    if (port < 0 || port >= USB_GAMEPAD_MAX)
        return;
    s_read_now[port] = s_reads[port];
    if (!s_title_ticks[port] && !xbox_input_host_read)
        s_reads[port]++;
}

int xbox_InputScriptKeyDown(int scancode)
{
    unsigned long t;
    int i;

    if (!s_has_keys || s_script_len <= 0 || !s_t0 || s_paused)
        return 0;
    t = (unsigned long)GetTickCount() - s_t0;
    for (i = 0; i < s_script_len; i++) {
        PadStep *st = &s_script[i];
        if (st->key == scancode && step_active(st, t)) {
            if (!st->announced) {
                st->announced = 1;
                fprintf(stderr, "  PAD: t=%lu ms step %d (key %u)\n", t, i, (unsigned)st->key);
                fflush(stderr);
            }
            return 1;
        }
    }
    return 0;
}

static void pad_script_apply(int pad, XBOX_GAMEPAD *g)
{
    static uint64_t f0;
    unsigned long now, t, tb[3];
    int i, j;

    if (s_script_len < 0)
        pad_script_load();
    now = (unsigned long)GetTickCount();
    if (!s_t0) {
        s_t0 = now;
        f0 = xbox_InputFrameClock();
    }
    t = now - s_t0;
    tb[STEP_MS] = t;
    read_clock_step(pad);
    tb[STEP_READS] = xbox_InputReadNow(pad);
    tb[STEP_FRAMES] = (unsigned long)(xbox_InputFrameClock() - f0);
    pad_live_poll(t);
    script_ports_update();
    /* The pause switch: BACK held on the real pad (g holds only the host pad here, the script is ORed in below) */
    if (pad == 0 && s_script_len >= 0) {
        static unsigned long back_since;
        static int back_done;
        if (g->wButtons & 0x20) {
            if (!back_since)
                back_since = t ? t : 1;
            else if (!back_done && t - back_since >= PAD_PAUSE_HOLD_MS) {
                back_done = 1;
                pad_pause_set(!s_paused, "BACK held");
            }
        } else {
            back_since = 0;
            back_done = 0;
        }
    }
    if (s_paused)
        return;
    for (i = 0; i < s_script_len; i++) {
        PadStep *st = &s_script[i];
        unsigned long ts = tb[st->base];
        if (st->pad != pad || st->key)   /* key steps: xbox_InputScriptKeyDown */
            continue;
        if (!step_active(st, ts))
            continue;
        if (!st->announced) {
            st->announced = 1;
            fprintf(stderr, "  PAD: t=%lu ms step %d (digital 0x%02X analog 0x%02X)%s%s\n",
                    t, i, st->digital, st->analog, st->shot[0] ? " shot " : "", st->shot);
            fflush(stderr);
            if (st->shot[0]) {
                extern void (*g_xbox_screenshot_hook)(const char *name);
                if (g_xbox_screenshot_hook)
                    g_xbox_screenshot_hook(st->shot);
            }
        }
        g->wButtons |= st->digital;
        for (j = 0; j < 8; j++)
            if (st->analog & (1u << j))
                g->bAnalogButtons[j] = st->aval[j] ? st->aval[j] : 0xFF;
        /* Exact stick values (recordings). */
        if (st->sx_mask & 1) g->sThumbLX = st->sx[0];
        if (st->sx_mask & 2) g->sThumbLY = st->sx[1];
        if (st->sx_mask & 4) g->sThumbRX = st->sx[2];
        if (st->sx_mask & 8) g->sThumbRY = st->sx[3];
        /* Sticks at full throw, up positive. */
        if (st->stick & 0x01) g->sThumbLX = -32767;
        if (st->stick & 0x02) g->sThumbLX = 32767;
        if (st->stick & 0x04) g->sThumbLY = 32767;
        if (st->stick & 0x08) g->sThumbLY = -32767;
        if (st->stick & 0x10) g->sThumbRX = -32767;
        if (st->stick & 0x20) g->sThumbRX = 32767;
        if (st->stick & 0x40) g->sThumbRY = 32767;
        if (st->stick & 0x80) g->sThumbRY = -32767;
    }
}

void xbox_InputScriptApply(int port, XBOX_GAMEPAD *g)
{
    if (port == 0)
        g->wButtons |= synthetic_buttons();
    pad_script_apply(port, g);
}

unsigned xbox_InputScriptPorts(void)
{
    if (s_script_len < 0)
        pad_script_load();
    script_ports_update();
    return s_script_ports;
}
