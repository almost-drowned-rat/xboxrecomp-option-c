/*
 * The pad recorder and the script's new syntax (2026-11-01): a play session becomes a pad script.
 *
 * A script in the read base with exact values is played through xbox_InputGetState - the call every title read
 * goes through - while RECOMP_PAD_RECORD records it. Checked:
 *   1. "r<n>:" steps fire on the title's n-th read of that port, held for <hold> reads;
 *   2. "a=128" sets an analog button's value, "lx=-12000" a stick exactly;
 *   3. reads the runtime makes for itself (movie skip: xbox_input_host_read) neither move the read clock nor record;
 *   4. the recording holds exactly the script's held states, in its syntax, so it replays as is.
 */
#include "xinput_xbox.h"
#include "xbox_memory_layout.h"   /* recomp_func_t */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Provided by the generated title; this harness has none. */
recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { (void)xbox_va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

static int failures;

static void check(int ok, const char *what, const char *detail)
{
    printf("  %-64s %s", what, ok ? "PASS" : "FAIL");
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
    char tmp[MAX_PATH], rec[MAX_PATH], d[256], text[4096];
    XBOX_GAMEPAD g;
    FILE *f;
    size_t n;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    GetTempPathA(MAX_PATH, tmp);
    snprintf(rec, sizeof rec, "%spad_record_test_%lu.txt", tmp, (unsigned long)GetCurrentProcessId());
    /* r6..r7: a resting stick off-centre (the maintainer's pad rests at ~642): inside the dead zone, not recorded */
    _putenv_s("RECOMP_PAD_SCRIPT", "r2:a=128+lx=-12000:3,r7:lx=642+ry=-700:2,r10:start:2");
    _putenv_s("RECOMP_PAD_RECORD", rec);
    _putenv_s("RECOMP_PAD_RECORD_BASE", "r");
    _putenv_s("RECOMP_KEYBOARD", "0");

    for (i = 0; i < 14; i++) {
        if (i == 6) {                                  /* a runtime sample between title reads */
            xbox_input_host_read = 1;
            read_pad();
            xbox_input_host_read = 0;
        }
        g = read_pad();
        if (i >= 2 && i < 5) {
            snprintf(d, sizeof d, "read %d: A=%u LX=%d", i, g.bAnalogButtons[0], g.sThumbLX);
            check(g.bAnalogButtons[0] == 128 && g.sThumbLX == -12000, "r2..r4: A at 128, left stick at -12000", d);
        } else if (i == 7 || i == 8) {
            snprintf(d, sizeof d, "read %d: LX=%d RY=%d", i, g.sThumbLX, g.sThumbRY);
            check(g.sThumbLX == 642 && g.sThumbRY == -700, "r7..r8: resting-stick noise reaches the title as is", d);
        } else if (i == 10 || i == 11) {
            snprintf(d, sizeof d, "read %d: buttons 0x%04X", i, g.wButtons);
            check((g.wButtons & 0x10) != 0, "r10..r11: Start held (the host sample did not count)", d);
        } else {
            snprintf(d, sizeof d, "read %d: buttons 0x%04X A=%u LX=%d", i, g.wButtons, g.bAnalogButtons[0], g.sThumbLX);
            check(!g.wButtons && !g.bAnalogButtons[0] && !g.sThumbLX, "other reads: nothing held", d);
        }
    }

    xbox_InputRecordFlush();
    f = fopen(rec, "rb");
    n = f ? fread(text, 1, sizeof text - 1, f) : 0;
    if (f) fclose(f);
    text[n] = 0;
    check(strstr(text, "r2:p1-a=128+lx=-12000:3") != NULL, "recorded: r2:p1-a=128+lx=-12000:3", text);
    check(strstr(text, "r10:p1-start:2") != NULL, "recorded: r10:p1-start:2", text);
    check(strstr(text, "r7:") == NULL && strstr(text, "=642") == NULL,
          "resting-stick noise inside the dead zone is not recorded", text);
    remove(rec);

    printf("\n%d failure(s)\n", failures);
    fflush(stdout);
    ExitProcess(failures ? 1 : 0);    /* the recorder's writer thread loops for ever */
}
