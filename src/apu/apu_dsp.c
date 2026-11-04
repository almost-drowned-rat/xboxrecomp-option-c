/*
 * MCPX APU DSP (GP/EP) - Stub implementation
 *
 * The DSP Global Processor (GP) and Encode Processor (EP) handle effects
 * processing (reverb, chorus, etc.) and final output encoding. The full
 * DSP is ~3000 lines of DSP56300 emulation code.
 *
 * For initial audio, we bypass the DSP entirely:
 * - VP mixbins are passed directly to the EP output
 * - GP effects processing is skipped
 * - The EP just copies mixbin 0/1 (front L/R) to the monitor buffer
 *
 * This gives us basic voice playback without effects. The DSP can be
 * connected later for reverb, EQ, and other processing.
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2019-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "apu_state.h"
#include "fpconv.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── DSP command doorbell acknowledgement ────────────────────────────────
 *
 * DirectSound does not stop at creating the device. It hands the audio DSP a
 * command block in guest RAM, writes a command word, and spins until the DSP
 * writes zero back. On real hardware the GP runs a DSP56300 program that does
 * that. Here the DSP is a passthrough stub, so the word never changes and the
 * title hangs inside DirectSound initialisation -- which on Wreckless gates the
 * entire engine, not just audio.
 *
 * RECOMP_APU_DSP_ACK=<addr>[,<addr>...] clears those guest dwords once per APU
 * frame, which is what "the command completed" looks like to the title.
 *
 * ponytail: this is a handshake acknowledgement, not a DSP. It says every
 * command succeeded instantly and computes nothing, so anything whose *result*
 * the title reads back will still be wrong. The real fix is DSP56300 emulation
 * in the GP/EP; this exists so audio init stops blocking everything behind it.
 *
 * An explicit address has to be observed: run with RECOMP_WATCHDOG_SECS and
 * the spin shows up as ebx plus the poll offset. It is not inside the ranges
 * GPSADDR/GPFADDR/EPSADDR/EPFADDR hold (Wreckless: 0x01504000 / 0x014EC000 /
 * 0x0151C000 / 0x014F0000, doorbell 0x014F8810), and it moves when the
 * allocator places the block elsewhere.
 *
 * RECOMP_APU_DSP_ACK=auto finds it without that: GPSADDR is not the scratch
 * space but the scatter-gather table that maps it, and the XDK DirectSound GP
 * program takes its commands in the first scratch page - the page SGE entry 0
 * names - at offset 0x810. So the doorbell is SGE[0] + 0x810 (physical),
 * wherever the contiguous allocator put that page (checked on Burnout 3 US:
 * GPSADDR 0x02600000, SGE[0] page 0x025F4000; the same rule is reported for
 * BLiNX 2 and Wreckless, not checked here). It is re-read every poll,
 * so a title that reprograms GPSADDR or frees and reallocates the page is
 * followed. Nothing is acknowledged before GPSADDR and its entry 0 are set.
 */
#define APU_DSP_ACK_MAX 8
#define APU_DSP_DOORBELL_OFFSET 0x810u
static uint32_t s_dsp_ack[APU_DSP_ACK_MAX];
static int s_dsp_ack_count = -1;
static int s_dsp_ack_auto;
static uint32_t s_dsp_auto_bell;

uint32_t mcpx_apu_dsp_auto_doorbell(MCPXAPUState *d)
{
    uint32_t sge = qatomic_read(&d->regs[NV_PAPU_GPSADDR]);
    uint32_t page;

    if (!sge)
        return 0;
    page = ldl_le_phys(address_space_memory, sge) & ~(uint32_t)(TARGET_PAGE_SIZE - 1);
    return page ? page + APU_DSP_DOORBELL_OFFSET : 0;
}

