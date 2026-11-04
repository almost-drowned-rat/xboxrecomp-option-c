/*
 * MCPX APU Core - Standalone extraction from xemu
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2018-2019 Jannik Vogel
 * Copyright (c) 2019-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "apu_state.h"
#include "apu.h"
#include "apu_xaudio2.h"
#include "apu_aci.h"
#include "fpconv.h"
#include "../kernel/xbox_memory_layout.h"   /* XBOX_WORKER_STACK_TOP */
/* ============================================================
 * Globals
 * ============================================================ */

uint8_t *g_apu_ram_ptr = NULL;

MCPXAPUState *g_state = NULL;

/* Forward declarations for software mixer */
static void mixer_init(void);
static void mixer_render(int16_t frame_buf[][2], int num_samples);
static APUMixerVoice g_mixer_voices[APU_MIXER_MAX_VOICES];
static volatile int g_mixer_active_count = 0;
static CRITICAL_SECTION g_mixer_cs;
static bool g_mixer_initialized = false;
struct McpxApuDebug g_dbg;
struct McpxApuDebug g_dbg_cache;
int g_dbg_voice_monitor = -1;
uint64_t g_dbg_muted_voices[4] = { 0 };

/* Global audio mute — disables all AWD/mixer sound playback */
volatile int g_audio_muted = 0;  /* 0 = audio enabled */

/* ============================================================
 * Debug frame markers (minimal stubs)
 * ============================================================ */

void mcpx_debug_begin_frame(void) {}
void mcpx_debug_end_frame(void) {}

/* ============================================================
 * IRQ handling (stubbed - no PCI bus in standalone)
 * ============================================================ */

/* Physical addresses, resolved the way every other bus master here does it
 * (dma_resolve in nv2a_pb_exec.c, bus_resolve in usb/ohci.c).
 *
 * DirectSound builds its voice, SGE and notifier structures in
 * MmAllocateContiguousMemory and hands the APU their physical addresses.
 * Physical P and the contiguous window's 0x80000000 + P are the same bytes on
 * hardware; here the window is separate storage. Reading every address as low
 * RAM meant the voice processor walked zeroes and wrote each "voice done"
 * notification into ordinary RAM, where DirectSound never looked -- so a
 * buffer never reported that it had stopped, and Burnout 3's frontend waits
 * on exactly that (IDirectSoundBuffer::GetStatus, polled forever).
 *
 * What MmGetPhysicalAddress said a page is comes first (the kernel's owner
 * table, as bus_resolve does it). A contiguous allocation's physical pages
 * can share numbers with the XBE image and with pool RAM, and the rule below
 * alone picked the wrong one both ways in Burnout 3: its streamed voice (the
 * movie and front-end audio) sits in contiguous pages at physical
 * 0x2F0000-0x320000, inside the image range, so the image test read the
 * image's code as samples -- the noise (lag-1 correlation 0.07 against 0.96
 * read from the contiguous window); and early pool buffers below the
 * contiguous high-water mark were read from the window -- silence where
 * the title plays sound (70-110 s into the run). The rule stays for pages
 * nobody asked about. */
extern uint32_t g_xbox_image_lo, g_xbox_image_hi;
extern uint32_t xbox_ContiguousAllocatedBytes(void);
extern int xbox_PhysOwner(uint32_t phys);   /* kernel.h: 0 unknown, 1 RAM, 2 contiguous */

uint8_t *mcpx_apu_phys(uint64_t addr)
{
    uint32_t a = (uint32_t)addr & 0x0FFFFFFFu;
    switch (xbox_PhysOwner(a)) {
    case 1:                                 /* XBOX_PHYS_RAM */
        return g_apu_ram_ptr + (a & 0x03FFFFFFu);
    case 2:                                 /* XBOX_PHYS_CONTIG */
        if (a < xbox_ContiguousAllocatedBytes())
            return g_apu_ram_ptr + 0x80000000u + a;
        break;
    default:
        break;
    }
    if (a >= g_xbox_image_lo && a < g_xbox_image_hi)
        return g_apu_ram_ptr + a;
    if (a < xbox_ContiguousAllocatedBytes())
        return g_apu_ram_ptr + 0x80000000u + a;
    return g_apu_ram_ptr + (a & 0x03FFFFFFu);
}

/* The interrupt line, as the frame thread sees it. update_irq used to call
 * pci_irq_assert, which is an empty stub here, so DirectSound's service
 * routine never ran and no voice completion ever reached it. */
static volatile LONG s_irq_line;
/* Service-routine calls, written to stderr every 60 s by the frame thread: the
 * rate is a few per second; thousands per second mean a status bit that is not
 * being cleared (see update_irq). */ volatile LONG s_irq_delivered;

/* FETINTSTS, the front end's trap interrupt, is latched once where the front
 * end traps (fe_method, SE2FE_IDLE_VOICE) and cleared by an ISTS write like
 * every other status bit; it is not derived from FECTL here.
 *
 * It used to be ORed back in on every call while FECTL's method mode was not
 * free running (the mask matches HALTED as well). DirectSound's service routine
 * acknowledges ISTS first and resumes the front end afterwards, so its own
 * acknowledgement re-latched the bit, and the line stayed up with the trap
 * reason of a trap already handled: ISTS 0x11 before and after most
 * deliveries, ~3,450 deliveries/s instead of a few per second, ~10 points of
 * CPU. A later delivery replayed FEDECMETH/FEDECPARAM - the idle voice of the
 * old trap - after the title had freed that voice, and the voice-done handler
 * read its empty slot: Burnout 3's crash ~100 s into the front end
 * (sub_002F8644, [0+0x64], 4 of 4 runs before, 0 of 8 after). Takedawn's
 * toolkit found and fixed the same replay. */
