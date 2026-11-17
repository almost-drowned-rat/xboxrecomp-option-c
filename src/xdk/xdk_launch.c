/* xdk_launch.c - XLaunchNewImage helpers (xdk_launch.h). From Panzer Dragoon Orta's src/recomp_manual.c
 * sub_0022DA5C (Orta Recompiled). SPDX-License-Identifier: GPL-3.0-or-later */
#include "xdk_launch.h"

#include <stddef.h>
#include <stdio.h>

extern ptrdiff_t g_xbox_mem_offset;

static uint32_t rd32(uint32_t va)
{
    return *(volatile uint32_t *)((uintptr_t)va + g_xbox_mem_offset);
}

void xdk_launch_read(uint32_t esp, XdkLaunch *l)
{
    unsigned i = 0, k;
    l->ret = rd32(esp);
    l->path_va = rd32(esp + 4);
    l->data_va = rd32(esp + 8);
    if (l->path_va)
        for (; i < sizeof l->path - 1; i++) {
            char c = (char)(*(volatile uint8_t *)((uintptr_t)l->path_va + g_xbox_mem_offset + i));
            if (!c)
                break;
            l->path[i] = c;
        }
    l->path[i] = 0;
    fprintf(stderr, "[LAUNCH] XLaunchNewImage('%s', data 0x%08X) from 0x%08X\n",
            l->path_va ? l->path : "(NULL = dashboard)", l->data_va, l->ret);
    fprintf(stderr, "[LAUNCH] guest stack:");
    for (k = 0; k < 48; k++)
        fprintf(stderr, "%s%08X", (k % 8) ? " " : "\n    ", rd32(esp + k * 4));
    fprintf(stderr, "\n");
    fflush(stderr);
}

int xdk_launch_is(const XdkLaunch *l, const char *want)
{
    const char *p = l->path;
    if (!l->path_va)
        return 0;
    for (; *p && *want; p++, want++) {
        char a = *p;
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (a == '/')
            a = '\\';
        if (a != *want)
            return 0;
    }
    return *p == 0 && *want == 0;
}