static void dsp_ack_init(void)
{
    const char *spec = getenv("RECOMP_APU_DSP_ACK");
    char buf[256], *p, *end;

    s_dsp_ack_count = 0;
    if (!spec || !*spec)
        return;
    if (!strcmp(spec, "auto")) {
        s_dsp_ack_auto = 1;
        fprintf(stderr, "[APU] DSP doorbell ack: auto (GPSADDR SGE[0] + 0x%X)\n",
                APU_DSP_DOORBELL_OFFSET);
        return;
    }
    strncpy(buf, spec, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    for (p = buf; *p && s_dsp_ack_count < APU_DSP_ACK_MAX; ) {
        unsigned long v = strtoul(p, &end, 0);
        if (end == p)
            break;
        if (v)
            s_dsp_ack[s_dsp_ack_count++] = (uint32_t)v;
        p = (*end == ',') ? end + 1 : end;
    }
    if (s_dsp_ack_count)
        fprintf(stderr, "[APU] DSP doorbell ack: %d address(es), first 0x%08X\n",
                s_dsp_ack_count, s_dsp_ack[0]);
}

/* SUM EVERY MIXBIN THE GUEST ROUTED TO, NOT JUST THE FIRST TWO.
 *
 * Default ON. RECOMP_APU_MIXDOWN_ALL=0 restores the previous two-bin read,
 * because this changes audible output for every title and an escape hatch
 * costs one branch. */
static int mcpx_apu_mixdown_all(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_APU_MIXDOWN_ALL");
        on = (e && *e) ? (atoi(e) != 0) : 1;
    }
    return on;
}

/* ── the title's GP effects image, modelled (apu_gp.h) ───────────────────
 *
 * From Takedawn's toolkit (xenophobentx/xboxrecomp, MIT; its src/apu/apu_dsp.c, f7b6368): a title that wired its
 * image (apu_gp_direct / _eq / _drive / _export / _program, before the APU starts) gets its speaker feeds from the
 * model instead of the all-bins mixdown below. Its commands load the model's effect states as they load the DSP's,
 * and the model's export lands in the GP scratch memory the GP program writes, so the title's own return voices
 * play it back (apu_vp.c, voice_plays_gp_export). Nothing wired: nothing changes. */
#include "apu_gp.h"

/* A dword of GP scratch, through the GPSADDR scatter-gather table (8 bytes an entry, the page's physical address
 * first). 0 with the table not set up yet. */
static uint32_t gp_scratch_word(MCPXAPUState *d, uint32_t off)
{
    uint32_t sge = qatomic_read(&d->regs[NV_PAPU_GPSADDR]), page;
    if (!sge || !d->ram_ptr || (off >> 12) > d->regs[NV_PAPU_GPSMAXSGE])
        return 0;
    page = *(const uint32_t *)mcpx_apu_phys(sge + (off >> 12) * 8);
    if (!page || (page & 0xFFF))
        return 0;
    return *(const uint32_t *)(mcpx_apu_phys(page) + (off & 0xFFF));
}

static uint32_t gp_mem_read(void *ctx, uint32_t off)
{
    return gp_scratch_word((MCPXAPUState *)ctx, off);
}

/* n dwords into GP scratch at off, a page at a time; pages the table does not map yet are skipped. */
static void gp_mem_write(void *ctx, uint32_t off, const uint32_t *words, int n)
{
    MCPXAPUState *d = (MCPXAPUState *)ctx;
    uint32_t sge = qatomic_read(&d->regs[NV_PAPU_GPSADDR]);

    if (!sge || !d->ram_ptr)
        return;
    while (n > 0) {
        uint32_t in_page = (0x1000u - (off & 0xFFF)) / 4u, page;
        int k = n < (int)in_page ? n : (int)in_page;
        if ((off >> 12) > d->regs[NV_PAPU_GPSMAXSGE])
            return;
        page = *(const uint32_t *)mcpx_apu_phys(sge + (off >> 12) * 8);
        if (page && !(page & 0xFFF))
            memcpy(mcpx_apu_phys(page) + (off & 0xFFF), words, (size_t)k * 4u);
        off += (uint32_t)k * 4u;
        words += k;
        n -= k;
    }
}