static void update_irq(MCPXAPUState *d)
{
    if ((d->regs[NV_PAPU_IEN] & NV_PAPU_ISTS_GINTSTS) &&
        ((d->regs[NV_PAPU_ISTS] & ~NV_PAPU_ISTS_GINTSTS) &
         d->regs[NV_PAPU_IEN])) {
        qatomic_or(&d->regs[NV_PAPU_ISTS], NV_PAPU_ISTS_GINTSTS);
        InterlockedExchange(&s_irq_line, 1);
    } else {
        qatomic_and(&d->regs[NV_PAPU_ISTS], ~NV_PAPU_ISTS_GINTSTS);
        InterlockedExchange(&s_irq_line, 0);
    }
}

/* ---- delivering it ---------------------------------------------------- */

/* HalGetInterruptVector(5) -- the APU's IRQ -- is what DirectSound connects
 * its service routine to (0x002F8E81 in Burnout 3). */
#define APU_VECTOR 5

typedef recomp_func_t apu_guest_fn;      /* void(RECOMP_PARAMS), xbox_memory_layout.h */
extern apu_guest_fn recomp_lookup(uint32_t xbox_va);
extern int  xbox_worker_stack_alloc(void);
extern void xbox_worker_stack_free(int slot);
extern uint32_t xbox_GetConnectedInterrupt(uint32_t vector);
extern uint32_t xbox_AllocThreadTib(void);
extern int xbox_IrqlBlocksInterrupts(void);
extern int xbox_GateEnabled(void);
extern int xbox_GateTryEnter(void);
extern void xbox_GateLeave(void);
extern void xbox_GateNote(uint32_t routine);
extern int xbox_IrqlEnterInterrupt(X86Ctx *ctx, int level);
extern void xbox_IrqlLeaveInterrupt(X86Ctx *ctx, int saved);
#if defined(_MSC_VER)
#  define APU_TLS __declspec(thread)
#else
#  define APU_TLS __thread
#endif

/* Call the connected service routine while the line is up, from the frame
 * thread, the way the OHCI model delivers USB interrupts (ohci_call_isr):
 * a worker stack for the call, a TIB of this thread's own, and a hold-off
 * while a guest thread sits at raised IRQL, which is when the single-CPU
 * console could not have taken the interrupt. The routine acknowledges by
 * writing ISTS, which drops the line through update_irq. */
static void apu_deliver_irq(X86Ctx *ctx, MCPXAPUState *d)
{
    static int tib_ready;
    static unsigned held_off;
    uint32_t kint, routine, context;
    apu_guest_fn fn;
    int slot;

    if (!InterlockedCompareExchange(&s_irq_line, 0, 0))
        return;
    if (!xbox_GateEnabled() && xbox_IrqlBlocksInterrupts() && ++held_off <= 50)
        return;                                 /* without the gate (RECOMP_DPC_GATE=0): the old hold-off */
    held_off = 0;
    kint = xbox_GetConnectedInterrupt(APU_VECTOR);
    if (!kint)
        return;
    routine = *(uint32_t *)(g_apu_ram_ptr + kint + 0);
    context = *(uint32_t *)(g_apu_ram_ptr + kint + 4);
    fn = routine ? recomp_lookup(routine) : NULL;
    if (!fn)
        return;
    if (!tib_ready) {
        uint32_t tib = xbox_AllocThreadTib();
        if (!tib)
            return;
        g_fs_base = tib;
        tib_ready = 1;
    }
    slot = xbox_worker_stack_alloc();
    if (slot < 0)
        return;
    /* The one-CPU gate (kernel_hal.c): not while a guest thread holds DISPATCH_LEVEL; the line stays up and the
     * next frame delivers. */
    if (!xbox_GateTryEnter()) {
        xbox_worker_stack_free(slot);
        return;
    }
    xbox_GateNote(routine);

    qemu_mutex_unlock(&d->lock);
    g_esp = XBOX_WORKER_STACK_TOP(slot);
    g_eax = g_ecx = g_edx = g_ebx = g_esi = g_edi = 0;
    g_esp -= 4; *(uint32_t *)(g_apu_ram_ptr + g_esp) = context;
    g_esp -= 4; *(uint32_t *)(g_apu_ram_ptr + g_esp) = kint;
    g_esp -= 4; *(uint32_t *)(g_apu_ram_ptr + g_esp) = 0xDEADBEEFu;
    { int _irql = xbox_IrqlEnterInterrupt(ctx, 16); RECOMP_CALL_GUEST(fn); xbox_IrqlLeaveInterrupt(ctx, _irql); }
    InterlockedIncrement(&s_irq_delivered);
    xbox_worker_stack_free(slot);
    xbox_GateLeave();
    qemu_mutex_lock(&d->lock);

    {
        static unsigned n;
        if (n++ < 3) {
            fprintf(stderr, "[APU] interrupt delivered to 0x%08X -> %s\n",
                    routine, (g_eax & 1) ? "claimed" : "declined");
            fflush(stderr);
        }
    }
}

/* ============================================================
 * MMIO Read / Write
 * ============================================================ */

uint64_t mcpx_apu_read(void *opaque, hwaddr addr, unsigned int size)
{
    MCPXAPUState *d = (MCPXAPUState *)opaque;
    uint64_t r = 0;

    switch (addr) {
    case NV_PAPU_XGSCNT:
        r = (uint64_t)(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 100);
        break;
    default:
        if (addr < 0x20000) {
            r = qatomic_read(&d->regs[addr]);
        }
        break;
    }

    /* Uncomment for register tracing:
     * fprintf(stderr, "[APU] read  [0x%05llX] size=%u -> 0x%08llX\n",
     *         (unsigned long long)addr, size, (unsigned long long)r);
     */
    (void)size;
    return r;
}

