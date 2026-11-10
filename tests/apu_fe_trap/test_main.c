/* apu_fe_trap: the front end's trapped method stays put until the service routine resumes it (Orta issue #1).
 *
 * The idle-voice trap (SE2FE_IDLE_VOICE) leaves the voice in FEDECPARAM and 0x8000 in FEDECMETH for DirectSound's
 * service routine. A title's PIO method written while the front end is trapped used to overwrite both - the routine,
 * delivered later, then read a mixed or foreign pair (Orta: "voice 0 went idle", its empty slot, [0+0x64]). Checks:
 *   1. a VOICE_OFF, then the idle scan: trapped on 0x8000 / the voice;
 *   2. a title method while trapped: the pair is kept (failed before the fix: METH 0x120, PARAM 0);
 *   3. a voice that stays idle at the head of its list does not starve the one behind it: after the resume the
 *      stopped voice is reported (option-c 15dd3d4 alone never did - Orta's SAVE & QUIT waited for ever);
 *   4. after the title resumes the front end, a title method is decoded again (the old behaviour outside a trap);
 *   5. a queued idle voice that was taken out of its list meanwhile is not reported (Orta stage 6 SAVE & QUIT).
 */
#include "apu_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *recomp_lookup(unsigned long address) { (void)address; abort(); }
void *recomp_lookup_manual(unsigned long address) { (void)address; abort(); }

extern uint8_t *g_apu_ram_ptr;
void mcpx_apu_vp_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size);

#define RAM_BYTES  (4u << 20)
#define VPV        0x00100000u    /* voice structures */
#define FEN        0x00200000u    /* notifier area */
#define VOICE      5u
#define STAY       0x40u          /* idle for good at the head of the list */

static uint8_t *ram;
static uint32_t *word(uint32_t phys) { return (uint32_t *)(ram + phys); }

static int check(int ok, const char *what, MCPXAPUState *d)
{
    if (!ok)
        fprintf(stderr, "FAIL: %s (FEDECMETH 0x%08X FEDECPARAM 0x%08X FECTL 0x%08X)\n", what,
                d->regs[NV_PAPU_FEDECMETH], d->regs[NV_PAPU_FEDECPARAM], d->regs[NV_PAPU_FECTL]);
    else
        printf("PASS: %s\n", what);
    return ok ? 0 : 1;
}

static int trapped(MCPXAPUState *d)
{
    return (d->regs[NV_PAPU_FECTL] & NV_PAPU_FECTL_FEMETHMODE) == NV_PAPU_FECTL_FEMETHMODE_TRAPPED;
}