/* The guest's view of the GP's X memory (apu_gp_mem.xwin): the APU's register window from 0x30000, plain RAM in this
 * runtime (xbox_memory_layout.c), a dword an X word. NULL before the memory layout is up. */
extern void *xbox_McpxWindow(void);

static uint32_t *gp_xwin(void)
{
    uint8_t *w = (uint8_t *)xbox_McpxWindow();
    return w ? (uint32_t *)(w + 0x30000) : NULL;
}

/* ── opt-in diagnostics for wiring a title's GP image ────────────────────
 *
 * RECOMP_APU_BINSTATS=1: every 10 s, the level each mixbin carried (RMS and
 * peak, dB full scale) as the voice processor left it, before the GP or the
 * fallback mixdown. Which bins a title feeds, and how loud, is the first
 * thing to know before its image is wired (apu_gp.h): a title whose music
 * sits in bins 0/1 and whose effects also reach crosstalk and effect sends
 * is mixed louder on the effects side by the all-bins fallback below.
 *
 * RECOMP_APU_GP_DUMP=<path>[,<seconds>]: once, that many seconds of audio
 * after the first frame (default 30), GP scratch memory (every page the
 * GPSADDR table maps) to <path>.scratch.bin and the GP's DSP memory window
 * (X, mixbuffer, Y, P: 0x30000..0x3DFFF of the APU window) to <path>.dsp.bin.
 * DirectSound has the title's effects image (dsstdfx.bin is shipped
 * encrypted) unpacked there, with its state blocks and effect table: what a
 * port's gp_image.c is read from (Burnout 3's was, by Takedawn).
 *
 * Both off by default; frame thread, no gate held. */
static void apu_bin_diag(MCPXAPUState *d, float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    static int on = -1;
    static double sq[NUM_MIXBINS];
    static float peak[NUM_MIXBINS];
    static uint32_t frames;
    static uint32_t dump_at;
    static char dump_path[512];

    if (on < 0) {
        const char *e = getenv("RECOMP_APU_BINSTATS");
        const char *p = getenv("RECOMP_APU_GP_DUMP");
        on = (e && *e && *e != '0') ? 1 : 0;
        if (p && *p) {
            const char *comma = strchr(p, ',');
            size_t n = comma ? (size_t)(comma - p) : strlen(p);
            int secs = comma ? atoi(comma + 1) : 30;
            if (n >= sizeof dump_path - 16)
                n = sizeof dump_path - 16;
            memcpy(dump_path, p, n);
            dump_path[n] = 0;
            dump_at = (uint32_t)(secs > 0 ? secs : 30) * (48000u / NUM_SAMPLES_PER_FRAME);
            on |= 2;
        }
    }
    if (!on)
        return;
    frames++;
    if (on & 1) {
        for (int b = 0; b < NUM_MIXBINS; b++)
            for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
                float v = mixbins[b][i], a = v < 0 ? -v : v;
                sq[b] += (double)v * v;
                if (a > peak[b])
                    peak[b] = a;
            }
        if (frames % (10u * 48000u / NUM_SAMPLES_PER_FRAME) == 0) {
            char line[1024];
            int len = 0;
            double n = 10.0 * 48000.0;
            for (int b = 0; b < NUM_MIXBINS && len < (int)sizeof line - 40; b++) {
                if (peak[b] <= 0.0f)
                    continue;
                double rms = sqrt(sq[b] / n);
                len += snprintf(line + len, sizeof line - (size_t)len, " %d:%.1f/%.1f h%u", b,
                                rms > 1e-9 ? 20.0 * log10(rms) : -180.0, 20.0 * log10((double)peak[b]),
                                (unsigned)d->vp.submix_headroom[b]);
                sq[b] = 0.0;
                peak[b] = 0.0f;
            }
            line[len] = 0;
            fprintf(stderr, "[APU-BINS] t=%us hrtf headroom %u, bin:rms/peak dBFS h<submix headroom>%s\n",
                    (unsigned)(frames / (48000u / NUM_SAMPLES_PER_FRAME)), (unsigned)d->vp.hrtf_headroom,
                    len ? line : " (silent)");
        }
    }
    if ((on & 2) && dump_at && frames == dump_at) {
        char path[528];
        FILE *f;
        uint32_t pages = d->regs[NV_PAPU_GPSMAXSGE] + 1u, words = 0;
        uint8_t *w = (uint8_t *)xbox_McpxWindow();

        snprintf(path, sizeof path, "%s.scratch.bin", dump_path);
        f = fopen(path, "wb");
        if (f) {
            for (uint32_t off = 0; off < pages * 0x1000u; off += 4, words++) {
                uint32_t v = gp_scratch_word(d, off);
                fwrite(&v, 4, 1, f);
            }
            fclose(f);
        }
        snprintf(path, sizeof path, "%s.dsp.bin", dump_path);
        f = w ? fopen(path, "wb") : NULL;
        if (f) {
            fwrite(w + 0x30000, 1, 0xE000, f);
            fclose(f);
        }
        fprintf(stderr, "[APU-GPDUMP] %s: scratch %u pages (%u words), dsp window %s\n", dump_path,
                (unsigned)pages, (unsigned)words, w ? "0x30000..0x3DFFF" : "not mapped");
    }
}

