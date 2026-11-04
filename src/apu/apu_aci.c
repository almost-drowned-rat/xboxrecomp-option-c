/*
 * apu_aci.c -- the MCPX audio codec interface (AC'97 bus master): channel model and its interrupt (vector 6).
 *
 * DirectSound starts two output channels at init (analog at NABM +0x110, digital at +0x170): it resets them,
 * points BDBAR at a 32-entry buffer descriptor list (BDL), sets CIV = LVI = 0 and CR = 0x1D (run + interrupts on
 * buffer completion, last valid buffer and FIFO error). On hardware each channel then plays its buffers at 48 kHz
 * and raises vector 6 per completed buffer; DirectSound's ISR (Burnout 3 US 0x002FEAC4) reads GLOB_STA & 0x51,
 * checks each channel's SR for BCIS, services the channel (adds the buffer length to its play position, writes the
 * next descriptors and LVI, calls a per-buffer callback) or queues a DPC for it, and acknowledges SR and GLOB_STA.
 * Here nothing ever completed - the registers kept what DirectSound wrote at init, the interrupt never came.
 *
 * The model plays the running channels in step with the APU's output (the monitor's 256-frame blocks, real time),
 * sets SR and GLOB_STA as the controller does, drives the vector-6 line through the device bus, and gives the
 * registers their write semantics: SR's status bits are write-1-to-clear, CIV/PIV/PICB read-only, RR resets the
 * channel, a new LVI resumes a channel halted at the last valid buffer. The buffers' samples are not played - the
 * output comes from the VP mix (monitor); the model only measures their peak, which shows whether anything is in
 * them (the GP/EP DSPs that would fill them on hardware are stubs here).
 *
 * What it does for Burnout 3 US (2026-10-26 diagnosis): both channels start with one descriptor, the same 4096-
 * sample silence buffer (IOC | BUP), LVI 0. It completes once per channel, vector 6 is raised, DirectSound's ISR
 * acknowledges BCIS + LVBCI - and queues nothing more: its channel queue stays empty, because the packets it would
 * append come from the EP DSP's output, which is not emulated. So after boot both channels sit halted at LVI, two
 * interrupts in a whole run. Kept because it is the hardware's behaviour at no cost; refilling needs the EP DSP
 * (open item in docs/option_c.md - matters for digital / 5.1 output, not for stereo, which the VP mix carries).
 *
 * Register block offsets below are relative to NABM +0x100.
 */
#include "apu_aci.h"

#include "platform/xbox_winnt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CH_ANALOG   0x10u               /* NABM +0x110 */
#define CH_DIGITAL  0x70u               /* NABM +0x170 */
#define R_BDBAR     0x0u
#define R_CIV       0x4u
#define R_LVI       0x5u
#define R_SR        0x6u
#define R_PICB      0x8u
#define R_PIV       0xAu
#define R_CR        0xBu
#define B_GLOB_STA  0x30u               /* NABM +0x130 */

#define SR_DCH      0x01u               /* halted */
#define SR_CELV     0x02u               /* CIV reached LVI and that buffer is done */
#define SR_LVBCI    0x04u
#define SR_BCIS     0x08u
#define SR_FIFOE    0x10u
#define SR_W1C      (SR_LVBCI | SR_BCIS | SR_FIFOE)
#define CR_RPBM     0x01u
#define CR_RR       0x02u
#define CR_LVBIE    0x04u
#define CR_FEIE     0x08u
#define CR_IOCE     0x10u
#define BD_IOC      0x8000u

#define GS_ANALOG   0x40u               /* GLOB_STA bits DirectSound's ISR tests per channel (its table 0x00300454) */
#define GS_DIGITAL  0x10u
#define ACI_VECTOR  6u

static const unsigned s_ch[2] = { CH_ANALOG, CH_DIGITAL };
static const unsigned s_gs[2] = { GS_ANALOG, GS_DIGITAL };

static uint16_t rd16(volatile const uint8_t *p) { uint16_t v; memcpy(&v, (const void *)p, 2); return v; }
static uint32_t rd32(volatile const uint8_t *p) { uint32_t v; memcpy(&v, (const void *)p, 4); return v; }
static void wr16(volatile uint8_t *p, uint16_t v) { memcpy((void *)p, &v, 2); }
static void wr32(volatile uint8_t *p, uint32_t v) { memcpy((void *)p, &v, 4); }

/* Counters for the 60-s report (written by the model under the trap's lock, read by the frame thread). */
static volatile LONG s_buffers[2], s_peak[2], s_raises;

