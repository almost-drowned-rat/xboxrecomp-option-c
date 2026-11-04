#ifndef XBOXRECOMP_GUEST_SURFACE_DUMP_H
#define XBOXRECOMP_GUEST_SURFACE_DUMP_H

#include <stddef.h>
#include <stdint.h>

/* Write the guest D3D surface at surface_va (its resource header) to a 32-bit
 * top-down BMP. Returns 0 on success; why gets the size and format, or the
 * reason it was refused. For decode-synchronous captures (G3 rule 4): see
 * guest_surface_dump.c. */
int xbox_dump_guest_surface(uint32_t surface_va, const char *bmp_path, char *why, size_t why_len);

#endif