void mcpx_apu_write(void *opaque, hwaddr addr, uint64_t val,
                     unsigned int size)
{
    MCPXAPUState *d = (MCPXAPUState *)opaque;

    /* Uncomment for register tracing:
     * fprintf(stderr, "[APU] write [0x%05llX] size=%u <- 0x%08llX\n",
     *         (unsigned long long)addr, size, (unsigned long long)val);
     */
    (void)size;

    switch (addr) {
    case NV_PAPU_ISTS:
        qatomic_and(&d->regs[NV_PAPU_ISTS], ~(uint32_t)val);
        update_irq(d);
        qemu_cond_broadcast(&d->cond);
        break;
    case NV_PAPU_FECTL:
    case NV_PAPU_SECTL:
        qatomic_set(&d->regs[addr], (uint32_t)val);
        /* Starting the APU has to start the frame thread.
         *
         * The thread idles on pause_requested, which init sets and only the
         * test tone ever cleared -- so a title that enabled the APU through
         * these registers got an APU that stayed asleep. Nothing then advanced
         * the front end, and a title waiting on a notify completion (the
         * FEMEMDATA magic write, which is how completion reaches guest memory)
         * waited forever. Wreckless hangs exactly there during DirectSound
         * init, and because it initialises its whole engine behind a
         * successful DirectSound create, that hang is not confined to audio.
         *
         * Resume whenever the write is not switching the block off; the thread
         * re-checks FECTL itself and idles again if it is halted or trapped. */
        {
            uint32_t sectl = qatomic_read(&d->regs[NV_PAPU_SECTL]);
            uint32_t fectl = qatomic_read(&d->regs[NV_PAPU_FECTL]);
            bool running =
                ((sectl & NV_PAPU_SECTL_XCNTMODE) != NV_PAPU_SECTL_XCNTMODE_OFF)
                && ((fectl & NV_PAPU_FECTL_FEMETHMODE)
                    != NV_PAPU_FECTL_FEMETHMODE_HALTED);
            if (running && d->pause_requested) {
                d->pause_requested = false;
                fprintf(stderr, "[APU] started by the title"
                                " (SECTL=%08X FECTL=%08X)\n", sectl, fectl);
            }
        }
        qemu_cond_broadcast(&d->cond);
        break;
    case NV_PAPU_FEMEMDATA:
        /* 'magic write' - value written to FEMEMADDR on notify completion */
        stl_le_phys(address_space_memory, d->regs[NV_PAPU_FEMEMADDR], (uint32_t)val);
        qatomic_set(&d->regs[addr], (uint32_t)val);
        break;
    default:
        if (addr < 0x20000) {
            qatomic_set(&d->regs[addr], (uint32_t)val);
        }
        break;
    }
}

/* ============================================================
 * Test tone state (used by monitor and test tone functions)
 * ============================================================ */

static struct {
    bool active;
    double phase;
    double phase_inc;
    int16_t amplitude;
} g_test_tone = { false, 0.0, 0.0, 0 };

/* ============================================================
 * Monitor - Audio output (XAudio2 primary, waveOut fallback)
 * ============================================================ */

#if defined(_WIN32)
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif
/* On Linux, waveOut* are inert stubs from win32_compat.h: the APU's
 * waveOut fallback path stays inactive and never produces audio. */

/* Ring of waveOut buffers for double-buffering */
#define WAVEOUT_NUM_BUFS 4
#define WAVEOUT_BUF_SAMPLES 2048  /* ~42.7ms at 48kHz, matches 8-frame delivery rate */
#define MIXER_FRAME_SAMPLES 256  /* Internal mixing frame size (matches frame_buf) */

typedef struct {
    HWAVEOUT hwo;
    WAVEHDR  hdrs[WAVEOUT_NUM_BUFS];
    int16_t  bufs[WAVEOUT_NUM_BUFS][WAVEOUT_BUF_SAMPLES][2];
    int      next_buf;
    bool     initialized;
    int      frames_written;
} WaveOutState;

static WaveOutState g_waveout = { 0 };

void mcpx_apu_monitor_init(MCPXAPUState *d, Error **errp)
{
    (void)errp;
    d->monitor.stream = NULL;
    d->monitor.queued_bytes_low = 1024;
    d->monitor.queued_bytes_high = 3072;

    /* Try XAudio2 first (lower latency) */
    if (xa2_init()) {
        fprintf(stderr, "[APU] Using XAudio2 audio backend\n");
        return;
    }
    fprintf(stderr, "[APU] XAudio2 unavailable, falling back to waveOut\n");

    WAVEFORMATEX wfx = { 0 };
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = 2;
    wfx.nSamplesPerSec  = 48000;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    MMRESULT mr = waveOutOpen(&g_waveout.hwo, WAVE_MAPPER, &wfx,
                               0, 0, CALLBACK_NULL);
    if (mr != MMSYSERR_NOERROR) {
        fprintf(stderr, "[APU] waveOutOpen failed (error %u)\n", mr);
        g_waveout.initialized = false;
        return;
    }

    /* Prepare all headers */
    for (int i = 0; i < WAVEOUT_NUM_BUFS; i++) {
        memset(&g_waveout.hdrs[i], 0, sizeof(WAVEHDR));
        g_waveout.hdrs[i].lpData = (LPSTR)g_waveout.bufs[i];
        g_waveout.hdrs[i].dwBufferLength = WAVEOUT_BUF_SAMPLES * 2 * sizeof(int16_t);
        waveOutPrepareHeader(g_waveout.hwo, &g_waveout.hdrs[i], sizeof(WAVEHDR));
    }

    g_waveout.next_buf = 0;
    g_waveout.initialized = true;
    g_waveout.frames_written = 0;

    fprintf(stderr, "[APU] waveOut audio output initialized (48kHz stereo 16-bit, %d buffers)\n",
            WAVEOUT_NUM_BUFS);
}

void mcpx_apu_monitor_finalize(MCPXAPUState *d)
{
    (void)d;
    if (xa2_is_active()) {
        xa2_shutdown();
        return;
    }
    if (!g_waveout.initialized) return;

    waveOutReset(g_waveout.hwo);
    for (int i = 0; i < WAVEOUT_NUM_BUFS; i++) {
        waveOutUnprepareHeader(g_waveout.hwo, &g_waveout.hdrs[i], sizeof(WAVEHDR));
    }
    waveOutClose(g_waveout.hwo);
    g_waveout.initialized = false;
    fprintf(stderr, "[APU] waveOut audio output shut down (%d frames written)\n",
            g_waveout.frames_written);
}

