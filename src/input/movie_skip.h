/*
 * movie_skip.h -- skippable movies (maintainer rule, 2026-10-16).
 *
 * Player skip: Y pressed twice within XBOX_MOVIE_SKIP_WINDOW_MS on any port (keyboard: the key mapped to Y) asks to
 * skip the current movie. The double press avoids accidental skips; the request stays valid for
 * XBOX_MOVIE_SKIP_HOLD_MS only, so a double Y in a menu does not end the next movie that starts later.
 * Test/agent skip: RECOMP_SKIP_MOVIES=1 skips every movie (input scripts, soak tests, CI). Default off.
 *
 * The toolkit only decides *whether* to skip. A port hooks its title's movie loop (weak override or instruction
 * hook), asks xbox_MovieSkipPending() once per movie frame and, if it says so, ends playback exactly like a natural
 * end (same result codes, callbacks and state), then calls xbox_MovieSkipConsume(). See docs/porting_guide.md.
 */
#ifndef XBOX_MOVIE_SKIP_H
#define XBOX_MOVIE_SKIP_H

#ifdef __cplusplus
extern "C" {
#endif

#define XBOX_MOVIE_SKIP_WINDOW_MS 1000u   /* second Y press within this time of the first */
#define XBOX_MOVIE_SKIP_HOLD_MS    500u   /* a request expires if no movie takes it */
#define XBOX_MOVIE_SKIP_MIN_GAP_MS  60u   /* presses closer than this are one press seen twice */

/* Y state of one port, from xbox_InputGetState (host pad, keyboard and pad script merged). */
void xbox_MovieSkipFeed(int port, int y_down);

/* Ask for a skip from code (a menu option, a debug key). Valid like a double press. */
void xbox_MovieSkipRequest(void);

/* 1: end the current movie now. Polls the pads itself, so it works while the title reads no input during movies.
 * RECOMP_SKIP_MOVIES=1 makes it 1 always. Call once per movie frame from the port's hook. */
int xbox_MovieSkipPending(void);

/* The port ended a movie because of a skip: clears the request and counts it (log line "[MOVIE] skipped ..."). */
void xbox_MovieSkipConsume(const char *what);

/* RECOMP_SKIP_MOVIES=1 (read once). */
int xbox_MovieSkipAll(void);

/* Tests: replace the millisecond clock (default SDL_GetTicks). */
void xbox_MovieSkipSetClock(unsigned long long (*clock_ms)(void));

#ifdef __cplusplus
}
#endif

#endif
