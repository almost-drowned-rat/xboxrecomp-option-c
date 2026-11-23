/* SPDX-License-Identifier: MIT
 * probe_dl.c - stand-in for the game library built on the device (2026-11-11). The smoke test copies this
 * .so from nativeLibraryDir into the app's internal storage (filesDir) and dlopen()s it from there - the route the
 * real game library (port_android.h PORT_GAME_LIB) takes (Android 10+ W^X forbids execve from app data; dlopen
 * should still be allowed). */
int xr_probe_answer(int x);
int xr_probe_answer(int x)
{
    return x * 2 + 1;
}
