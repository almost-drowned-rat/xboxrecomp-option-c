/*
 * log_async.c - the logger thread behind log_async.h.
 *
 * A bounded multi-producer ring (D. Vyukov's sequence-numbered slots): a
 * producer claims a slot with one compare-exchange, formats into it and
 * publishes it by bumping the slot's sequence; the single consumer -- the
 * logger thread, or xbox_log_drain on whatever thread calls it -- writes the
 * published slots in order. A producer never waits: when the ring is full the
 * line is dropped and counted, and the logger says how many it lost.
 *
 * Lines from one thread keep their order. Lines from different threads are
 * ordered by when they claimed a slot, as the CRT's stream lock ordered them.
 */
#define XBOX_LOG_ASYNC_NO_REDIRECT
#include "log_async.h"
#undef fprintf
#undef vfprintf
#undef printf
#undef fputs
#undef puts
#undef fputc
#undef putc
#undef putchar
#undef fwrite
#undef fflush

#include "platform/xbox_winnt.h"
#include <string.h>

#define LOG_SLOTS 2048                  /* power of two */
#define LOG_TEXT  1024                  /* a longer line is cut, and says so */

typedef struct {
    volatile LONG seq;
    FILE *stream;
    int len;
    char text[LOG_TEXT];
} LogSlot;

static LogSlot s_ring[LOG_SLOTS];
static volatile LONG s_tail;            /* next slot to claim */
static LONG s_head;                     /* next slot to write; owned by whoever holds s_consumer */
static volatile LONG s_consumer;        /* 1 while someone writes slots out */
static volatile LONG s_dropped, s_dropped_reported;
static volatile LONG s_started, s_idle;
static HANDLE s_wake;

static int sync_mode(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_LOG_SYNC");
        on = e && *e == '1';
    }
    return on;
}

static int is_console(FILE *stream)
{
    return stream == stderr || stream == stdout;
}

/* Sequence arithmetic on the wrapping counters, done unsigned. */
static LONG seq_diff(LONG a, LONG b)
{
    return (LONG)((ULONG)a - (ULONG)b);
}

static int head_published(void)
{
    LogSlot *s = &s_ring[s_head & (LOG_SLOTS - 1)];
    return seq_diff(InterlockedCompareExchange(&s->seq, 0, 0), s_head) == 1;
}

/* Write out every published slot. Returns the number written; -1 when another
 * thread is already doing it. */
static int consume(void)
{
    int n = 0;
    FILE *used = NULL;
    LONG dropped;

    if (InterlockedCompareExchange(&s_consumer, 1, 0) != 0)
        return -1;
    for (;;) {
        LogSlot *s = &s_ring[s_head & (LOG_SLOTS - 1)];
        if (!head_published())
            break;                      /* empty, or claimed and not yet published */
        fwrite(s->text, 1, (size_t)s->len, s->stream);
        if (used && used != s->stream)
            fflush(used);
        used = s->stream;
        InterlockedExchange(&s->seq, (LONG)((ULONG)s_head + LOG_SLOTS));
        s_head = (LONG)((ULONG)s_head + 1);
        n++;
    }
    dropped = InterlockedCompareExchange(&s_dropped, 0, 0);
    if (dropped != s_dropped_reported) {
        fprintf(stderr, "  [LOG] %ld line(s) dropped: the log ring was full\n",
                (long)(dropped - s_dropped_reported));
        s_dropped_reported = dropped;
        used = stderr;
    }
    if (used)
        fflush(used);
    InterlockedExchange(&s_consumer, 0);
    return n;
}

static DWORD WINAPI logger_thread(LPVOID unused)
{
    (void)unused;
    for (;;) {
        consume();
        InterlockedExchange(&s_idle, 1);
        /* A line published between the drain and the flag has no wake-up
         * coming; the timeout bounds it. */
        WaitForSingleObject(s_wake, 20);
        InterlockedExchange(&s_idle, 0);
    }
    return 0;
}

static void start(void)
{
    if (InterlockedCompareExchange(&s_started, 1, 0) != 0)
        return;
    s_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    atexit(xbox_log_drain);
    CloseHandle(CreateThread(NULL, 0, logger_thread, NULL, 0, NULL));
}

/* Claim a slot; NULL when the ring is full (counted as dropped). */
static LogSlot *claim(LONG *pos_out)
{
    for (;;) {
        LONG pos = InterlockedCompareExchange(&s_tail, 0, 0);
        LogSlot *s = &s_ring[pos & (LOG_SLOTS - 1)];
        LONG d = seq_diff(InterlockedCompareExchange(&s->seq, 0, 0), pos);
        if (d == 0) {
            if (InterlockedCompareExchange(&s_tail, (LONG)((ULONG)pos + 1), pos) == pos) {
                *pos_out = pos;
                return s;
            }
        } else if (d < 0) {
            InterlockedIncrement(&s_dropped);
            return NULL;
        }
        /* d > 0: another producer took it; reload the tail */
    }
}

