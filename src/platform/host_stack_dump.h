/* host_stack_dump.h - every host thread's pc + frame-pointer backtrace, for hang reports (Linux, Android; elsewhere
 * a no-op returning 0). Not async-signal-safe: call it from an ordinary thread, one dump at a time. */
#ifndef XR_HOST_STACK_DUMP_H
#define XR_HOST_STACK_DUMP_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* writes one "[STACKS] tid ..." line per thread to out (NULL: stderr); returns the number of threads listed */
int xbox_HostStackDump(FILE *out, const char *why);

#ifdef __cplusplus
}
#endif

#endif
