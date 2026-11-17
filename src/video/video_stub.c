/*
 * video_stub.c -- the movie player's entry points without the player.
 *
 * The player (video_player.c / video_pump.c, Media Foundation onto a D3D8
 * texture) was removed 2026-11-16 with the D3D11 layer; the runtime is
 * Vulkan only (nv2a_vk). The kernel only reaches these when
 * RECOMP_FMV_HOST asks it to substitute a host-played .wmv for the title's
 * own movie output; here that request fails, and the title plays its movie
 * itself, as it does by default.
 */

int xbox_VideoPlayFile(const char *host_path)
{
    (void)host_path;
    return -1;
}

int xbox_VideoIsPlaying(void)
{
    return 0;
}
