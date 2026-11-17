/* xdk_launch.h - XAPI's XLaunchNewImage(lpTitlePath, pLaunchData) in a port (2026-11-16, from Panzer Dragoon
 * Orta's src/recomp_manual.c sub_0022DA5C; maintainer's order 2026-11-15, step 4: "XLaunchNewImage" generic).
 *
 * On the console a path "D:\..." reboots into that XBE with the launch data page filled, NULL returns to the
 * dashboard; neither returns. A recomp has only the one title, so a port's wrap of the title's XLaunchNewImage asks
 * the toolkit what the call is and refuses the launches it cannot do, with ERROR_NOT_SUPPORTED, instead of letting
 * the stock code reboot:
 *
 *     void sub_0022DA5C(RECOMP_PARAMS)            // Orta PAL: XLaunchNewImage, stdcall 8
 *     {
 *         XdkLaunch l;
 *         xdk_launch_read(g_esp, &l);             // reads and logs the path / data / caller
 *         if (xdk_launch_is(&l, "d:\\pd1.xbe") && !orta_launch_saturn_bonus()) {
 *             g_eax = XDK_ERROR_NOT_SUPPORTED;
 *             g_esp += 4 + 8;                     // ret 8
 *             return;
 *         }
 *         sub_0022DA5C_gen(ctx, base);
 *     }
 *
 * What the title does when the call returns (it never does on the console) is the port's to check - Orta's menu
 * stays where it was. The default for an unknown path is the stock code, logged. */
#ifndef XDK_LAUNCH_H
#define XDK_LAUNCH_H

#include <stdint.h>

#define XDK_ERROR_NOT_SUPPORTED 50u

typedef struct {
    uint32_t ret;        /* the caller's return address */
    uint32_t path_va;    /* 0 = back to the dashboard */
    uint32_t data_va;    /* the LAUNCH_DATA the title passes (0 = none) */
    char     path[64];   /* the path, "" for NULL */
} XdkLaunch;

/* Reads the stdcall arguments at esp (return address, path, data) and logs the call with the guest stack
 * ("[LAUNCH] ..."). */
void xdk_launch_read(uint32_t esp, XdkLaunch *out);
/* 1 when the path equals want (lower case, '\\' separators; the path compared case-insensitively, '/' = '\\'). */
int xdk_launch_is(const XdkLaunch *l, const char *want);

#endif
