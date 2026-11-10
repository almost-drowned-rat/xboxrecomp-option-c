/* phys_resolve: a physical number means one byte (Orta issue #1 stage 6, option-c).
 *
 * The heap is identity-mapped and the contiguous window is separate storage, so MmGetPhysicalAddress used to answer
 * the same number for heap VA P and contiguous VA 0x80000000 + P, and the per-page owner table decided for bus
 * masters - stickily: DirectSound's heap buffers noted pages as heap, the next DirectSound object got contiguous
 * blocks with those numbers, and the APU wrote them into the heap (a collision mesh lost a vertex pointer). Checks:
 *   1. a heap VA gets a number of its own (VA + XBOX_HEAP_PHYS_BIAS), a contiguous VA the window offset;
 *   2. the biased number resolves back to the heap VA, a contiguous number does not;
 *   3. asking for a heap page's number no longer marks the page "heap": a contiguous block handed out on that
 *      number afterwards is the window's (failed before: owner stayed RAM);
 *   4. the APU resolves the biased number to the heap bytes, not the window (mcpx_apu_phys).
 * RECOMP_PHYS_BIAS=0 brings the old behaviour back: then 1, 2 and 3 fail (negative control).
 */
#include "kernel.h"

#include <stdio.h>
#include <stdlib.h>

void *recomp_lookup(unsigned long address) { (void)address; abort(); }
void *recomp_lookup_manual(unsigned long address) { (void)address; abort(); }

extern uint8_t *g_apu_ram_ptr;
uint8_t *mcpx_apu_phys(uint64_t addr);

#define HEAP_VA    0x014F3A90u    /* Orta's collision triangle */
#define CONTIG_VA  0x814F3000u    /* a contiguous block with the same page number */

static int check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    return ok ? 0 : 1;
}

int main(void)
{
    static uint8_t ram_dummy[16];
    int failed = 0;
    uint32_t heap_phys, contig_phys, va = 0;

    /* The contiguous page first, then the heap buffer on the same number (DirectSound's order in Orta's stage 5). */
    contig_phys = (uint32_t)xbox_MmGetPhysicalAddress((PVOID)(uintptr_t)CONTIG_VA);
    heap_phys = (uint32_t)xbox_MmGetPhysicalAddress((PVOID)(uintptr_t)HEAP_VA);

    /* 1. */
    failed |= check(heap_phys == HEAP_VA + XBOX_HEAP_PHYS_BIAS, "a heap VA gets VA + 256 MB");
    failed |= check(contig_phys == CONTIG_VA - XBOX_CONTIG_BASE, "a contiguous VA gets its window offset");
    failed |= check((heap_phys >> 12) != (contig_phys >> 12), "the two never share a page number");

    /* 2. */
    failed |= check(xbox_PhysHeapVA(heap_phys, &va) && va == HEAP_VA, "the heap number resolves to the heap VA");
    failed |= check(!xbox_PhysHeapVA(contig_phys, &va), "a contiguous number is not taken for heap");

    /* 3. The page was asked about as heap above (its old identity number); a contiguous block lands on it now. */
    xbox_PhysNoteRange(HEAP_VA & ~0xFFFu, 0x1000, XBOX_PHYS_CONTIG);
    failed |= check(xbox_PhysOwner(HEAP_VA) == XBOX_PHYS_CONTIG,
                    "a contiguous block on that number belongs to the window (owner table not flipped to heap)");

    /* 4. */
    g_apu_ram_ptr = ram_dummy;                 /* pointer arithmetic only, nothing is read */
    failed |= check(mcpx_apu_phys(heap_phys) == ram_dummy + HEAP_VA, "the APU reads the heap number in the heap");

    printf("%s\n", failed ? "FAILED" : "all passed");
    return failed;
}