static int channel_irq(volatile const uint8_t *blk, unsigned ch)
{
    unsigned sr = rd16(blk + ch + R_SR), cr = blk[ch + R_CR];
    return ((sr & SR_BCIS) && (cr & CR_IOCE)) || ((sr & SR_LVBCI) && (cr & CR_LVBIE)) ||
           ((sr & SR_FIFOE) && (cr & CR_FEIE));
}

/* GLOB_STA's channel bits follow the channels' pending interrupts; the line is up while one is set. */
static int update_glob(volatile uint8_t *blk)
{
    uint32_t g = rd32(blk + B_GLOB_STA) & ~(GS_ANALOG | GS_DIGITAL);
    for (int c = 0; c < 2; c++)
        if (channel_irq(blk, s_ch[c]))
            g |= s_gs[c];
    wr32(blk + B_GLOB_STA, g);
    return (g & (GS_ANALOG | GS_DIGITAL)) != 0;
}

int mcpx_aci_block_after_write(volatile uint8_t *blk, const uint8_t *before, unsigned off)
{
    for (int c = 0; c < 2; c++) {
        unsigned ch = s_ch[c];
        uint16_t sr_old = rd16(before + ch + R_SR), sr_new = rd16(blk + ch + R_SR);
        /* SR written (a word at +6, as DirectSound acknowledges; a byte at +6/+7): its status bits clear on 1,
         * and writing back the value just read is the usual acknowledgement - so it is the write's address that
         * counts, not whether the value changed. DCH/CELV are the controller's. Any other write leaves SR. */
        if (off == ch + R_SR || off == ch + R_SR + 1u)
            wr16(blk + ch + R_SR, (uint16_t)(sr_old & ~(sr_new & SR_W1C)));
        else
            wr16(blk + ch + R_SR, sr_old);
        blk[ch + R_CIV] = before[ch + R_CIV];   /* read-only */
        wr16(blk + ch + R_PICB, rd16(before + ch + R_PICB));
        blk[ch + R_PIV] = before[ch + R_PIV];
        if (blk[ch + R_CR] & CR_RR) {           /* reset (the trap clears RR itself afterwards) */
            blk[ch + R_CIV] = 0;
            blk[ch + R_LVI] = 0;
            blk[ch + R_PIV] = 0;
            wr16(blk + ch + R_PICB, 0);
            wr16(blk + ch + R_SR, SR_DCH);
            blk[ch + R_CR] = (uint8_t)(blk[ch + R_CR] & ~CR_RPBM);
        }
    }
    {   /* GLOB_STA: the channel bits are derived, the rest (codec ready 0x100) is kept */
        uint32_t g_old = rd32(before + B_GLOB_STA);
        wr32(blk + B_GLOB_STA, g_old);
    }
    return update_glob(blk);
}

/* Descriptor CIV of a channel: buffer address, length (16-bit samples) and flags; zeros without guest memory. */
static void desc(volatile const uint8_t *blk, unsigned ch, mcpx_aci_mem_fn mem, uint32_t *addr, unsigned *len,
                 unsigned *flags)
{
    uint32_t bdbar = rd32(blk + ch + R_BDBAR), lf = 0;
    const int16_t *d = mem ? mem(bdbar + (blk[ch + R_CIV] & 31u) * 8u, 8u) : NULL;
    *addr = 0;
    if (d) {
        memcpy(addr, d, 4);
        memcpy(&lf, (const uint8_t *)d + 4, 4);
    }
    *len = lf & 0xFFFFu;
    *flags = lf >> 16;
}

/* Starts buffer CIV: PICB = its length, PIV = the next index. */
static void load_desc(volatile uint8_t *blk, unsigned ch, mcpx_aci_mem_fn mem)
{
    uint32_t addr;
    unsigned len, flags;
    desc(blk, ch, mem, &addr, &len, &flags);
    wr16(blk + ch + R_PICB, (uint16_t)len);
    blk[ch + R_PIV] = (uint8_t)((blk[ch + R_CIV] + 1u) & 31u);
}

