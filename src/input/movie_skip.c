/*
 * movie_skip.c -- double Y / RECOMP_SKIP_MOVIES decides when a movie is skipped (see movie_skip.h).
 *
 * Fed from xbox_InputGetState, which every input path goes through (XInput HLE, the USB pad model, the pad script),
 * and polled once more from xbox_MovieSkipPending: some titles read no pad while a movie plays.
 */
#include "movie_skip.h"
#include "xinput_xbox.h"

#ifndef XBOX_MOVIE_SKIP_NO_SDL       /* tests/movie_skip builds this file alone, with its own clock */
#include <SDL3/SDL_timer.h>
#endif
#include <stdio.h>
#include <stdlib.h>

static unsigned long long (*s_clock)(void);
static int s_all = -1;
static unsigned long s_skips;
/* per port: Y held at the last sample, time of the last press (0 = none) */
static int s_down[XBOX_MAX_CONTROLLERS];
static unsigned long long s_last_press[XBOX_MAX_CONTROLLERS];
/* time the current request was made (0 = none); written from input threads, read from the movie thread */
static volatile unsigned long long s_request;

static unsigned long long now_ms(void)
{
    unsigned long long t;
#ifndef XBOX_MOVIE_SKIP_NO_SDL
    t = s_clock ? s_clock() : SDL_GetTicks();
#else
    t = s_clock ? s_clock() : 0;
#endif
    return t ? t : 1;   /* 0 means "none" */
}

void xbox_MovieSkipSetClock(unsigned long long (*clock_ms)(void))
{
    s_clock = clock_ms;
}

int xbox_MovieSkipAll(void)
{
    if (s_all < 0) {
        const char *e = getenv("RECOMP_SKIP_MOVIES");
        s_all = e && e[0] == '1';
        if (s_all)
            fprintf(stderr, "  [MOVIE] RECOMP_SKIP_MOVIES=1: every movie is skipped\n");
    }
    return s_all;
}

void xbox_MovieSkipFeed(int port, int y_down)
{
    unsigned long long t;

    if (port < 0 || port >= XBOX_MAX_CONTROLLERS)
        return;
    y_down = y_down != 0;
    /* Several threads sample the same port (the USB pad model, the title's XInput reads, xbox_MovieSkipPending):
     * the exchange lets exactly one of them see a press. */
    if (__atomic_exchange_n(&s_down[port], y_down, __ATOMIC_ACQ_REL) || !y_down)
        return;                                  /* only a new press (edge) counts */
    t = now_ms();
    /* Two samples taken just apart can still disagree for an instant; a real second press is never that fast. */
    if (s_last_press[port] && t - s_last_press[port] < XBOX_MOVIE_SKIP_MIN_GAP_MS)
        return;
    if (s_last_press[port] && t - s_last_press[port] <= XBOX_MOVIE_SKIP_WINDOW_MS) {
        __atomic_store_n(&s_request, t, __ATOMIC_RELEASE);
        s_last_press[port] = 0;                  /* a third press starts a new pair */
    } else {
        s_last_press[port] = t;
    }
}

/* Set while this file samples the pads for itself (xinput_xbox.h): not a title read. Defined here, where it is
 * set, so the module stands alone (tests/movie_skip). */
_Thread_local int xbox_input_host_read;

void xbox_MovieSkipRequest(void)
{
    __atomic_store_n(&s_request, now_ms(), __ATOMIC_RELEASE);
}

int xbox_MovieSkipPending(void)
{
    unsigned long long r;
    unsigned mask;
    int port;

    if (xbox_MovieSkipAll())
        return 1;
    /* Sample the pads ourselves: the title may not be reading them during the movie. xbox_InputGetState feeds
     * xbox_MovieSkipFeed; a press already seen by the title's own read is not counted twice (edge per port). */
    mask = xbox_InputConnectedMask();
    xbox_input_host_read = 1;     /* not a title read: the pad script's read count and the recorder skip it */
    for (port = 0; port < XBOX_MAX_CONTROLLERS; port++) {
        if ((mask >> port) & 1u) {
            XBOX_INPUT_STATE st;
            xbox_InputGetState((DWORD)port, &st);
        }
    }
    xbox_input_host_read = 0;
    r = __atomic_load_n(&s_request, __ATOMIC_ACQUIRE);
    if (!r)
        return 0;
    if (now_ms() - r > XBOX_MOVIE_SKIP_HOLD_MS) {
        __atomic_store_n(&s_request, 0ull, __ATOMIC_RELEASE);   /* nobody took it */
        return 0;
    }
    return 1;
}

void xbox_MovieSkipConsume(const char *what)
{
    __atomic_store_n(&s_request, 0ull, __ATOMIC_RELEASE);
    s_skips++;
    fprintf(stderr, "  [MOVIE] skipped %s (%s, #%lu)\n", what ? what : "movie",
            xbox_MovieSkipAll() ? "RECOMP_SKIP_MOVIES" : "double Y", s_skips);
    fflush(stderr);
}
