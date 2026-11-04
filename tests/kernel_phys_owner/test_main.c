/*
 * The physical-page owner table (kernel_memory.c): which storage a physical page number was handed out for.
 *
 * Bus masters (APU, USB host) resolve a physical address through it: the contiguous window and identity-mapped
 * pool memory share physical numbers here. Checked:
 *   - MmGetPhysicalAddress notes the page it answers (window or RAM), the last answer wins;
 *   - xbox_PhysNoteRange (the contiguous allocators, 2026-11-01) notes every page of a block, so a page
 *     inside it that nobody asked about resolves to the window (Panzer Dragoon Orta: DirectSound computes voice
 *     0x44 at physical 0x28A200 from its base without asking; the APU read the XBE image copy there);
 *   - but a page some MmGetPhysicalAddress already answered keeps that answer (THPS 2X's pad buffer in pool memory
 *     shares its page number with a contiguous block handed out later);
 *   - a block reaching past 64 MB is clipped, a zero size notes nothing.
 * No guest memory is touched; the kernel functions are called directly.
 */
#include "kernel.h"
#include "xbox_memory_layout.h"   /* recomp_func_t */

#include <stdio.h>

/* Provided by the generated title; this harness has none and calls no guest function. */
recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { (void)xbox_va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

static int failures;

static void check(int ok, const char *what)
{
    printf("  %-70s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    check(xbox_PhysOwner(0x288000) == XBOX_PHYS_UNKNOWN, "a page nobody noted is unknown");

    /* DirectSound: asks once for its base, the allocator noted the whole block. */
    xbox_PhysNoteRange(0x288000, 0x4000, XBOX_PHYS_CONTIG);
    check(xbox_PhysOwner(0x288000) == XBOX_PHYS_CONTIG, "range: first page of the block is contiguous");
    check(xbox_PhysOwner(0x28A200) == XBOX_PHYS_CONTIG, "range: voice 0x44 page (never asked) is contiguous");
    check(xbox_PhysOwner(0x28BFFF) == XBOX_PHYS_CONTIG, "range: last byte's page is contiguous");
    check(xbox_PhysOwner(0x28C000) == XBOX_PHYS_UNKNOWN, "range: the page after the block stays unknown");

    /* THPS 2X: a pool buffer's page answered as RAM first, a contiguous block covering it handed out later. */
    xbox_MmGetPhysicalAddress((PVOID)(uintptr_t)0x01C0F52Au);
    check(xbox_PhysOwner(0x01C0F52A) == XBOX_PHYS_RAM, "MmGetPhysicalAddress(pool VA) notes RAM");
    xbox_PhysNoteRange(0x01C00000, 0x20000, XBOX_PHYS_CONTIG);
    check(xbox_PhysOwner(0x01C0F52A) == XBOX_PHYS_RAM, "range keeps a page already answered as RAM");
    check(xbox_PhysOwner(0x01C10000) == XBOX_PHYS_CONTIG, "range notes the block's other pages");

    /* An explicit answer still overrides a range note (last answer wins). */
    xbox_MmGetPhysicalAddress((PVOID)(uintptr_t)0x0028A000u);
    check(xbox_PhysOwner(0x28A000) == XBOX_PHYS_RAM, "MmGetPhysicalAddress overrides a range note");
    xbox_MmGetPhysicalAddress((PVOID)(uintptr_t)0x8028A000u);
    check(xbox_PhysOwner(0x28A000) == XBOX_PHYS_CONTIG, "MmGetPhysicalAddress(window VA) notes contiguous");

    /* Edges. */
    xbox_PhysNoteRange(0x03FFF000, 0x3000, XBOX_PHYS_CONTIG);
    check(xbox_PhysOwner(0x03FFF000) == XBOX_PHYS_CONTIG, "a block past 64 MB: its last real page is noted");
    check(xbox_PhysOwner(0x04000000) == XBOX_PHYS_UNKNOWN, "a block past 64 MB: nothing beyond is noted");
    xbox_PhysNoteRange(0x00500000, 0, XBOX_PHYS_CONTIG);
    check(xbox_PhysOwner(0x00500000) == XBOX_PHYS_UNKNOWN, "a zero-size block notes nothing");
    xbox_PhysNoteRange(0xFFFFF000u, 0x2000, XBOX_PHYS_CONTIG);
    check(xbox_PhysOwner(0x00000000) == XBOX_PHYS_UNKNOWN, "a wrapping range does not wrap to page 0");

    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
