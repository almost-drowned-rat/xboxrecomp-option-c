/*
 * log_async: the logger thread behind src/kernel/log_async.h.
 *
 * stderr goes to a file; four threads log numbered lines through the
 * redirected fprintf, then the test drains and reads the file back:
 *   - every thread's lines arrive in the order it wrote them;
 *   - every line arrives, or is counted by xbox_log_dropped();
 *   - a line longer than a ring slot is cut and still ends in a newline;
 *   - fprintf to an ordinary file is written at once (fclose right after);
 *   - with RECOMP_LOG_SYNC=1 nothing is queued and nothing is dropped.
 */
#include "platform/xbox_winnt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log_async.h"

#define THREADS 4
#define LINES   5000

static int s_fail;
#define CHECK(c, ...) do { if (!(c)) { s_fail = 1; xbox_log_drain(); \
    fprintf(stdout, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stdout, __VA_ARGS__); fputc('\n', stdout); } } while (0)

static DWORD WINAPI producer(LPVOID arg)
{
    int t = (int)(intptr_t)arg, i;
    for (i = 0; i < LINES; i++) {
        fprintf(stderr, "T%d %d\n", t, i);
        fflush(stderr);                 /* a no-op when queued: must not block or reorder */
    }
    return 0;
}

int main(void)
{
    const char *out = "log_async_stderr.txt", *side = "log_async_side.txt";
    int sync = getenv("RECOMP_LOG_SYNC") && getenv("RECOMP_LOG_SYNC")[0] == '1';
    HANDLE th[THREADS];
    int last[THREADS], got = 0, i, cut_ok = 0;
    char line[4096];
    FILE *f;

    if (!freopen(out, "w", stderr)) {
        printf("FAIL: cannot redirect stderr\n");
        return 1;
    }

    /* An ordinary file is not queued: its text is there once fclose returns. */
    f = fopen(side, "w");
    CHECK(f != NULL, "fopen %s", side);
    if (f) {
        fprintf(f, "side %d\n", 42);
        fclose(f);
        f = fopen(side, "r");
        CHECK(f && fgets(line, sizeof line, f) && strcmp(line, "side 42\n") == 0, "side file content");
        if (f)
            fclose(f);
    }

    for (i = 0; i < THREADS; i++)
        th[i] = CreateThread(NULL, 0, producer, (LPVOID)(intptr_t)i, 0, NULL);
    for (i = 0; i < THREADS; i++) {
        WaitForSingleObject(th[i], INFINITE);
        CloseHandle(th[i]);
    }
    /* Drained first: with the queue still full of the producers' lines the long
     * line was dropped (and counted) instead of cut - one line more than the
     * producers wrote, and no cut to check (Linux CI, 2026-10-30). */
    xbox_log_drain();
    {
        char big[3000];
        memset(big, 'x', sizeof big - 2);
        big[sizeof big - 2] = '\n';
        big[sizeof big - 1] = 0;
        fprintf(stderr, "LONG %s", big);
    }
    xbox_log_drain();
    fflush(stderr);
    freopen(out, "r", stderr);          /* close the write side before reading it back */

    f = fopen(out, "r");
    CHECK(f != NULL, "reopen %s", out);
    for (i = 0; i < THREADS; i++)
        last[i] = -1;
    while (f && fgets(line, sizeof line, f)) {
        int t, n;
        if (sscanf(line, "T%d %d", &t, &n) == 2 && t >= 0 && t < THREADS) {
            CHECK(n > last[t], "thread %d: line %d after %d", t, n, last[t]);
            last[t] = n;
            got++;
        } else if (strncmp(line, "LONG ", 5) == 0) {
            size_t len = strlen(line);
            cut_ok = sync ? len == 3004 : (len == 1023 && strstr(line, " [...]\n") == line + len - 7);
        }
        /* "[LOG] n line(s) dropped" lines are counted through xbox_log_dropped below */
    }
    if (f)
        fclose(f);

    printf("%s: %d of %d lines written, %lu dropped\n", sync ? "sync" : "queued", got, THREADS * LINES,
           xbox_log_dropped());
    CHECK((unsigned long)got + xbox_log_dropped() == (unsigned long)THREADS * LINES, "lines lost without a count");
    CHECK(cut_ok, "long line %s", sync ? "not written whole" : "not cut to one slot");
    if (sync)
        CHECK(xbox_log_dropped() == 0, "sync mode dropped lines");
    remove(out);
    remove(side);
    printf("%s\n", s_fail ? "FAILED" : "passed");
    return s_fail;
}