/* Output ring between the 5.33-ms monitor blocks and the device buffers (1024 samples for XAudio2, 2048 for
 * waveOut). Each block is what the APU produced in that time: the VP/DSP mix in frame_buf, the test tone and the
 * software mixer (DirectSound HLE voices) on top. The monitor used to clear frame_buf and render 1024 samples of
 * tone + software mixer per block instead: the VP mix - every title whose DirectSound drives the real APU, Burnout
 * 3 among them - never reached the device (silence), and the software mixer ran four blocks ahead of real time
 * with three of every four buffers refused by the full XAudio2 queue. */
#define OUT_RING_SAMPLES   8192u                /* power of two */
#define OUT_RING_MAX       4096u                /* ~85 ms: above it the oldest samples are dropped */
static int16_t  s_out_ring[OUT_RING_SAMPLES][2];
static unsigned s_out_w, s_out_r;               /* free-running sample counters */
static unsigned s_out_dropped;

static void out_ring_take(int16_t (*dst)[2], unsigned n)
{
    for (unsigned i = 0; i < n; i++, s_out_r++) {
        dst[i][0] = s_out_ring[s_out_r % OUT_RING_SAMPLES][0];
        dst[i][1] = s_out_ring[s_out_r % OUT_RING_SAMPLES][1];
    }
}

unsigned mcpx_apu_monitor_take(int16_t (*dst)[2], unsigned max)
{
    unsigned n = s_out_w - s_out_r;
    if (n > max)
        n = max;
    out_ring_take(dst, n);
    return n;
}

void mcpx_apu_monitor_frame(MCPXAPUState *d)
{
    if ((d->ep_frame_div + 1) % 8) {
        return;
    }

    /* This block: frame_buf holds the 256 VP/DSP samples of the last eight frames (zero when se_frame did not run,
     * since it is cleared after every block). Tone and software mixer go on top, in real time.
     *
     * The VP/DSP mix goes to the output; RECOMP_APU_VP_OUT=0 gives the old silence. Off by default from 2026-10-26
     * (Burnout 3 was noise then: 7b06641 owner-first fixed it) to 2026-10-28, when the maintainer's listening tests
     * (whine fix 95442f0, the GP model 379e407) had it right. */
    {
        static int vp_out = -1;
        if (vp_out < 0) {
            const char *e = getenv("RECOMP_APU_VP_OUT");
            vp_out = !(e && *e == '0');
            if (!vp_out)
                fprintf(stderr, "[APU] RECOMP_APU_VP_OUT=0: the VP/DSP mix is muted\n");
        }
        if (!vp_out)
            memset(d->monitor.frame_buf, 0, sizeof(d->monitor.frame_buf));
    }
    if (g_test_tone.active && !g_audio_muted) {
        for (int i = 0; i < MIXER_FRAME_SAMPLES; i++) {
            int16_t s = (int16_t)(sin(g_test_tone.phase) * g_test_tone.amplitude);
            d->monitor.frame_buf[i][0] = s;
            d->monitor.frame_buf[i][1] = s;
            g_test_tone.phase += g_test_tone.phase_inc;
            if (g_test_tone.phase >= 2.0 * M_PI)
                g_test_tone.phase -= 2.0 * M_PI;
        }
    }
    if (g_audio_muted)
        memset(d->monitor.frame_buf, 0, sizeof(d->monitor.frame_buf));
    else
        mixer_render(d->monitor.frame_buf, MIXER_FRAME_SAMPLES);
    for (int i = 0; i < MIXER_FRAME_SAMPLES; i++, s_out_w++) {
        s_out_ring[s_out_w % OUT_RING_SAMPLES][0] = d->monitor.frame_buf[i][0];
        s_out_ring[s_out_w % OUT_RING_SAMPLES][1] = d->monitor.frame_buf[i][1];
    }
    memset(d->monitor.frame_buf, 0, sizeof(d->monitor.frame_buf));
    mcpx_aci_tick(MIXER_FRAME_SAMPLES);         /* the AC'97 channels play in step with this block */
    if (s_out_w - s_out_r > OUT_RING_MAX) {     /* the device fell behind: keep the latency bounded */
        unsigned drop = s_out_w - s_out_r - OUT_RING_MAX + 1024u;
        s_out_r += drop;
        if (s_out_dropped++ < 5)
            fprintf(stderr, "[APU] output ring over %u samples: %u oldest dropped\n", OUT_RING_MAX, drop);
    }

    /* XAudio2 path: submit whole buffers while the ring has them and the queue takes them */
    if (xa2_is_active()) {
        unsigned buf_size = (unsigned)xa2_get_buffer_size();
        int16_t xa2_tmp[1024][2];  /* matches XA2_BUF_SAMPLES max */
        if (buf_size > 1024u)
            buf_size = 1024u;
        while (s_out_w - s_out_r >= buf_size) {
            unsigned r = s_out_r;
            out_ring_take(xa2_tmp, buf_size);
            if (!xa2_submit_samples((const int16_t *)xa2_tmp, (int)buf_size)) {
                s_out_r = r;                    /* queue full: try again after the next block */
                break;
            }
        }
        return;
    }

    if (!g_waveout.initialized) return;
    if (s_out_w - s_out_r < WAVEOUT_BUF_SAMPLES) return;

    int idx = g_waveout.next_buf;
    WAVEHDR *hdr = &g_waveout.hdrs[idx];

    /* Wait if this buffer is still playing (with timeout) */
    int wait_loops = 0;
    while (!(hdr->dwFlags & WHDR_DONE) && (hdr->dwFlags & WHDR_INQUEUE)) {
        qemu_mutex_unlock(&d->lock);
        Sleep(1);
        qemu_mutex_lock(&d->lock);
        if (++wait_loops > 50) break;
    }

    /* Fill the waveOut buffer from the ring */
    out_ring_take(g_waveout.bufs[idx], WAVEOUT_BUF_SAMPLES);

    /* Submit to waveOut */
    hdr->dwFlags &= ~WHDR_DONE;
    waveOutWrite(g_waveout.hwo, hdr, sizeof(WAVEHDR));

    g_waveout.next_buf = (idx + 1) % WAVEOUT_NUM_BUFS;
    g_waveout.frames_written++;
}

