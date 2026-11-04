/* The APU's VP/DSP mix must reach the output: mcpx_apu_monitor_frame used to clear monitor.frame_buf and render
 * only the test tone and the software mixer, so every title whose DirectSound drives the real APU (Burnout 3) was
 * silent. See README.md.
 *
 * Run with: ctest --test-dir <build> */
#include "apu_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The runtime links against the title's dispatch; nothing here calls it. */
void *recomp_lookup(unsigned long address) { (void)address; abort(); }
void *recomp_lookup_manual(unsigned long address) { (void)address; abort(); }

#define BLOCK  256          /* samples per monitor block (eight 32-sample frames) */
#define BLOCKS 8

static int16_t pattern(int block, int i, int ch) { return (int16_t)(1000 + block * BLOCK + i) * (ch ? -1 : 1); }

int main(void)
{
    /* Zeroed: no XAudio2 and no waveOut initialised, so the samples stay in the output ring for the test to take;
     * no test tone, no software-mixer voices. */
    MCPXAPUState *d = (MCPXAPUState *)calloc(1, sizeof(*d));
    static int16_t got[BLOCKS * BLOCK + BLOCK][2];
    unsigned n;
    int b, i, fails = 0;

    if (!d) { fprintf(stderr, "out of memory\n"); return 2; }

    for (b = 0; b < BLOCKS; b++) {
        for (i = 0; i < BLOCK; i++) {           /* what mcpx_apu_dsp_frame leaves after eight frames */
            d->monitor.frame_buf[i][0] = pattern(b, i, 0);
            d->monitor.frame_buf[i][1] = pattern(b, i, 1);
        }
        d->ep_frame_div = 8 * b + 7;            /* the eighth frame of a block: the monitor runs */
        mcpx_apu_monitor_frame(d);
        for (i = 0; i < BLOCK; i++)
            if (d->monitor.frame_buf[i][0] || d->monitor.frame_buf[i][1]) {
                fprintf(stderr, "FAIL: block %d left frame_buf[%d] set - the next block would repeat it\n", b, i);
                fails++;
                break;
            }
    }
    /* A block in which the APU produced nothing (se_frame not run): silence, not the previous block again. */
    d->ep_frame_div = 8 * BLOCKS + 7;
    mcpx_apu_monitor_frame(d);
    /* A frame that is not the eighth must not touch the ring. */
    d->ep_frame_div = 8 * BLOCKS + 8;
    mcpx_apu_monitor_frame(d);

    n = mcpx_apu_monitor_take(got, (unsigned)(sizeof got / sizeof got[0]) + 100u);
    if (n != BLOCKS * BLOCK + BLOCK) {
        fprintf(stderr, "FAIL: %u samples in the output ring, expected %d (one per sample produced)\n", n,
                BLOCKS * BLOCK + BLOCK);
        fails++;
    }
    for (b = 0; b < BLOCKS && !fails; b++)
        for (i = 0; i < BLOCK; i++)
            if (got[b * BLOCK + i][0] != pattern(b, i, 0) || got[b * BLOCK + i][1] != pattern(b, i, 1)) {
                fprintf(stderr, "FAIL: block %d sample %d is %d/%d, expected %d/%d - the VP/DSP mix did not reach "
                        "the output\n", b, i, got[b * BLOCK + i][0], got[b * BLOCK + i][1], pattern(b, i, 0),
                        pattern(b, i, 1));
                fails++;
                break;
            }
    for (i = 0; i < BLOCK && !fails; i++)
        if (got[BLOCKS * BLOCK + i][0] || got[BLOCKS * BLOCK + i][1]) {
            fprintf(stderr, "FAIL: an empty block came out as %d/%d at %d\n", got[BLOCKS * BLOCK + i][0],
                    got[BLOCKS * BLOCK + i][1], i);
            fails++;
        }
    free(d);
    if (fails)
        return 1;
    puts("PASS: every VP/DSP block reaches the output once, in order; empty blocks are silence.");
    return 0;
}