int mcpx_aci_block_play(volatile uint8_t *blk, unsigned frames, mcpx_aci_mem_fn mem)
{
    for (int c = 0; c < 2; c++) {
        unsigned ch = s_ch[c], sr = rd16(blk + ch + R_SR), budget = frames * 2u, guard = 0;
        if (!(blk[ch + R_CR] & CR_RPBM))
            continue;
        if (sr & SR_DCH) {
            if ((sr & SR_CELV) && blk[ch + R_LVI] == blk[ch + R_CIV])
                continue;                       /* halted at the last valid buffer, no new one yet */
            if (sr & SR_CELV)                   /* a new LVI: go on with the next buffer */
                blk[ch + R_CIV] = (uint8_t)((blk[ch + R_CIV] + 1u) & 31u);
            sr &= ~(SR_DCH | SR_CELV);
            load_desc(blk, ch, mem);
        }
        /* budget: 16-bit samples, two per stereo frame */
        while (budget && guard++ < 64) {
            uint32_t addr;
            unsigned len, flags, picb = rd16(blk + ch + R_PICB), take = picb < budget ? picb : budget;
            desc(blk, ch, mem, &addr, &len, &flags);
            if (take && addr && picb <= len) {  /* what is in the buffer: its peak, for the report */
                const int16_t *s = mem(addr + (len - picb) * 2u, take * 2u);
                LONG pk = s_peak[c];
                for (unsigned i = 0; s && i < take; i++) {
                    LONG v = abs(s[i]);
                    if (v > pk) pk = v;
                }
                s_peak[c] = pk;
            }
            picb -= take;
            budget -= take;
            wr16(blk + ch + R_PICB, (uint16_t)picb);
            if (picb)
                break;
            if (flags & BD_IOC)                 /* buffer done */
                sr |= SR_BCIS;
            InterlockedIncrement(&s_buffers[c]);
            if (blk[ch + R_CIV] == blk[ch + R_LVI]) {
                sr |= SR_LVBCI | SR_CELV | SR_DCH;
                break;
            }
            blk[ch + R_CIV] = (uint8_t)((blk[ch + R_CIV] + 1u) & 31u);
            load_desc(blk, ch, mem);
        }
        wr16(blk + ch + R_SR, (uint16_t)sr);
    }
    return update_glob(blk);
}

/* ── runtime wiring: the AC'97 write trap in xbox_memory_layout.c ────────────────────────────────────────── */

extern volatile uint8_t *xbox_ac97_nabm(void);
extern void xbox_ac97_host_begin(void);
extern void xbox_ac97_host_end(void);
extern void (*g_ac97_after_write)(volatile uint8_t *nabm, const uint8_t *before_100, uint32_t off);
extern void xbox_set_irq_line(uint32_t vector, int level);
extern uint8_t *mcpx_apu_phys(uint64_t addr);

static int s_attached;
static int s_line;

static const int16_t *guest_mem(uint32_t phys, uint32_t bytes)
{
    (void)bytes;
    return phys ? (const int16_t *)mcpx_apu_phys(phys) : NULL;
}

static void set_line(int level)
{
    if (level && !s_line)
        InterlockedIncrement(&s_raises);
    s_line = level;
    xbox_set_irq_line(ACI_VECTOR, level);
}

static void after_write(volatile uint8_t *nabm, const uint8_t *before_100, uint32_t off)
{
    if (off < 0x100u || off >= 0x180u)
        return;                                 /* codec registers etc.: not the bus master's */
    set_line(mcpx_aci_block_after_write(nabm + 0x100, before_100, off - 0x100u));
}

void mcpx_aci_attach(void)
{
    const char *e = getenv("RECOMP_AC97_MODEL");
    if (s_attached || (e && *e == '0') || !xbox_ac97_nabm())
        return;
    g_ac97_after_write = after_write;
    s_attached = 1;
    fprintf(stderr, "[AC97] channel model attached: analog +0x110, digital +0x170, interrupt vector %u"
            " (RECOMP_AC97_MODEL=0 turns it off)\n", ACI_VECTOR);
}

void mcpx_aci_tick(unsigned frames)
{
    int level;
    if (!s_attached)
        return;
    xbox_ac97_host_begin();
    level = mcpx_aci_block_play(xbox_ac97_nabm() + 0x100, frames, guest_mem);
    xbox_ac97_host_end();
    set_line(level);
}

int mcpx_aci_report(char *buf, int size)
{
    if (!s_attached)
        return 0;
    return snprintf(buf, (size_t)size, "[AC97] last 60 s: analog %ld buffers (peak %ld), digital %ld buffers"
                    " (peak %ld), vector 6 raised %ld times", (long)InterlockedExchange(&s_buffers[0], 0),
                    (long)InterlockedExchange(&s_peak[0], 0), (long)InterlockedExchange(&s_buffers[1], 0),
                    (long)InterlockedExchange(&s_peak[1], 0), (long)InterlockedExchange(&s_raises, 0));
}