int main(void)
{
    MCPXAPUState *d = (MCPXAPUState *)calloc(1, sizeof(*d));
    int failed = 0;

    ram = (uint8_t *)calloc(1, RAM_BYTES);
    if (!d || !ram) { fprintf(stderr, "out of memory\n"); return 2; }
    g_apu_ram_ptr = ram;
    d->ram_ptr = ram;
    qemu_mutex_init(&d->lock);
    qemu_cond_init(&d->cond);

    d->regs[NV_PAPU_VPVADDR] = VPV;
    d->regs[NV_PAPU_FENADDR] = FEN;
    d->regs[NV_PAPU_FETFORCE1] = NV_PAPU_FETFORCE1_SE2FE_IDLE_VOICE;
    /* The 2D list: STAY (idle for good, at the head - DirectSound's own submix voice in Orta) -> VOICE. */
    d->regs[NV_PAPU_TVL2D] = STAY;
    d->regs[NV_PAPU_TVL3D] = 0xFFFF;
    d->regs[NV_PAPU_TVLMP] = 0xFFFF;
    *word(VPV + STAY * NV_PAVS_SIZE + NV_PAVS_VOICE_TAR_PITCH_LINK) = VOICE;
    *word(VPV + STAY * NV_PAVS_SIZE + NV_PAVS_VOICE_PAR_STATE) = 0;
    *word(VPV + VOICE * NV_PAVS_SIZE + NV_PAVS_VOICE_TAR_PITCH_LINK) = 0xFFFF;   /* end of the list */
    *word(VPV + VOICE * NV_PAVS_SIZE + NV_PAVS_VOICE_PAR_STATE) = NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE;

    /* 1. The title turns VOICE off; the idle scan traps (first on the idle voice ahead of it). */
    mcpx_apu_vp_write(d, NV1BA0_PIO_VOICE_OFF, VOICE, 4);
    failed |= check(mcpx_apu_vp_idle_scan(d) == 1 && trapped(d), "the idle scan traps the front end", d);
    failed |= check(d->regs[NV_PAPU_FEDECMETH] == SE2FE_IDLE_VOICE && d->regs[NV_PAPU_FEDECPARAM] == STAY,
                    "trapped on 0x8000 / the first idle voice", d);

    /* 2. A title method before the service routine ran: the pair is kept. */
    mcpx_apu_vp_write(d, NV1BA0_PIO_SET_ANTECEDENT_VOICE, 0, 4);
    failed |= check(d->regs[NV_PAPU_FEDECMETH] == SE2FE_IDLE_VOICE && d->regs[NV_PAPU_FEDECPARAM] == STAY,
                    "a title method while trapped keeps the trapped pair", d);
    failed |= check(d->regs[NV_PAPU_FEAV] == 0, "the title method itself still took effect", d);

    /* 3. The routine resumes the front end: the voice behind the idle one is reported next, not starved by it
     *    (option-c 15dd3d4 alone: never - Orta's SAVE & QUIT waited for ever). */
    d->regs[NV_PAPU_FECTL] &= ~(uint32_t)(NV_PAPU_FECTL_FEMETHMODE | NV_PAPU_FECTL_FETRAPREASON);
    failed |= check(mcpx_apu_vp_idle_scan(d) == 1 && d->regs[NV_PAPU_FEDECPARAM] == VOICE,
                    "after the resume the stopped voice behind it is reported", d);

    /* 4. Resumed again: methods are decoded again (the old behaviour outside a trap). */
    d->regs[NV_PAPU_FECTL] &= ~(uint32_t)(NV_PAPU_FECTL_FEMETHMODE | NV_PAPU_FECTL_FETRAPREASON);
    mcpx_apu_vp_write(d, NV1BA0_PIO_SET_ANTECEDENT_VOICE, 0x00010003u, 4);
    failed |= check(d->regs[NV_PAPU_FEDECMETH] == NV1BA0_PIO_SET_ANTECEDENT_VOICE
                    && d->regs[NV_PAPU_FEDECPARAM] == 0x00010003u, "after the resume a title method is decoded", d);

    /* 5. A queued idle voice the routine took out of its list meanwhile (and DirectSound freed) is not reported
     *    later: the hardware only reports voices it walks (323e57d did - the empty slot, [0+0x64]). */
    mcpx_apu_vp_write(d, NV1BA0_PIO_VOICE_OFF, VOICE, 4);
    failed |= check(mcpx_apu_vp_idle_scan(d) == 1 && d->regs[NV_PAPU_FEDECPARAM] == STAY,
                    "a new idle scan traps on the head voice, the other one queued", d);
    *word(VPV + STAY * NV_PAVS_SIZE + NV_PAVS_VOICE_TAR_PITCH_LINK) = 0xFFFF;   /* VOICE unlinked */
    d->regs[NV_PAPU_FECTL] &= ~(uint32_t)(NV_PAPU_FECTL_FEMETHMODE | NV_PAPU_FECTL_FETRAPREASON);
    failed |= check(mcpx_apu_vp_idle_scan(d) == 0 && !trapped(d), "a queued voice no longer listed is not reported", d);

    free(ram);
    free(d);
    printf("%s\n", failed ? "FAILED" : "all passed");
    return failed;
}