/* ============================================================
 * Throttle (timing control for frame pacing)
 * ============================================================ */

static void throttle(X86Ctx *ctx, MCPXAPUState *d)
{
    if (d->ep_frame_div % 8) {
        return;
    }

    int64_t now_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME);

    if (d->next_frame_time_us == 0 ||
        now_us - d->next_frame_time_us > EP_FRAME_US) {
        d->next_frame_time_us = now_us;
    }

    while (!d->pause_requested) {
        now_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
        int64_t remaining_ms = (d->next_frame_time_us - now_us) / 1000;
        if (remaining_ms <= 0) {
            break;
        }
        /* The frame thread renders a whole 256-sample EP period at once and
         * then sleeps up to 5.33 ms here. What the title starts during that
         * sleep is reported now, not at the next period (as Takedawn's
         * toolkit a3e4485, BO-78): a voice it turned off gets its idle-voice
         * trap as the hardware VP's next 32-sample frame would give it
         * (mcpx_apu_vp_idle_scan), and a notification raised (a VOICE_OFF's
         * SSLA_DONE, the trap) becomes the interrupt at once, as the frame
         * loop does after every frame. DirectSound's StopEx-and-wait spins on
         * Sleep(0) until its service routine has seen the voice go idle:
         * Burnout 3's 0x0020B590, up to 5 ms of its main thread per stopped
         * voice, and a core busy with it in races. Only while the pipeline
         * runs, as se_frame does. */
        {
            uint32_t fectl = qatomic_read(&d->regs[NV_PAPU_FECTL]);
            if (GET_MASK(qatomic_read(&d->regs[NV_PAPU_SECTL]),
                         NV_PAPU_SECTL_XCNTMODE) != NV_PAPU_SECTL_XCNTMODE_OFF
                && !(fectl & NV_PAPU_FECTL_FEMETHMODE_HALTED)
                && !g_test_tone.active)
                mcpx_apu_vp_idle_scan(d);
        }
        if (d->set_irq) {
            update_irq(d);
            d->set_irq = false;
            apu_deliver_irq(ctx, d);
            continue;
        }
        /* A line still up (the one-CPU gate was taken, or a guest thread held
         * raised IRQL) is tried again within a millisecond, not at the end
         * of the period. */
        if (InterlockedCompareExchange(&s_irq_line, 0, 0)) {
            apu_deliver_irq(ctx, d);
            if (InterlockedCompareExchange(&s_irq_line, 0, 0))
                remaining_ms = 1;
        }
        qemu_cond_timedwait(&d->cond, &d->lock, (int)remaining_ms);
    }
    d->next_frame_time_us += EP_FRAME_US;

    d->sleep_acc_us += (int)(qemu_clock_get_us(QEMU_CLOCK_REALTIME) - now_us);
}

/* ============================================================
 * se_frame - Process one audio frame (VP -> GP -> EP pipeline)
 * ============================================================ */

static void se_frame(MCPXAPUState *d)
{
    mcpx_apu_update_dsp_preference(d);
    mcpx_debug_begin_frame();
    g_dbg.gp_realtime = d->gp.realtime;
    g_dbg.ep_realtime = d->ep.realtime;

    int64_t now_ms = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    int64_t elapsed_ms = now_ms - d->frame_count_time_ms;
    if (elapsed_ms >= 1000) {
        g_dbg.utilization = 1.0f - d->sleep_acc_us / (elapsed_ms * 1000.0f);
        g_dbg.frames_processed = (int)(d->frame_count * 1000.0 / elapsed_ms + 0.5);
        d->frame_count_time_ms = now_ms;
        d->frame_count = 0;
        d->sleep_acc_us = 0;
    }
    d->frame_count++;

    /* Buffer for all mixbins for this frame */
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
    memset(mixbins, 0, sizeof(mixbins));

    mcpx_apu_vp_frame(d, mixbins);
    mcpx_apu_dsp_frame(d, mixbins);
    mcpx_apu_monitor_frame(d);

    d->ep_frame_div++;

    mcpx_debug_end_frame();
}

/* ============================================================
 * APU frame thread (background processing)
 * ============================================================ */

