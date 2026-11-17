/* xdk_d3d_wait.c - see xdk_d3d_wait.h. The logic of Panzer Dragoon Orta's sub_002AFF90 override (Orta
 * Recompiled src/recomp_manual.c, 2026-11-01/03), parameterised for every port. */
#include "xdk_d3d_wait.h"

#include "kernel.h"                 /* g_xbox_mem_offset */
#include "nv2a_backend.h"           /* nv2a_pb_set_semaphore_target */
#include "platform/host_wait.h"     /* host_sleep_ns */

#include <stdio.h>
#include <stdlib.h>

extern ptrdiff_t g_xbox_mem_offset;

static uint32_t g32(uint32_t va)
{
    return *(volatile uint32_t *)((uintptr_t)va + g_xbox_mem_offset);
}

void xdk_d3d_block_on_time(uint32_t device_ptr_va, uint32_t sem_off, uint32_t put_off, uint32_t target)
{
    static int exec = -1;
    static uint32_t registered;
    uint32_t dev, sem, put;
    int i;

    if (exec < 0) {
        const char *e = getenv("RECOMP_PB_EXEC");
        exec = e && *e;
    }
    if (!exec)
        return;
    dev = g32(device_ptr_va);
    sem = dev ? g32(dev + sem_off) : 0;
    if (!sem)
        return;
    if (sem != registered) {                    /* once per device: the executor writes the releases there */
        nv2a_pb_set_semaphore_target(sem);
        fprintf(stderr, "[D3D] GPU semaphore at 0x%08X\n", sem);
        registered = sem;
    }
    put = g32(dev + put_off);
    if (target == put)
        return;
    for (i = 0; i < 1000 && (uint32_t)(put - target) < (uint32_t)(put - g32(sem)); i++)
        host_sleep_ns(250000ull);
}
