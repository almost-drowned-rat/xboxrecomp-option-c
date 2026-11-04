/* RECOMP_APU_DSP_ACK=auto: the DSP command doorbell is GPSADDR SGE[0] + 0x810
 * and follows the table when the title reprograms it. See README.md.
 *
 * Run with: ctest --test-dir <build>   (two arms; see CMakeLists.txt) */
#include "apu_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *recomp_lookup(unsigned long address) { (void)address; abort(); }
void *recomp_lookup_manual(unsigned long address) { (void)address; abort(); }

extern uint8_t *g_apu_ram_ptr;

#define RAM_BYTES   (4u << 20)
#define SGE_TABLE   0x00100000u   /* GPSADDR: the scatter-gather table */
#define PAGE_A      0x00203000u   /* scratch page 0, first placement */
#define PAGE_B      0x00317000u   /* after the title reprograms the table */

static uint8_t *ram;

static uint32_t *word(uint32_t phys) { return (uint32_t *)(ram + phys); }

static int check(int ok, const char *what)
{
    if (!ok)
        fprintf(stderr, "FAIL: %s\n", what);
    return ok ? 0 : 1;
}

int main(void)
{
    const char *expect_env = getenv("APU_DOORBELL_EXPECT");
    const int expect_ack = expect_env ? atoi(expect_env) : 1;
    MCPXAPUState *d = (MCPXAPUState *)calloc(1, sizeof(*d));
    int failed = 0;

    ram = (uint8_t *)calloc(1, RAM_BYTES);
    if (!d || !ram) { fprintf(stderr, "out of memory\n"); return 2; }
    g_apu_ram_ptr = ram;
    d->ram_ptr = ram;

    /* Nothing programmed yet: no doorbell, and a command word in the page
     * that is not mapped yet stays untouched. */
    *word(PAGE_A + 0x810) = 3;
    mcpx_apu_dsp_ack_poll(d);
    failed |= check(mcpx_apu_dsp_auto_doorbell(d) == 0, "doorbell before GPSADDR is set");
    failed |= check(*word(PAGE_A + 0x810) == 3, "acknowledged before GPSADDR is set");

    /* GPSADDR set, entry 0 still empty: still nothing. */
    d->regs[NV_PAPU_GPSADDR] = SGE_TABLE;
    mcpx_apu_dsp_ack_poll(d);
    failed |= check(mcpx_apu_dsp_auto_doorbell(d) == 0, "doorbell with SGE[0] empty");
    failed |= check(*word(PAGE_A + 0x810) == 3, "acknowledged with SGE[0] empty");

    /* Entry 0 = page A (control bits in the low 12 bits are ignored); entry 1
     * is another page and must not matter. */
    *word(SGE_TABLE) = PAGE_A | 0x3;
    *word(SGE_TABLE + 8) = 0x00400000u;
    *word(PAGE_A + 0x80C) = 0x55;
    *word(PAGE_A + 0x814) = 0x55;
    if (expect_ack) {
        mcpx_apu_dsp_ack_poll(d);
        failed |= check(mcpx_apu_dsp_auto_doorbell(d) == PAGE_A + 0x810, "doorbell != SGE[0] + 0x810");
        failed |= check(*word(PAGE_A + 0x810) == 0, "command at SGE[0] + 0x810 not acknowledged");
        failed |= check(*word(PAGE_A + 0x80C) == 0x55 && *word(PAGE_A + 0x814) == 0x55,"neighbour words touched");

        /* A second command on the same page. */
        *word(PAGE_A + 0x810) = 0x11;
        mcpx_apu_dsp_ack_poll(d);
        failed |= check(*word(PAGE_A + 0x810) == 0, "second command not acknowledged");

        /* The title moves the scratch page: the old page is ordinary memory
         * again, the new one is followed. */
        *word(SGE_TABLE) = PAGE_B;
        *word(PAGE_A + 0x810) = 7;
        *word(PAGE_B + 0x810) = 3;
        mcpx_apu_dsp_ack_poll(d);
        failed |= check(*word(PAGE_B + 0x810) == 0, "moved page not followed");
        failed |= check(*word(PAGE_A + 0x810) == 7, "old page still acknowledged after the move");
        if (!failed)
            puts("PASS: SGE[0] + 0x810 acknowledged, nothing before GPSADDR and entry 0 are set, "
                 "neighbours untouched, a moved page followed.");
    } else {
        /* Negative control: without =auto the same command is never answered,
         * which is the boot hang this replaces. */
        mcpx_apu_dsp_ack_poll(d);
        failed |= check(*word(PAGE_A + 0x810) == 3, "acknowledged without RECOMP_APU_DSP_ACK=auto");
        if (!failed)
            puts("PASS: without =auto the command stays posted (the hang this mode answers).");
    }

    free(ram);
    free(d);
    return failed;
}
