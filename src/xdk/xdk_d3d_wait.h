/* xdk_d3d_wait.h - XDK D3D's fence wait (BlockOnTime) without the spin (2026-11-16).
 *
 * XDK D3D waits for the GPU with a busy loop: it reads the GPU semaphore ([g_pDevice + sem_off] points to it) and
 * compares it with the target until the GPU has caught up. On the console that ends within a GPU frame; here the
 * GPU is the pushbuffer executor on another host thread and the guest thread burns a whole host core meanwhile -
 * Orta's attract demo used 0.85-0.9 cores against 0.40 for the reference port until its override slept instead
 * (Orta Recompiled src/recomp_manual.c sub_002AFF90, 2026-11-03; the same pattern as Burnout 3 sub_00351770 and THPS 2X
 * sub_00019FE0). The executor writes the semaphore releases (method 0x1D70) to the title's real semaphore once it
 * knows where it is.
 *
 * xdk_d3d_block_on_time is that override's body for every port, one call in the port's wrap of the title's
 * BlockOnTime before the generated function runs (which then finds the wait satisfied, or takes over as before):
 *
 *     void sub_002AFF90(RECOMP_PARAMS)            // Orta PAL, XDK 5028
 *     {
 *         xdk_d3d_block_on_time(0x002BB788u, 0x30u, 0x2Cu, GUEST32(g_esp + 4));
 *         sub_002AFF90_gen(ctx, base);
 *     }
 *
 * device_ptr_va: the title's g_pDevice; sem_off / put_off: the device fields with the GPU semaphore pointer and the
 * submitted (put) value; target: BlockOnTime's first stack argument. The exit test is the XDK's own (unsigned):
 * done when (put - target) >= (put - *sem). target == put is left alone (D3D kicks the pushbuffer off itself
 * first). Sleeps 0.25 ms per check (host_sleep_ns, no spin), 250 ms at most. Does nothing without RECOMP_PB_EXEC.
 * Since step 6c (2026-11-17) the four values can come from the port's variant file instead ([d3d_block_on_time],
 * tools/gameconfig.py) and the lifter emits this wrap (gen/recomp_hle.c), so a port needs no hand-written override;
 * one in recomp_manual.c still wins. */
#ifndef XDK_D3D_WAIT_H
#define XDK_D3D_WAIT_H

#include <stdint.h>

void xdk_d3d_block_on_time(uint32_t device_ptr_va, uint32_t sem_off, uint32_t put_off, uint32_t target);

#endif
