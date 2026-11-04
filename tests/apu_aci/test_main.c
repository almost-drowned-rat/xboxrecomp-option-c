/* The AC'97 output channel model (src/apu/apu_aci.c): buffers complete at 48 kHz and raise the interrupt the way
 * DirectSound's ISR expects, status bits clear on write-1, the channel halts at the last valid buffer and resumes
 * on a new LVI, CIV/PICB stay read-only. Driven like Burnout 3's DirectSound drives it. See README.md.
 *
 * Run with: ctest --test-dir <build> */
#include "apu_aci.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The runtime links against the title's dispatch; nothing here calls it. */
void *recomp_lookup(unsigned long address) { (void)address; abort(); }
void *recomp_lookup_manual(unsigned long address) { (void)address; abort(); }

/* Block = NABM +0x100..+0x17F; the analog channel at +0x10, the digital one at +0x70, GLOB_STA at +0x30. */
#define A 0x10
#define D 0x70
#define BDL_PHYS 0x1000u
#define BUF_PHYS 0x2000u
#define BUF_LEN  960u                           /* 16-bit samples = 480 stereo frames = 10 ms */

static uint8_t s_mem[0x2000 + 32 * BUF_LEN * 2];
static uint8_t s_blk[0x80];
static int fails;

static const int16_t *mem(uint32_t phys, uint32_t bytes)
{
    return phys + bytes <= sizeof s_mem ? (const int16_t *)(s_mem + phys) : NULL;
}

static unsigned sr(int ch) { return (unsigned)(s_blk[ch + 6] | (s_blk[ch + 7] << 8)); }
static unsigned picb(int ch) { return (unsigned)(s_blk[ch + 8] | (s_blk[ch + 9] << 8)); }
static unsigned glob(void) { uint32_t g; memcpy(&g, s_blk + 0x30, 4); return g; }

/* A guest write: byte or word at off, with the model's write semantics applied afterwards. */
static int guest_write(int off, unsigned value, int bytes)
{
    uint8_t before[sizeof s_blk];
    memcpy(before, s_blk, sizeof s_blk);
    s_blk[off] = (uint8_t)value;
    if (bytes == 2) s_blk[off + 1] = (uint8_t)(value >> 8);
    if (bytes == 4) { s_blk[off + 1] = (uint8_t)(value >> 8); s_blk[off + 2] = (uint8_t)(value >> 16); s_blk[off + 3] = (uint8_t)(value >> 24); }
    return mcpx_aci_block_after_write(s_blk, before, (unsigned)off);
}

static void check(int ok, const char *what)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); fails++; }
}

/* What DirectSound's ISR does: GLOB_STA & 0x51, per channel SR, write SR back (clears), write GLOB_STA & ~1. */
static int isr(int *bcis_seen)
{
    unsigned g = glob() & 0x51, s = sr(A);
    if (g & 0x40) {
        if (s & 0x08) (*bcis_seen)++;
        guest_write(A + 6, s, 2);
    }
    return guest_write(0x30, g & ~1u, 4);
}

int main(void)
{
    int i, line, bcis = 0, irqs = 0;

    for (i = 0; i < 32; i++) {                  /* the BDL: 32 buffers of 10 ms, interrupt on completion */
        uint32_t a = BUF_PHYS + (uint32_t)i * BUF_LEN * 2u, lf = BUF_LEN | (0x8000u << 16);
        memcpy(s_mem + BDL_PHYS + i * 8, &a, 4);
        memcpy(s_mem + BDL_PHYS + i * 8 + 4, &lf, 4);
    }
    ((int16_t *)(s_mem + BUF_PHYS))[5] = 1234;  /* something in buffer 0 for the peak */
    s_blk[0x30] = 0x00; s_blk[0x31] = 0x01;     /* GLOB_STA: codec ready (0x100), as the runtime reports it */

    /* DirectSound's init (Burnout 3, 2026-10-26 diagnosis): reset both channels, BDBAR, CIV = LVI = 0, run. */
    guest_write(A + 0xB, 0x02, 1);
    check(sr(A) == 0x01, "reset leaves the channel halted (SR = DCH)");
    s_blk[A + 0xB] &= (uint8_t)~0x02;           /* the trap clears RR itself */
    guest_write(A + 0x0, BDL_PHYS, 4);
    guest_write(A + 0x5, 3, 1);                 /* LVI = 3: four buffers */
    line = guest_write(A + 0xB, 0x1D, 1);
    check(!line, "no interrupt before anything played");

    /* 1920 frames = four buffers; play in the monitor's 256-frame blocks and service each interrupt. */
    for (i = 0; i < 8; i++) {
        line = mcpx_aci_block_play(s_blk, 256, mem);
        if (line) {
            irqs++;
            check(glob() & 0x40, "the line is up with GLOB_STA 0x40 (analog) set");
            check(!(glob() & 0x10), "the idle digital channel raises nothing");
            check(!isr(&bcis), "the ISR's acknowledgement lowers the line");
        }
    }
    check(bcis == 4, "one BCIS per completed buffer (4)");
    check((sr(A) & 0x03) == 0x03, "halted at the last valid buffer (DCH | CELV)");
    check(s_blk[A + 4] == 3, "CIV stops at LVI");
    check(glob() & 0x100, "codec ready survives the acknowledgements");

    /* A halted channel stays halted... */
    line = mcpx_aci_block_play(s_blk, 256, mem);
    check(!line && s_blk[A + 4] == 3, "no progress while CIV == LVI");
    /* ...until DirectSound writes a new LVI. */
    guest_write(A + 0x5, 7, 1);
    mcpx_aci_block_play(s_blk, 256, mem);
    check(s_blk[A + 4] == 4 && !(sr(A) & 0x03), "a new LVI resumes with the next buffer");
    check(picb(A) == BUF_LEN - 512u, "PICB counts down the current buffer (16-bit samples)");

    /* A write next to SR (LVI, a byte at +5) must not clear a pending status. */
    s_blk[A + 6] |= 0x08;                       /* BCIS pending */
    guest_write(A + 0x5, 8, 1);
    check(sr(A) & 0x08, "an LVI write leaves a pending BCIS alone");
    guest_write(A + 6, sr(A), 2);               /* the acknowledgement clears it */
    check(!(sr(A) & 0x08), "writing SR back clears BCIS");

    /* Read-only registers keep the controller's value. */
    guest_write(A + 4, 0x1F, 1);
    check(s_blk[A + 4] == 4, "CIV is read-only");
    guest_write(A + 8, 0, 2);
    check(picb(A) == BUF_LEN - 512u, "PICB is read-only");

    if (fails)
        return 1;
    printf("PASS: %d interrupts, %d buffers completed with BCIS; halt at LVI, resume on a new LVI, W1C status.\n",
           irqs, bcis);
    return 0;
}
