/*
 * pad_live_stop: a live-file line "stop" drops the script for good, later live lines still press
 * (2026-11-10, for an unattended repro that autoplays and then drives a menu). Checked:
 *   1. the script (a repeating A on every read) is pressed;
 *   2. after "stop" + "b:60000" in the live file: no more A, and B (a live line) is held;
 *   3. still no A on later reads - the script does not come back.
 */
#include "xinput_xbox.h"
#include "xbox_memory_layout.h"   /* recomp_func_t */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { (void)xbox_va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

static int failures;

static void check(int ok, const char *what, const char *detail)
{
    printf("  %-60s %s", what, ok ? "PASS" : "FAIL");
    if (!ok && detail) printf(" -- %s", detail);
    putchar('\n');
    if (!ok) failures++;
}

static XBOX_GAMEPAD read_pad(void)
{
    XBOX_INPUT_STATE s;
    xbox_InputGetState(0, &s);
    return s.Gamepad;
}

int main(void)
{
    char tmp[MAX_PATH], live[MAX_PATH], d[160];
    XBOX_GAMEPAD g;
    FILE *f;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    GetTempPathA(MAX_PATH, tmp);
    snprintf(live, sizeof live, "%spad_live_stop_%lu.txt", tmp, (unsigned long)GetCurrentProcessId());
    f = fopen(live, "wb");
    if (!f) { printf("cannot write %s\n", live); return 2; }
    fclose(f);
    _putenv_s("RECOMP_PAD_SCRIPT", "r0/1-100000:a:1");     /* A on every read: the autoplay's turbo */
    _putenv_s("RECOMP_PAD_LIVE", live);
    _putenv_s("RECOMP_KEYBOARD", "0");

    g = read_pad();
    g = read_pad();
    snprintf(d, sizeof d, "A=%u B=%u", g.bAnalogButtons[0], g.bAnalogButtons[1]);
    check(g.bAnalogButtons[0] != 0, "before stop: the script's A is pressed", d);

    f = fopen(live, "ab");
    fputs("stop\nb:60000\n", f);
    fclose(f);
    Sleep(80);                                           /* the live file is polled every 50 ms */
    g = read_pad();
    for (i = 0; i < 3 && !g.bAnalogButtons[1]; i++) {    /* B comes at the next poll at the latest */
        Sleep(60);
        g = read_pad();
    }
    snprintf(d, sizeof d, "A=%u B=%u", g.bAnalogButtons[0], g.bAnalogButtons[1]);
    check(g.bAnalogButtons[0] == 0, "after stop: no A from the script", d);
    check(g.bAnalogButtons[1] != 0, "after stop: the live line's B is held", d);

    for (i = 0; i < 5; i++)
        g = read_pad();
    snprintf(d, sizeof d, "A=%u B=%u", g.bAnalogButtons[0], g.bAnalogButtons[1]);
    check(g.bAnalogButtons[0] == 0 && g.bAnalogButtons[1] != 0, "later reads: the script stays gone, B still held", d);

    remove(live);
    printf("\n%d failure(s)\n", failures);
    fflush(stdout);
    ExitProcess(failures ? 1 : 0);
}