static void *mcpx_apu_frame_thread(void *arg)
{
    X86Ctx thread_ctx = X86CTX_INIT;   /* this host thread's guest register set */
    X86Ctx *ctx = &thread_ctx;
    xbox_diag_ctx_set(ctx);
    MCPXAPUState *d = MCPX_APU_DEVICE(arg);
    qemu_mutex_lock(&d->lock);

    while (!qatomic_read(&d->exiting)) {
        if (d->pause_requested && !g_test_tone.active && !g_mixer_active_count) {
            d->is_idle = true;
            qemu_cond_signal(&d->idle_cond);
            qemu_cond_wait(&d->cond, &d->lock);
            d->is_idle = false;
            continue;
        }

        /* Always run the audio output loop — the software mixer and test tone
         * need continuous frame delivery regardless of APU register state.
         * The VP/DSP pipeline (se_frame) only runs when registers allow it. */
        throttle(ctx, d);
        {
            static int64_t next_report;
            int64_t now = qemu_clock_get_us(QEMU_CLOCK_REALTIME) / 1000;
            if (!next_report)
                next_report = now + 60000;
            if (now >= next_report) {
                next_report = now + 60000;
                fprintf(stderr, "[APU] interrupts delivered so far: %ld\n", (long)s_irq_delivered);
                {
                    char aci[200];
                    if (mcpx_aci_report(aci, sizeof aci) > 0)
                        fprintf(stderr, "%s\n", aci);
                }
                fflush(stderr);
            }
        }

        /* The doorbell ack stands in for the GP DSP, which on hardware runs
         * whatever the front end is doing. Tying it to se_frame stopped it
         * whenever FECTL was trapped or halted, and DirectSound then waits
         * forever to post its next command: Burnout 3 stalls in
         * sub_002F805E polling the same doorbell it was acked on at init. */
        mcpx_apu_dsp_ack_poll(d);

        int xcntmode = GET_MASK(qatomic_read(&d->regs[NV_PAPU_SECTL]),
                                NV_PAPU_SECTL_XCNTMODE);
        uint32_t fectl = qatomic_read(&d->regs[NV_PAPU_FECTL]);
        bool apu_active = (xcntmode != NV_PAPU_SECTL_XCNTMODE_OFF) &&
                          !(fectl & NV_PAPU_FECTL_FEMETHMODE_TRAPPED) &&
                          !(fectl & NV_PAPU_FECTL_FEMETHMODE_HALTED);

        if (apu_active && !g_test_tone.active) {
            /* Full pipeline: VP voices → DSP → monitor → waveOut */
            se_frame(d);
        } else {
            /* Lightweight: just monitor frame (test tone + software mixer) */
            mcpx_apu_monitor_frame(d);
            d->ep_frame_div++;
        }

        /* What xemu's frame thread does after each frame: turn a pending
         * notification (set by the voice processor or a trapped method) into
         * the interrupt line. Nothing here ever did, so the line never rose
         * even before there was anything to deliver it to. */
        if (d->set_irq) {
            update_irq(d);
            d->set_irq = false;
        }
        apu_deliver_irq(ctx, d);

        /* Let the guest in once per frame.
         *
         * The thread holds d->lock for its whole loop and only drops it inside
         * throttle()'s wait. Once voices really play, processing can run
         * behind real time, throttle never waits, and the lock is never
         * released -- while every VOICE_ON/OFF/RELEASE the title writes needs
         * it (voice_lock). A critical section is not fair, so the title's
         * thread starved there indefinitely: Burnout 3 froze on its vehicle
         * select, blocked in voice_lock at raised IRQL, which in turn held
         * off every USB interrupt. */
        qemu_mutex_unlock(&d->lock);
        SwitchToThread();
        qemu_mutex_lock(&d->lock);
    }

    qemu_mutex_unlock(&d->lock);
    return NULL;
}

/* ============================================================
 * Wait for idle / resume helpers
 * ============================================================ */

static void mcpx_apu_wait_for_idle(MCPXAPUState *d)
{
    d->pause_requested = true;
    qemu_cond_signal(&d->cond);
    while (!d->is_idle) {
        qemu_cond_wait(&d->idle_cond, &d->lock);
    }
}

static void mcpx_apu_resume(MCPXAPUState *d)
{
    d->pause_requested = false;
    qemu_cond_signal(&d->cond);
}

/* ============================================================
 * Reset
 * ============================================================ */

static void mcpx_apu_reset_locked(MCPXAPUState *d)
{
    memset(d->regs, 0, sizeof(d->regs));
    mcpx_apu_vp_reset(d);

    d->set_irq = false;
}

/* ============================================================
 * Public API: Init / Shutdown
 * ============================================================ */

MCPXAPUState *mcpx_apu_init_standalone(uint8_t *ram_ptr)
{
    MCPXAPUState *d = (MCPXAPUState *)calloc(1, sizeof(MCPXAPUState));
    if (!d) {
        fprintf(stderr, "[APU] Failed to allocate MCPXAPUState\n");
        return NULL;
    }

    g_apu_ram_ptr = ram_ptr;
    g_state = d;
    d->ram_ptr = ram_ptr;

    d->set_irq = false;
    d->exiting = false;
    d->is_idle = false;
    d->pause_requested = true;

    qemu_mutex_init(&d->lock);
    qemu_mutex_lock(&d->lock);
    qemu_cond_init(&d->cond);
    qemu_cond_init(&d->idle_cond);

    /* Init VP (voice processor) */
    mcpx_apu_vp_init(d);

    /* Init DSP (GP/EP - stubbed) */
    mcpx_apu_dsp_init(d);

    /* Init software mixer for DirectSound bridge */
    mixer_init();

    /* Init monitor (waveOut output) */
    Error *local_err = NULL;
    mcpx_apu_monitor_init(d, &local_err);
    if (local_err) {
        warn_reportf_err(local_err, "mcpx_apu_monitor_init failed: ");
    }

    /* The AC'97 output channels and their interrupt (vector 6), when the AC'97 write trap is armed */
    mcpx_aci_attach();

    /* Start background frame thread */
    qemu_thread_create(&d->apu_thread, "mcpx.apu_thread",
                       mcpx_apu_frame_thread, d, QEMU_THREAD_JOINABLE);
    mcpx_apu_wait_for_idle(d);
    qemu_mutex_unlock(&d->lock);

    fprintf(stderr, "[APU] MCPX APU initialized (standalone)\n");
    fprintf(stderr, "[APU]   RAM pointer: %p\n", (void *)ram_ptr);
    fprintf(stderr, "[APU]   MMIO base: 0xFE800000 (512KB)\n");
    fprintf(stderr, "[APU]   VP: %d max voices, %d samples/frame\n",
            MCPX_HW_MAX_VOICES, NUM_SAMPLES_PER_FRAME);
    return d;
}

void mcpx_apu_shutdown(MCPXAPUState *d)
{
    if (!d) return;

    fprintf(stderr, "[APU] Shutting down MCPX APU...\n");

    qemu_mutex_lock(&d->lock);
    mcpx_apu_wait_for_idle(d);
    qatomic_set(&d->exiting, true);
    qemu_cond_signal(&d->cond);
    qemu_mutex_unlock(&d->lock);

    qemu_thread_join(&d->apu_thread);
    mcpx_apu_vp_finalize(d);
    mcpx_apu_monitor_finalize(d);

    free(d);
    g_state = NULL;
    fprintf(stderr, "[APU] Shutdown complete\n");
}