/* The doorbell whose command the model has already taken at the start of a frame, so the acknowledgement does not
 * load it again. */
static const volatile uint32_t *s_gp_taken;

/* A command about to be acknowledged: its block is the dwords before the doorbell (GP scratch 0x800..0x80C). */
static void gp_command(MCPXAPUState *d, const volatile uint32_t *doorbell)
{
    apu_gp_mem m = { gp_mem_read, gp_mem_write, d, gp_xwin() };
    if (s_gp_taken == doorbell) {
        s_gp_taken = NULL;
        return;
    }
    if (apu_gp_wired())
        apu_gp_command(&m, doorbell[0], doorbell[-4], doorbell[-2], doorbell[-1]);
}

/* A command posted since the last acknowledgement poll: the GP program looks at its doorbell when its frame starts,
 * so a command the title wrote just before this frame takes effect in it. The acknowledgement stays with the poll. */
static void gp_command_at_frame(MCPXAPUState *d)
{
    if (s_dsp_ack_count < 0 || !d->ram_ptr || s_gp_taken)
        return;
    if (s_dsp_ack_auto) {
        uint32_t bell = mcpx_apu_dsp_auto_doorbell(d);
        const volatile uint32_t *slot = bell ? (const volatile uint32_t *)mcpx_apu_phys(bell) : NULL;
        if (slot && *slot) {
            gp_command(d, slot);
            s_gp_taken = slot;
        }
        return;
    }
    for (int i = 0; i < s_dsp_ack_count; i++) {
        const volatile uint32_t *slot = (const volatile uint32_t *)(d->ram_ptr + s_dsp_ack[i]);
        if (*slot) {
            gp_command(d, slot);
            s_gp_taken = slot;
            return;
        }
    }
}

/* pa, a voice buffer's physical address, in the block the GP model exports (apu_vp.c: such a voice plays the
 * model's output): 1 + the mixbin whose slot it starts in, 0 when it is not in the block. */
int mcpx_apu_gp_export_buffer(MCPXAPUState *d, uint32_t pa)
{
    uint32_t off, bytes, sge, page;
    int first;

    if (!apu_gp_export_range(&off, &bytes, &first))
        return 0;
    sge = qatomic_read(&d->regs[NV_PAPU_GPSADDR]);
    if (!sge || !d->ram_ptr)
        return 0;
    for (uint32_t o = off & ~0xFFFu; o < off + bytes; o += 0x1000u) {
        uint32_t lo = o < off ? off : o, hi = o + 0x1000u;
        if (hi > off + bytes) hi = off + bytes;
        if ((o >> 12) > d->regs[NV_PAPU_GPSMAXSGE])
            break;
        page = *(const uint32_t *)mcpx_apu_phys(sge + (o >> 12) * 8);
        if (page && !(page & 0xFFF) && pa >= page + (lo & 0xFFF) &&
            pa < page + (hi - o))
            return 1 + first + (int)((o + (pa - page) - off) / (NUM_SAMPLES_PER_FRAME * 4u));
    }
    return 0;
}

