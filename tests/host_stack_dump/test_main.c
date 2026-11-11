/* host_stack_dump: a thread spinning in a known function is found, by name, with a pc inside this program.
 * Linux / Android only (elsewhere the dump is a no-op and the test says so and passes). */
#define _GNU_SOURCE
#include "platform/host_stack_dump.h"

#include <stdio.h>
#include <string.h>

#if defined(__linux__) && (defined(__aarch64__) || defined(__x86_64__))
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static volatile int s_stop;

__attribute__((noinline)) static void spin_here(void)
{
    while (!s_stop)
        __asm__ __volatile__("" ::: "memory");
}

static void *spinner(void *u)
{
    (void)u;
    pthread_setname_np(pthread_self(), "hsd_spin");
    spin_here();
    return NULL;
}

static void *sleeper(void *u)
{
    (void)u;
    pthread_setname_np(pthread_self(), "hsd_sleep");
    while (!s_stop) {
        struct timespec ts = { 0, 5000000 };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t a, b;
    char buf[65536], self[256], *line, *save = NULL;
    const char *me;
    size_t got;
    int n, fails = 0, found_spin = 0, found_sleep = 0;
    FILE *f = tmpfile();
    struct timespec ts = { 0, 50000000 };
    (void)argc;
    me = strrchr(argv[0], '/');
    snprintf(self, sizeof self, "%s+0x", me ? me + 1 : argv[0]);

    pthread_create(&a, NULL, spinner, NULL);
    pthread_create(&b, NULL, sleeper, NULL);
    nanosleep(&ts, NULL);
    n = xbox_HostStackDump(f, "test");
    s_stop = 1;
    pthread_join(a, NULL);
    pthread_join(b, NULL);

    rewind(f);
    got = fread(buf, 1, sizeof buf - 1, f);
    buf[got] = 0;
    fclose(f);
    fputs(buf, stdout);

    if (n < 3) {
        printf("FAIL: %d threads listed, want >= 3\n", n);
        fails++;
    }
    for (line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (strstr(line, "\"hsd_spin\"")) {
            found_spin = 1;
            if (strstr(line, "(no answer)") || !strstr(line, self)) {
                printf("FAIL: the spinner's line has no pc in %s\n", self);
                fails++;
            }
            if (!strstr(line, " R cpu ")) {
                printf("FAIL: the spinner is not in state R\n");
                fails++;
            }
        }
        if (strstr(line, "\"hsd_sleep\"")) {
            found_sleep = 1;
            if (strstr(line, "(no answer)")) {
                printf("FAIL: the sleeper did not answer\n");
                fails++;
            }
        }
        if (strstr(line, "(the dumping thread)") == NULL && strstr(line, "tid ") && strstr(line, "(no answer)")) {
            printf("FAIL: a thread did not answer\n");
            fails++;
        }
    }
    if (!found_spin || !found_sleep) {
        printf("FAIL: spinner %d / sleeper %d not listed\n", found_spin, found_sleep);
        fails++;
    }
    printf(fails ? "FAILED: %d\n" : "PASS: host_stack_dump (threads named, pcs in the program, states)\n", fails);
    return fails ? 1 : 0;
}

#else

int main(void)
{
    printf("PASS: host_stack_dump is a no-op on this host (%d)\n", xbox_HostStackDump(stdout, "test"));
    return 0;
}

#endif
