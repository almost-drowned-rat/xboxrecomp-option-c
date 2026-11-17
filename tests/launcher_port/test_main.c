/* launcher_port test: a port launcher's whole main is XrLauncherConfig + XrPortLauncher + one call (the way
 * Panzer Dragoon Orta's launcher/main.c reads after the move into the toolkit, 2026-11-16). */
#include "launcher_port.h"

#include <SDL3/SDL_main.h>

static const unsigned char g_icon[1] = { 0 };

int main(int argc, char **argv)
{
    static const xr_game_version versions[] = {
        { "Test", "0000000000000000000000000000000000000000" },
    };
    static const XrLauncherConfig c = {
        "Launcher Port Test", "Launcher Port Test (Test)", "Launcher Port Test Recomp", "TEST.exe", 0x00000000u,
        versions, 1, g_icon, 0, "launcher-port-test",
    };
    static const XrPortLauncher p = { &c, "Launcher Port Test Recomp.exe", "C:\\Games\\Test", NULL, NULL };
    return xr_launcher_port_main(&p, argc, argv);
}
