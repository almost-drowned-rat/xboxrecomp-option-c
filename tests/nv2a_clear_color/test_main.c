/* nv2a_clear_color: SET_COLOR_CLEAR_VALUE is in the colour surface's format.
 * 16-bit targets (R5G6B5, X1R5G5B5) are expanded to 8:8:8 with the alpha of
 * the register's top byte (0 for a plain 16-bit value); 32-bit formats pass
 * through. The THPS 2X fix (vulkan-backend d037dc5 + 6d3583f): read as
 * A8R8G8B8, a white R5G6B5 clear (0xFFFF) was teal (0x0000FFFF). */
#include <stdio.h>
#include "nv2a_clear_color.h"

static int g_fail;

static void check(const char *what, uint32_t clear, uint32_t format, uint32_t want)
{
    uint32_t got = nv2a_clear_color_argb(clear, format);
    if (got != want) {
        printf("FAIL %-28s clear %08X format %X: got %08X, want %08X\n", what, clear, format, got, want);
        g_fail++;
    }
}

int main(void)
{
    /* R5G6B5 (format 3) */
    check("R5G6B5 white", 0x0000FFFFu, 0x3, 0x00FFFFFFu);
    check("R5G6B5 red", 0x0000F800u, 0x3, 0x00FF0000u);
    check("R5G6B5 green", 0x000007E0u, 0x3, 0x0000FF00u);
    check("R5G6B5 blue", 0x0000001Fu, 0x3, 0x000000FFu);
    check("R5G6B5 mid grey", 0x00008410u, 0x3, 0x00848284u);
    check("R5G6B5 black", 0x00000000u, 0x3, 0x00000000u);
    check("R5G6B5 top byte is alpha", 0xAB00FFFFu, 0x3, 0xABFFFFFFu);
    check("format's high bits ignored", 0x0000FFFFu, 0x123, 0x00FFFFFFu);
    /* X1R5G5B5 (formats 1 and 2) */
    check("X1R5G5B5 white", 0x00007FFFu, 0x1, 0x00FFFFFFu);
    check("X1R5G5B5 red", 0x00007C00u, 0x2, 0x00FF0000u);
    check("X1R5G5B5 green", 0x000003E0u, 0x1, 0x0000FF00u);
    check("X1R5G5B5 blue", 0x0000001Fu, 0x2, 0x000000FFu);
    check("X1R5G5B5 X bit ignored", 0x00008000u, 0x1, 0x00000000u);
    /* 32-bit formats: unchanged */
    check("X8R8G8B8 passes", 0x00123456u, 0x5, 0x00123456u);
    check("A8R8G8B8 passes", 0xFF123456u, 0x8, 0xFF123456u);
    check("A8R8G8B8 white", 0xFFFFFFFFu, 0x8, 0xFFFFFFFFu);
    if (g_fail) { printf("%d failures\n", g_fail); return 1; }
    printf("nv2a_clear_color: all checks passed\n");
    return 0;
}