static void publish(LogSlot *s, LONG pos)
{
    InterlockedExchange(&s->seq, (LONG)((ULONG)pos + 1));
    if (InterlockedCompareExchange(&s_idle, 0, 0))
        SetEvent(s_wake);
}

static void ring_init_once(void)
{
    static volatile LONG done;
    LONG i;
    if (InterlockedCompareExchange(&done, 0, 0) == 2)
        return;
    if (InterlockedCompareExchange(&done, 1, 0) == 0) {
        for (i = 0; i < LOG_SLOTS; i++)
            s_ring[i].seq = i;
        InterlockedExchange(&done, 2);
    } else {
        while (InterlockedCompareExchange(&done, 0, 0) != 2)
            ;
    }
    start();
}

/* Queue len bytes of text for stream. Text longer than a slot is cut. */
static void queue_text(FILE *stream, const char *text, size_t len)
{
    LONG pos;
    LogSlot *s;

    ring_init_once();
    s = claim(&pos);
    if (!s)
        return;
    if (len > LOG_TEXT - 1)
        len = LOG_TEXT - 1;
    memcpy(s->text, text, len);
    s->len = (int)len;
    s->stream = stream;
    publish(s, pos);
}

int xbox_log_vfprintf(FILE *stream, const char *fmt, va_list ap)
{
    LONG pos;
    LogSlot *s;
    int n;
    va_list cp;

    if (!is_console(stream) || sync_mode())
        return vfprintf(stream, fmt, ap);
    ring_init_once();
    s = claim(&pos);
    if (!s)
        return 0;
    va_copy(cp, ap);
    n = vsnprintf(s->text, LOG_TEXT, fmt, cp);
    va_end(cp);
    if (n < 0)
        n = 0;
    if (n >= LOG_TEXT) {                /* cut: keep the line a line */
        static const char cut[] = " [...]\n";
        memcpy(s->text + LOG_TEXT - sizeof cut, cut, sizeof cut);
        s->len = LOG_TEXT - 1;
    } else {
        s->len = n;
    }
    s->stream = stream;
    publish(s, pos);
    return n;
}

int xbox_log_fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = xbox_log_vfprintf(stream, fmt, ap);
    va_end(ap);
    return n;
}

int xbox_log_printf(const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = xbox_log_vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int xbox_log_fputs(const char *str, FILE *stream)
{
    if (!is_console(stream) || sync_mode())
        return fputs(str, stream);
    queue_text(stream, str, strlen(str));
    return 1;
}

int xbox_log_puts(const char *str)
{
    size_t len = strlen(str);
    char line[LOG_TEXT];
    if (sync_mode())
        return puts(str);
    if (len > LOG_TEXT - 2)
        len = LOG_TEXT - 2;
    memcpy(line, str, len);
    line[len] = '\n';
    queue_text(stdout, line, len + 1);
    return 1;
}

int xbox_log_fputc(int c, FILE *stream)
{
    char ch = (char)c;
    if (!is_console(stream) || sync_mode())
        return fputc(c, stream);
    queue_text(stream, &ch, 1);
    return (unsigned char)ch;
}

int xbox_log_putchar(int c)
{
    return xbox_log_fputc(c, stdout);
}

size_t xbox_log_fwrite(const void *p, size_t size, size_t n, FILE *stream)
{
    if (!is_console(stream) || sync_mode())
        return fwrite(p, size, n, stream);
    queue_text(stream, (const char *)p, size * n);
    return n;
}

int xbox_log_fflush(FILE *stream)
{
    /* The console is flushed by the logger after each batch. */
    if (stream && is_console(stream) && !sync_mode())
        return 0;
    return fflush(stream);
}

void xbox_log_drain(void)
{
    /* The logger thread may be writing a batch: wait for it, then take over.
     * Bounded (2 s), so a logger stopped mid-batch cannot hang the exit. A
     * line claimed but never published (its thread killed) ends the drain. */
    int spins;
    if (!InterlockedCompareExchange(&s_started, 0, 0))
        return;
    for (spins = 0; spins < 2000; spins++) {
        if (consume() >= 0 && !head_published())
            return;
        Sleep(1);
    }
}

unsigned long xbox_log_dropped(void)
{
    return (unsigned long)InterlockedCompareExchange(&s_dropped, 0, 0);
}
