/* host_port.c - the port's names for the shared host layer (xr_host.h, host_set_port). 2026-11-16: the host
 * layer moved from Panzer Dragoon Orta (src/platform, Orta Recompiled) into the toolkit (maintainer 2026-11-15,
 * step 4: "Host-Schicht inkl. host_linux.c" into the toolkit); everything that named Orta is one of these. */
#include "xr_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_title[128] = "Xbox Recomp";
static char s_log[64] = "game";
static char s_env[32] = "RECOMP_PORT";

void host_set_port(const char *title, const char *log_name, const char *env_prefix)
{
    if (title && *title)
        snprintf(s_title, sizeof s_title, "%s", title);
    if (log_name && *log_name)
        snprintf(s_log, sizeof s_log, "%s", log_name);
    if (env_prefix && *env_prefix)
        snprintf(s_env, sizeof s_env, "%s", env_prefix);
}

const char *host_port_title(void) { return s_title; }
const char *host_port_log_name(void) { return s_log; }
const char *host_port_env_prefix(void) { return s_env; }

const char *host_port_getenv(const char *suffix)
{
    char name[96];
    snprintf(name, sizeof name, "%s_%s", s_env, suffix);
    return getenv(name);
}