void mcpx_apu_dsp_ack_poll(MCPXAPUState *d)
{
    int i;

    if (s_dsp_ack_count < 0)
        dsp_ack_init();
    if (!d->ram_ptr)
        return;
    for (i = 0; i < s_dsp_ack_count; i++) {
        uint32_t *slot = (uint32_t *)(d->ram_ptr + s_dsp_ack[i]);
        if (*slot) {
            static int shown[APU_DSP_ACK_MAX];
            if (shown[i]++ < 3)
                fprintf(stderr, "[APU] DSP doorbell 0x%08X: command 0x%08X"
                                " acknowledged\n", s_dsp_ack[i], *slot);
            gp_command(d, slot);
            *slot = 0;
        }
    }
    if (s_dsp_ack_auto) {
        static int shown;
        uint32_t bell = mcpx_apu_dsp_auto_doorbell(d);
        volatile uint32_t *slot;
        uint32_t cmd;

        if (!bell)
            return;
        if (bell != s_dsp_auto_bell) {
            fprintf(stderr, "[APU] DSP doorbell at 0x%08X (GPSADDR 0x%08X, SGE[0] page 0x%08X)\n",
                    bell, qatomic_read(&d->regs[NV_PAPU_GPSADDR]), bell - APU_DSP_DOORBELL_OFFSET);
            s_dsp_auto_bell = bell;
        }
        slot = (volatile uint32_t *)mcpx_apu_phys(bell);
        cmd = *slot;
        if (cmd) {
            if (shown++ < 3)
                fprintf(stderr, "[APU] DSP doorbell 0x%08X: command 0x%08X acknowledged\n", bell, cmd);
            gp_command(d, slot);
            *slot = 0;
        }
    }
}

void mcpx_apu_dsp_init(MCPXAPUState *d)
{
    /* Allocate minimal DSP state for GP and EP.
     * We need these to exist so reset doesn't crash,
     * but they won't actually run DSP programs. */
    d->gp.dsp = (DSPState *)calloc(1, sizeof(DSPState));
    d->ep.dsp = (DSPState *)calloc(1, sizeof(DSPState));

    if (d->gp.dsp) d->gp.dsp->is_gp = true;
    if (d->ep.dsp) d->ep.dsp->is_gp = false;

    d->gp.realtime = false;
    d->ep.realtime = false;

    fprintf(stderr, "[APU] DSP GP/EP initialized (STUBBED - passthrough mode)\n");
}

void mcpx_apu_update_dsp_preference(MCPXAPUState *d)
{
    /* In the real xemu, this reads settings to decide whether
     * GP/EP should run in realtime or cached mode. We ignore it. */
    (void)d;
}