/* ============================================================
 * VP MMIO handlers (sub-region at +0x20000)
 *
 * These are called when the game writes to the VP PIO registers
 * to configure voices, SSL, etc.
 * ============================================================ */

uint64_t mcpx_apu_vp_read(void *opaque, hwaddr addr, unsigned int size);
void mcpx_apu_vp_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size);

/* Dispatch a VP-region access (offset 0x20000-0x2FFFF from APU base) */
void mcpx_apu_dispatch_mmio(MCPXAPUState *d, hwaddr addr, uint64_t val,
                             unsigned int size, bool is_write)
{
    if (addr >= 0x20000 && addr < 0x30000) {
        /* VP region */
        hwaddr vp_addr = addr - 0x20000;
        if (is_write) {
            mcpx_apu_vp_write(d, vp_addr, val, size);
        }
        /* VP reads handled by caller if needed */
    } else if (addr < 0x20000) {
        /* Main APU registers */
        if (is_write) {
            mcpx_apu_write(d, addr, val, size);
        }
    }
    /* GP (0x30000) and EP (0x50000) regions ignored for now */
}

/* ============================================================
 * Public MMIO API (called from VEH or MMIO hook)
 * addr is offset from APU base (0xFE800000)
 * ============================================================ */

uint64_t mcpx_apu_mmio_read(MCPXAPUState *d, uint64_t addr, unsigned int size)
{
    if (!d) return 0;
    if (addr >= 0x20000 && addr < 0x30000) {
        return mcpx_apu_vp_read(d, addr - 0x20000, size);
    } else if (addr < 0x20000) {
        return mcpx_apu_read(d, (hwaddr)addr, size);
    }
    return 0;
}

void mcpx_apu_mmio_write(MCPXAPUState *d, uint64_t addr, uint64_t val, unsigned int size)
{
    if (!d) return;
    mcpx_apu_dispatch_mmio(d, (hwaddr)addr, val, size, true);
}

/* ============================================================
 * APU Test Tone - Direct waveOut sine generator
 *
 * Bypasses the VP pipeline entirely and writes a 440Hz sine wave
 * directly to the monitor frame_buf. This verifies that waveOut
 * output works correctly.
 * ============================================================ */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void mcpx_apu_play_test_tone(MCPXAPUState *d)
{
    if (!d) {
        fprintf(stderr, "[APU-TEST] No APU state\n");
        return;
    }

    if (g_test_tone.active) {
        /* Toggle off */
        g_test_tone.active = false;
        fprintf(stderr, "[APU-TEST] Test tone OFF\n");
        return;
    }

    /* 440Hz at 48kHz sample rate */
    g_test_tone.phase = 0.0;
    g_test_tone.phase_inc = 2.0 * M_PI * 440.0 / 48000.0;
    g_test_tone.amplitude = 6000;  /* ~18% of full scale */
    g_test_tone.active = true;

    /* Make sure waveOut is running - enable SECTL and resume APU thread */
    qemu_mutex_lock(&d->lock);
    d->regs[NV_PAPU_SECTL] = NV_PAPU_SECTL_XCNTMODE & ~NV_PAPU_SECTL_XCNTMODE_OFF;
    d->regs[NV_PAPU_FECTL] = NV_PAPU_FECTL_FEMETHMODE_FREE_RUNNING;
    /* Initialize empty voice lists so VP frame doesn't crash */
    d->regs[NV_PAPU_TVL2D] = 0xFFFF;
    d->regs[NV_PAPU_TVL3D] = 0xFFFF;
    d->regs[NV_PAPU_TVLMP] = 0xFFFF;
    mcpx_apu_resume(d);
    qemu_mutex_unlock(&d->lock);

    fprintf(stderr, "[APU-TEST] Test tone ON - 440Hz sine, amplitude=%d\n",
            g_test_tone.amplitude);
}

/* ============================================================
 * Software mixer - mixes DirectSound buffers to waveOut
 *
 * This bypasses the VP hardware voice pipeline entirely.
 * DirectSound buffers register PCM data here, and the APU
 * frame thread mixes them into the monitor frame_buf.
 * ============================================================ */

static void mixer_init(void)
{
    if (g_mixer_initialized) return;
    InitializeCriticalSection(&g_mixer_cs);
    memset(g_mixer_voices, 0, sizeof(g_mixer_voices));
    g_mixer_initialized = true;
}

int apu_mixer_alloc_voice(void)
{
    if (!g_mixer_initialized) mixer_init();
    EnterCriticalSection(&g_mixer_cs);
    for (int i = 0; i < APU_MIXER_MAX_VOICES; i++) {
        if (!g_mixer_voices[i].active && !g_mixer_voices[i].pcm_data) {
            g_mixer_voices[i].volume = 1.0f;
            g_mixer_voices[i].sample_rate = 44100;
            g_mixer_voices[i].num_channels = 2;
            LeaveCriticalSection(&g_mixer_cs);
            return i;
        }
    }
    LeaveCriticalSection(&g_mixer_cs);
    return -1;
}

void apu_mixer_free_voice(int slot)
{
    if (slot < 0 || slot >= APU_MIXER_MAX_VOICES) return;
    EnterCriticalSection(&g_mixer_cs);
    apu_mixer_stop(slot);
    g_mixer_voices[slot].pcm_data = NULL;
    g_mixer_voices[slot].pcm_bytes = 0;
    g_mixer_voices[slot].play_offset = 0;
    LeaveCriticalSection(&g_mixer_cs);
}

APUMixerVoice *apu_mixer_get_voice(int slot)
{
    if (slot < 0 || slot >= APU_MIXER_MAX_VOICES) return NULL;
    return &g_mixer_voices[slot];
}

