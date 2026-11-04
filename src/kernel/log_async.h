/*
 * log_async.h - console output from the hot paths, through a logger thread.
 *
 * Standing rule (maintainer, 2026-10-27): no blocking file or log I/O in a
 * kernel call, a DPC, an ISR or anything else that holds the one-CPU gate.
 * A print under the gate waits for the CRT stream lock and for WriteFile, and
 * every guest thread waits behind it: the 2-s kernel summary made Burnout 3's
 * USB DPC run 27-87 ms, and moving it off the path halved the race's p99.
 *
 * The kernel, USB and APU libraries get this header force-included
 * (cmake/hot_path_logging.cmake), so fprintf/printf/fputs/fwrite/fflush on
 * stderr or stdout in those sources format the line on the calling thread and
 * queue it; a logger thread writes it. Nothing blocks: a full ring drops the
 * line and the logger reports the count. Any other stream (a dump file, a
 * save) is passed to the real call unchanged -- writing files is not made
 * asynchronous, it is kept out of those paths (tools/test_hot_path_io.py).
 *
 * xbox_log_drain() writes out whatever is queued, on the calling thread. Call
 * it before ending the process from code that does not return to exit():
 * ExitProcess, TerminateProcess, _exit and abort skip the atexit hook and kill
 * the logger thread. RECOMP_LOG_SYNC=1 makes every call synchronous again
 * (for a crash that has to show its last line).
 */
#ifndef XBOX_LOG_ASYNC_H
#define XBOX_LOG_ASYNC_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -Wformat keeps checking the redirected calls. */
#if defined(__MINGW32__) && defined(__MINGW_PRINTF_FORMAT)
#define XBOX_LOG_FMT(f, a) __attribute__((format(__MINGW_PRINTF_FORMAT, f, a)))
#elif defined(__GNUC__) || defined(__clang__)
#define XBOX_LOG_FMT(f, a) __attribute__((format(printf, f, a)))
#else
#define XBOX_LOG_FMT(f, a)
#endif

int    xbox_log_fprintf(FILE *stream, const char *fmt, ...) XBOX_LOG_FMT(2, 3);
int    xbox_log_vfprintf(FILE *stream, const char *fmt, va_list ap) XBOX_LOG_FMT(2, 0);
int    xbox_log_printf(const char *fmt, ...) XBOX_LOG_FMT(1, 2);
int    xbox_log_fputs(const char *s, FILE *stream);
int    xbox_log_puts(const char *s);
int    xbox_log_fputc(int c, FILE *stream);
int    xbox_log_putchar(int c);
size_t xbox_log_fwrite(const void *p, size_t size, size_t n, FILE *stream);
int    xbox_log_fflush(FILE *stream);
void   xbox_log_drain(void);
unsigned long xbox_log_dropped(void);    /* lines lost to a full ring so far */

#ifdef __cplusplus
}
#endif

/* The redirect. The implementation (log_async.c) undoes it for itself. */
#ifndef XBOX_LOG_ASYNC_NO_REDIRECT
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
#define fprintf(...)  xbox_log_fprintf(__VA_ARGS__)
#define vfprintf(...) xbox_log_vfprintf(__VA_ARGS__)
#define printf(...)   xbox_log_printf(__VA_ARGS__)
#define fputs(...)    xbox_log_fputs(__VA_ARGS__)
#define puts(...)     xbox_log_puts(__VA_ARGS__)
#define fputc(...)    xbox_log_fputc(__VA_ARGS__)
#define putc(...)     xbox_log_fputc(__VA_ARGS__)
#define putchar(...)  xbox_log_putchar(__VA_ARGS__)
#define fwrite(...)   xbox_log_fwrite(__VA_ARGS__)
#define fflush(...)   xbox_log_fflush(__VA_ARGS__)
#endif

#endif /* XBOX_LOG_ASYNC_H */
