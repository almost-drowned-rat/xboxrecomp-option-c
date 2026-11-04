/*
 * A guest D3D surface written to a BMP, straight from guest memory.
 *
 * Why: G3 rule 4 compares a movie's decoded frame k across runs. Taking that
 * picture "at the next flip after frame k was decoded" only pins one image while
 * every decoded frame is presented exactly once. A title whose movie runs on its
 * own clock and presents twice per movie frame (Burnout 3 at 60 Hz, once the
 * decoder kept up) shows frame k+2..k+5 at that flip, run by run. Reading the
 * surface the decoder has just written does not depend on flips or pacing: it is
 * the decoder's output for frame k, nothing else.
 *
 * The surface header is the XDK D3D resource layout: +0x04 Data (physical
 * address), +0x0C Format (bits 8-15 the format code, bits 20-27 log2 width and
 * height for power-of-two surfaces), +0x10 Size (linear surfaces: width-1,
 * height-1, pitch/64-1). Formats: 32-bit and 16-bit RGB, linear and swizzled,
 * and YUY2; anything else is refused with the format code in the message.
 */
#include "guest_surface_dump.h"
#include "d3d/d3d8_swizzle.h"
#include "kernel/xbox_memory_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FMT_A8R8G8B8      0x06u
#define FMT_X8R8G8B8      0x07u
#define FMT_R5G6B5        0x05u
#define FMT_LIN_A8R8G8B8  0x12u
#define FMT_LIN_R5G6B5    0x11u
#define FMT_LIN_X8R8G8B8  0x1Eu
#define FMT_YUY2          0x24u

static uint32_t gread32(uint32_t va)
{
    return *(const volatile uint32_t *)((const uint8_t *)xbox_GetMemoryOffset() + va);
}

static uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void yuv_to_bgra(int y, int u, int v, uint8_t *o)
{
    int c = y - 16, d = u - 128, e = v - 128;
    o[2] = clamp8((298 * c + 409 * e + 128) >> 8);
    o[1] = clamp8((298 * c - 100 * d - 208 * e + 128) >> 8);
    o[0] = clamp8((298 * c + 516 * d + 128) >> 8);
    o[3] = 0xFF;
}

static int write_bmp(const char *path, const uint8_t *bgra, uint32_t w, uint32_t h)
{
    uint8_t hdr[54];
    uint32_t bytes = w * h * 4;
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    *(uint32_t *)(hdr + 2) = 54 + bytes;
    *(uint32_t *)(hdr + 10) = 54;
    *(uint32_t *)(hdr + 14) = 40;
    *(int32_t *)(hdr + 18) = (int32_t)w;
    *(int32_t *)(hdr + 22) = -(int32_t)h;          /* top-down */
    *(uint16_t *)(hdr + 26) = 1;
    *(uint16_t *)(hdr + 28) = 32;
    *(uint32_t *)(hdr + 34) = bytes;
    if (fwrite(hdr, 1, sizeof hdr, f) != sizeof hdr || fwrite(bgra, 1, bytes, f) != bytes) {
        fclose(f);
        return -1;
    }
    return fclose(f) ? -1 : 0;
}

int xbox_dump_guest_surface(uint32_t surface_va, const char *bmp_path, char *why, size_t why_len)
{
    uint32_t data, format, size, fmt, w, h, pitch, bpp, x, y;
    int swizzled;
    const uint8_t *src;
    uint8_t *lin = NULL, *out;

    if (!surface_va) {
        snprintf(why, why_len, "no surface");
        return -1;
    }
    data = gread32(surface_va + 0x04);
    format = gread32(surface_va + 0x0C);
    size = gread32(surface_va + 0x10);
    fmt = (format >> 8) & 0xFFu;
    switch (fmt) {
    case FMT_A8R8G8B8: case FMT_X8R8G8B8: case FMT_LIN_A8R8G8B8: case FMT_LIN_X8R8G8B8: bpp = 4; break;
    case FMT_R5G6B5: case FMT_LIN_R5G6B5: case FMT_YUY2: bpp = 2; break;
    default:
        snprintf(why, why_len, "format 0x%02X not supported (Format 0x%08X)", fmt, format);
        return -1;
    }
    swizzled = fmt == FMT_A8R8G8B8 || fmt == FMT_X8R8G8B8 || fmt == FMT_R5G6B5;
    if (size) {
        w = (size & 0xFFFu) + 1;
        h = ((size >> 12) & 0xFFFu) + 1;
        pitch = (((size >> 24) & 0xFFu) + 1) * 64;
    } else {
        w = 1u << ((format >> 20) & 0xFu);
        h = 1u << ((format >> 24) & 0xFu);
        pitch = w * bpp;
    }
    if (w > 4096 || h > 4096 || pitch < w * bpp) {
        snprintf(why, why_len, "implausible size %ux%u pitch %u (Size 0x%08X Format 0x%08X)", w, h, pitch, size, format);
        return -1;
    }
    src = (const uint8_t *)xbox_GetMemoryOffset() + (0x80000000u | (data & 0x0FFFFFFFu));
    if (swizzled) {
        lin = (uint8_t *)malloc((size_t)w * h * bpp);
        if (!lin) { snprintf(why, why_len, "out of memory"); return -1; }
        xbox_unswizzle_rect(lin, src, w, h, bpp);
        src = lin;
        pitch = w * bpp;
    }
    out = (uint8_t *)malloc((size_t)w * h * 4);
    if (!out) { free(lin); snprintf(why, why_len, "out of memory"); return -1; }
    for (y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t)y * pitch;
        uint8_t *o = out + (size_t)y * w * 4;
        for (x = 0; x < w; x++, o += 4) {
            if (bpp == 4) {
                o[0] = row[x * 4]; o[1] = row[x * 4 + 1]; o[2] = row[x * 4 + 2]; o[3] = 0xFF;
            } else if (fmt == FMT_YUY2) {
                const uint8_t *p = row + (x & ~1u) * 2;            /* Y0 U Y1 V */
                yuv_to_bgra((x & 1u) ? p[2] : p[0], p[1], p[3], o);
            } else {
                uint32_t v = row[x * 2] | (row[x * 2 + 1] << 8);
                o[0] = (uint8_t)d3d8_expand_channel(v & 0x1Fu, 5);
                o[1] = (uint8_t)d3d8_expand_channel((v >> 5) & 0x3Fu, 6);
                o[2] = (uint8_t)d3d8_expand_channel((v >> 11) & 0x1Fu, 5);
                o[3] = 0xFF;
            }
        }
    }
    free(lin);
    if (write_bmp(bmp_path, out, w, h)) {
        free(out);
        snprintf(why, why_len, "cannot write %s", bmp_path);
        return -1;
    }
    free(out);
    snprintf(why, why_len, "%ux%u format 0x%02X", w, h, fmt);
    return 0;
}
