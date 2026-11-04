/* nv2a_clear_color.h - SET_COLOR_CLEAR_VALUE to the back ends' A8R8G8B8.
 *
 * The register is in the colour surface's own format (D3D converts the
 * D3DCOLOR before it writes the register): 5:6:5 for R5G6B5, 1:5:5:5 for the
 * X1R5G5B5 pair, A8R8G8B8 only for the 32-bit formats. Back ends take
 * A8R8G8B8, so the 16-bit colour is expanded here. THPS 2X clears its 256x256
 * R5G6B5 shadow targets to white, 0xFFFF: read as A8R8G8B8 that is
 * (0, 255, 255) -- the teal fringe the filtered edges of every skater shadow
 * showed. (Behaviour as in xemu's pgraph_get_clear_color; B8/G8B8 are never
 * cleared by D3D.)
 * Alpha stays what the register's top byte gives -- 0 for a 16-bit value --
 * not opaque: the back ends keep an alpha channel in every target, and THPS
 * 2X's shadow pass relies on the cleared part of its target staying
 * transparent there. Cleared opaque (xemu's choice, where a 16-bit target has
 * no alpha to sample), every shadow quad drew as a black square.
 *
 * Its own header so tests/nv2a_clear_color can check it without the executor.
 */
#ifndef NV2A_CLEAR_COLOR_H
#define NV2A_CLEAR_COLOR_H

#include <stdint.h>

/* clear: the register value; format: SET_SURFACE_FORMAT (colour format in
 * the low 4 bits). */
static inline uint32_t nv2a_clear_color_argb(uint32_t clear, uint32_t format)
{
    uint32_t c = clear, r, g, b, a = c & 0xFF000000u;
    switch (format & 0xF) {
    case 0x1: case 0x2:                  /* X1R5G5B5 */
        r = (c >> 10) & 0x1F; g = (c >> 5) & 0x1F; b = c & 0x1F;
        r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
        return a | (r << 16) | (g << 8) | b;
    case 0x3:                            /* R5G6B5 */
        r = (c >> 11) & 0x1F; g = (c >> 5) & 0x3F; b = c & 0x1F;
        r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
        return a | (r << 16) | (g << 8) | b;
    default:
        return c;
    }
}

#endif