void mcpx_apu_dsp_frame(MCPXAPUState *d,
                         float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    /* Bypass DSP: take mixbin 0 (front-left) and mixbin 1 (front-right)
     * and write them directly to the monitor frame buffer as the final
     * EP output.
     *
     * The Xbox DirectSound typically routes:
     *   Mixbin 0 = Front Left
     *   Mixbin 1 = Front Right
     *   Mixbin 2 = Center (often unused in stereo)
     *   Mixbin 3 = LFE
     *   Mixbin 4-5 = Rear L/R
     *
     * For stereo output, bins 0 and 1 are what we want.
     */

    int off = (d->ep_frame_div % 8) * NUM_SAMPLES_PER_FRAME;

    apu_bin_diag(d, mixbins);

    /* A title with its GP image wired: the model's six speaker feeds, folded to stereo as the console's analog output
     * is taken to be (ITU-R BS.775: centre and surrounds at -3 dB, no LFE). From Takedawn's apu_dsp.c. */
    if (apu_gp_wired() && d->monitor.point != MCPX_APU_DEBUG_MON_VP) {
        enum { FL, FR, FC, LFE, SL, SR };
        apu_gp_mem m = { gp_mem_read, gp_mem_write, d, gp_xwin() };
        float acc[APU_GP_SPEAKERS][NUM_SAMPLES_PER_FRAME];

        gp_command_at_frame(d);
        apu_gp_frame(&m, mixbins, acc);
        for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
            float c = 0.70710678f * acc[FC][i];
            float left = acc[FL][i] + c + 0.70710678f * acc[SL][i];
            float right = acc[FR][i] + c + 0.70710678f * acc[SR][i];
            if (left > 1.0f) left = 1.0f;
            if (left < -1.0f) left = -1.0f;
            if (right > 1.0f) right = 1.0f;
            if (right < -1.0f) right = -1.0f;
            d->monitor.frame_buf[off + i][0] = (int16_t)(left * 32767.0f);
            d->monitor.frame_buf[off + i][1] = (int16_t)(right * 32767.0f);
        }
        g_dbg.gp.cycles = 0;
        g_dbg.ep.cycles = 0;
        return;
    }

    if (d->monitor.point != MCPX_APU_DEBUG_MON_VP) {
        for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
            /* Bins 2..31 used to be computed and then dropped on the floor.
             * On hardware the GP and EP mix the submixes down; here they are
             * stubs, so thirty of thirty-two bins were discarded every frame
             * with no counter anywhere to say so.
             *
             * Measured on Jet Set Radio Future, one 200 s gameplay run, with
             * a positive control moving beside it:
             *
             *     [APU-BIN] 2D heard=557466 lost=0
             *               3D heard=0      lost=377768
             *               lost by bin: 6,7,8,9,10
             *
             * 557,466 music voice-frames heard and none lost; 377,768 effect
             * voice-frames produced correctly and thrown away. The title's 3D
             * positional voices -- its sound effects -- are routed to bins 6
             * to 10 by the guest's own V0BIN..V3BIN, and music on 2D voices
             * lands in bins 0 and 1, which is why the music was always
             * audible and no effect ever was. Every instrument upstream of
             * this line read healthy.
             *
             * Gating the HRTF submix override was tried first and did not fix
             * it, so the defect is the width of this mixdown and nothing else.
             *
             * Even bins left, odd bins right, which preserves the stereo
             * pairing the guest set up -- bins 6/7 and 8/9 arrive with matched
             * counts. This is not what a real EP does; it is the cheapest
             * mixdown that stops discarding audio. */
            float left, right;
            if (mcpx_apu_mixdown_all()) {
                left = 0.0f;
                right = 0.0f;
                for (int b = 0; b < NUM_MIXBINS; ++b) {
                    if (b & 1) right += mixbins[b][i];
                    else       left  += mixbins[b][i];
                }
            } else {
                left = mixbins[0][i];
                right = mixbins[1][i];
            }
            /* Clamp to [-1, 1] range */
            if (left > 1.0f) left = 1.0f;
            if (left < -1.0f) left = -1.0f;
            if (right > 1.0f) right = 1.0f;
            if (right < -1.0f) right = -1.0f;

            /* Convert to 16-bit and write (not accumulate) into frame buffer.
             * Each of the 8 sub-frames writes its own 32-sample slice. */
            d->monitor.frame_buf[off + i][0] = (int16_t)(left * 32767.0f);
            d->monitor.frame_buf[off + i][1] = (int16_t)(right * 32767.0f);
        }
    }

    g_dbg.gp.cycles = 0;
    g_dbg.ep.cycles = 0;
}