int apu_mixer_set_position(int slot, uint32_t byte_offset)
{
    if (slot < 0 || slot >= APU_MIXER_MAX_VOICES) return 0;
    EnterCriticalSection(&g_mixer_cs);
    APUMixerVoice *v = &g_mixer_voices[slot];
    uint32_t frame_bytes = v->num_channels * sizeof(int16_t);
    int valid = (v->num_channels == 1 || v->num_channels == 2) &&
                byte_offset < v->pcm_bytes &&
                byte_offset / frame_bytes < v->pcm_bytes / frame_bytes;
    if (valid) v->play_offset = ((uint64_t)(byte_offset / frame_bytes)) << 16;
    LeaveCriticalSection(&g_mixer_cs);
    return valid;
}

void apu_mixer_get_state(int slot, uint32_t *byte_offset, int *active, int *looping)
{
    if (byte_offset) *byte_offset = 0;
    if (active) *active = 0;
    if (looping) *looping = 0;
    if (slot < 0 || slot >= APU_MIXER_MAX_VOICES) return;
    EnterCriticalSection(&g_mixer_cs);
    APUMixerVoice *v = &g_mixer_voices[slot];
    if (byte_offset) *byte_offset = (uint32_t)(v->play_offset >> 16) *
                                    v->num_channels * sizeof(int16_t);
    if (active) *active = v->active;
    if (looping) *looping = v->active && v->looping;
    LeaveCriticalSection(&g_mixer_cs);
}

void apu_mixer_play(int slot, int looping)
{
    if (g_audio_muted) return;
    if (slot < 0 || slot >= APU_MIXER_MAX_VOICES) return;
    EnterCriticalSection(&g_mixer_cs);
    APUMixerVoice *v = &g_mixer_voices[slot];
    if (!v->pcm_data || (v->num_channels != 1 && v->num_channels != 2) ||
        v->pcm_bytes < v->num_channels * sizeof(int16_t)) {
        LeaveCriticalSection(&g_mixer_cs);
        return;
    }
    v->looping = looping;
    if (!v->active) InterlockedIncrement((volatile LONG *)&g_mixer_active_count);
    v->active = 1;

    static int play_log_count = 0;
    if (play_log_count < 20) {
        fprintf(stderr, "[APU-MIX] Play voice %d: %u bytes, %u ch, %u Hz, vol=%.2f, loop=%d\n",
                slot, v->pcm_bytes, v->num_channels, v->sample_rate, v->volume, looping);
        play_log_count++;
    }
    LeaveCriticalSection(&g_mixer_cs);

    /* The frame thread takes the APU lock before the mixer lock. */
    extern MCPXAPUState *g_state;
    if (g_state) {
        qemu_mutex_lock(&g_state->lock);
        g_state->pause_requested = false;
        qemu_cond_signal(&g_state->cond);
        qemu_mutex_unlock(&g_state->lock);
    }
}

void apu_mixer_stop(int slot)
{
    if (slot < 0 || slot >= APU_MIXER_MAX_VOICES) return;
    EnterCriticalSection(&g_mixer_cs);
    if (g_mixer_voices[slot].active) {
        g_mixer_voices[slot].active = 0;
        InterlockedDecrement((volatile LONG *)&g_mixer_active_count);
    }
    LeaveCriticalSection(&g_mixer_cs);
}

/* Mix all active voices into frame_buf. Called from mcpx_apu_monitor_frame.
 * Keep 16 fractional bits in a wide offset so buffers can exceed 65536 frames. */
static void mixer_render(int16_t frame_buf[][2], int num_samples)
{
    if (!g_mixer_initialized) return;
    EnterCriticalSection(&g_mixer_cs);

    for (int v = 0; v < APU_MIXER_MAX_VOICES; v++) {
        APUMixerVoice *voice = &g_mixer_voices[v];
        if (!voice->active || !voice->pcm_data || voice->pcm_bytes == 0)
            continue;

        uint32_t total_frames = voice->pcm_bytes / sizeof(int16_t);
        if (voice->num_channels == 2) total_frames /= 2;
        if (total_frames == 0) continue;

        /* Fixed-point 16.16 increment per output sample */
        uint64_t inc = ((uint64_t)voice->sample_rate << 16) / 48000;
        uint64_t pos = voice->play_offset;
        uint64_t end = (uint64_t)total_frames << 16;
        float vol = voice->volume;

        for (int i = 0; i < num_samples; i++) {
            uint32_t src_frame = pos >> 16;

            if (src_frame >= total_frames) {
                if (voice->looping) {
                    pos %= end;
                    src_frame = (uint32_t)(pos >> 16);
                } else {
                    pos = 0;
                    voice->active = 0;
                    InterlockedDecrement((volatile LONG *)&g_mixer_active_count);
                    break;
                }
            }

            int32_t left, right;
            if (voice->num_channels >= 2) {
                left  = (int32_t)(voice->pcm_data[src_frame * 2] * vol);
                right = (int32_t)(voice->pcm_data[src_frame * 2 + 1] * vol);
            } else {
                left = right = (int32_t)(voice->pcm_data[src_frame] * vol);
            }

            /* Accumulate (mix) into frame_buf with clamping */
            int32_t mixed_l = frame_buf[i][0] + left;
            int32_t mixed_r = frame_buf[i][1] + right;
            if (mixed_l > 32767) mixed_l = 32767;
            if (mixed_l < -32768) mixed_l = -32768;
            if (mixed_r > 32767) mixed_r = 32767;
            if (mixed_r < -32768) mixed_r = -32768;
            frame_buf[i][0] = (int16_t)mixed_l;
            frame_buf[i][1] = (int16_t)mixed_r;

            pos += inc;
        }

        voice->play_offset = pos;
        uint32_t end_frame = pos >> 16;
        if (end_frame >= total_frames) {
            if (voice->looping) {
                voice->play_offset = pos % end;
            } else if (voice->active) {
                voice->play_offset = 0;
                voice->active = 0;
                InterlockedDecrement((volatile LONG *)&g_mixer_active_count);
            }
        }
    }
    LeaveCriticalSection(&g_mixer_cs);
}
