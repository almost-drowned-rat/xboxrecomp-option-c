/* movie_skip.c: double Y within the window asks for a skip, a single press or a slow pair does not, a request
 * expires when no movie takes it, the same press seen by two readers counts once. */
#include "movie_skip.h"
#include "xinput_xbox.h"

#include <stdio.h>
#include <string.h>

static unsigned long long g_now = 1000;
static unsigned long long test_clock(void) { return g_now; }

/* stand-ins for xinput_device.c: the poll in xbox_MovieSkipPending reads no pad here */
unsigned xbox_InputConnectedMask(void) { return 0; }
DWORD xbox_InputGetState(DWORD port, XBOX_INPUT_STATE *s) { (void)port; memset(s, 0, sizeof *s); return 0; }

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

static void press(int port, unsigned long long at, unsigned hold)
{
    g_now = at;
    xbox_MovieSkipFeed(port, 1);
    xbox_MovieSkipFeed(port, 1);            /* a second reader sees the same press */
    g_now = at + hold;
    xbox_MovieSkipFeed(port, 0);
}

int main(void)
{
    xbox_MovieSkipSetClock(test_clock);
    CHECK(!xbox_MovieSkipAll());           /* RECOMP_SKIP_MOVIES unset in the test */

    press(0, 1000, 80);                     /* single press: nothing */
    CHECK(!xbox_MovieSkipPending());

    press(0, 1400, 80);                     /* second press 400 ms later: skip */
    CHECK(xbox_MovieSkipPending());
    xbox_MovieSkipConsume("test");
    CHECK(!xbox_MovieSkipPending());

    press(1, 5000, 80);                     /* pair too slow (1.5 s): nothing */
    press(1, 6500, 80);
    CHECK(!xbox_MovieSkipPending());

    press(2, 10000, 80);                    /* request not taken by a movie expires */
    press(2, 10300, 80);
    g_now = 10300 + 80 + XBOX_MOVIE_SKIP_HOLD_MS + 1;
    CHECK(!xbox_MovieSkipPending());

    press(3, 20000, 80);                    /* flicker: up/down within a few ms is one press */
    g_now = 20010; xbox_MovieSkipFeed(3, 1); g_now = 20020; xbox_MovieSkipFeed(3, 0);
    CHECK(!xbox_MovieSkipPending());

    press(0, 30000, 80);                    /* presses on different ports are not a pair */
    press(1, 30300, 80);
    CHECK(!xbox_MovieSkipPending());

    xbox_MovieSkipRequest();                /* request from code */
    CHECK(xbox_MovieSkipPending());

    printf(g_fail ? "%d failure(s)\n" : "ok\n", g_fail);
    return g_fail != 0;
}
