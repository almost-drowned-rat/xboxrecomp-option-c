/*
 * MCPX APU State Structures (adapted from xemu)
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

#pragma once

#include "apu_shim.h"
#include "apu_regs.h"
#include "apu_debug.h"

/* ============================================================
 * DSP (GP/EP) placeholder state
 *
 * The DSP56300 cores are not emulated (apu_dsp.c bypasses them), so only
 * which processor a state belongs to is kept. The DSP core, DMA and filter
 * declarations that came from xemu's GPL-2.0 DSP headers and vp/svf.h were
 * removed; the voice low-pass is apu_lowpass.h (MIT).
 * ============================================================ */

typedef struct DSPState {
    bool is_gp;
} DSPState;

#include "apu_lowpass.h"
#include "apu_resample.h"

/* ============================================================
 * HRTF Filter - from vp/hrtf.h
 * ============================================================ */

#define HRTF_SAMPLES_PER_FRAME  NUM_SAMPLES_PER_FRAME
#define HRTF_NUM_TAPS           31
#define HRTF_MAX_DELAY_SAMPLES  42
#define HRTF_BUFLEN             (HRTF_NUM_TAPS + HRTF_MAX_DELAY_SAMPLES)
#define HRTF_PARAM_SMOOTH_ALPHA 0.01f

typedef struct {
    int buf_pos;
    struct {
        float buf[HRTF_BUFLEN];
        float hrir_coeff_cur[HRTF_NUM_TAPS];
        float hrir_coeff_tar[HRTF_NUM_TAPS];
    } ch[2];
    float itd_cur;
    float itd_tar;
} HrtfFilter;

static inline void hrtf_filter_init(HrtfFilter *f) {
    memset(f, 0, sizeof(*f));
}

static inline void hrtf_filter_clear_history(HrtfFilter *f) {
    f->buf_pos = 0;
    memset(f->ch[0].buf, 0, sizeof(f->ch[0].buf));
    memset(f->ch[1].buf, 0, sizeof(f->ch[1].buf));
}

static inline void
hrtf_filter_set_target_params(HrtfFilter *f, float hrir_coeff[2][HRTF_NUM_TAPS],
                              float itd) {
    f->itd_tar = fmaxf(-(float)HRTF_MAX_DELAY_SAMPLES,
                        fminf(itd, (float)HRTF_MAX_DELAY_SAMPLES));
    for (int ch = 0; ch < 2; ch++) {
        float *coeff = f->ch[ch].hrir_coeff_tar;
        memcpy(coeff, hrir_coeff[ch], sizeof(f->ch[ch].hrir_coeff_tar));
        float s = 0.0f;
        for (int k = 0; k < HRTF_NUM_TAPS; k++) {
            s += fabsf(coeff[k]);
        }
        if (s == 0.0f || s == 1.0f) break;
        for (int k = 0; k < HRTF_NUM_TAPS; k++) {
            coeff[k] /= s;
        }
    }
}

static inline float hrtf_filter_smooth_param(float cur, float tar) {
    return cur + HRTF_PARAM_SMOOTH_ALPHA * (tar - cur);
}

static inline void hrtf_filter_step_parameters(HrtfFilter *f) {
    for (int ch = 0; ch < 2; ch++) {
        float *cc = f->ch[ch].hrir_coeff_cur;
        float *ct = f->ch[ch].hrir_coeff_tar;
        for (int k = 0; k < HRTF_NUM_TAPS; k++) {
            cc[k] = hrtf_filter_smooth_param(cc[k], ct[k]);
        }
    }
    f->itd_cur = hrtf_filter_smooth_param(f->itd_cur, f->itd_tar);
}

static inline void hrtf_filter_process(HrtfFilter *f,
                                       float in[][2],
                                       float out[][2]) {
    for (int n = 0; n < HRTF_SAMPLES_PER_FRAME; n++) {
        hrtf_filter_step_parameters(f);
        for (int ch = 0; ch < 2; ch++) {
            float *buf = f->ch[ch].buf;
            float *coeff = f->ch[ch].hrir_coeff_cur;
            buf[f->buf_pos] = in[n][ch];
            float d = f->itd_cur * (ch == 0 ? +1.0f : -1.0f);
            if (d < 0.0f) d = 0.0f;
            int di = (int)d;
            float dfrac = d - di;
            float acc = 0.0f;
            for (int k = 0; k < HRTF_NUM_TAPS; k++) {
                int idx1 = (f->buf_pos - di - k + HRTF_BUFLEN) % HRTF_BUFLEN;
                float s = buf[idx1];
                if (dfrac > 0.0f) {
                    int idx2 = (idx1 - 1 + HRTF_BUFLEN) % HRTF_BUFLEN;
                    s = s * (1 - dfrac) + buf[idx2] * dfrac;
                }
                acc += coeff[k] * s;
            }
            out[n][ch] = acc;
        }
        f->buf_pos = (f->buf_pos + 1) % HRTF_BUFLEN;
    }
}

#include "xbox_adpcm.h"   /* adpcm_decode_block */

/* ============================================================
 * VP State (from vp/vp.h)
 * ============================================================ */

typedef struct MCPXAPUState MCPXAPUState;

typedef struct MCPXAPUVPSSLData {
    uint32_t base[MCPX_HW_SSLS_PER_VOICE];
    uint8_t count[MCPX_HW_SSLS_PER_VOICE];
    int ssl_index;
    int ssl_seg;
} MCPXAPUVPSSLData;

typedef struct MCPXAPUVoiceFilter {
    uint16_t voice;
    float resample_buf[NUM_SAMPLES_PER_FRAME * 2];
    SRC_STATE *resampler;
    ApuResampler rs;                /* voice_resample: position between source samples (apu_resample.h) */
    ApuLowpass lowpass[2];
    HrtfFilter hrtf;
    float gain_prev[8];             /* each bin's gain at the end of the last frame (voice_process ramp) */
    bool gain_valid;
} MCPXAPUVoiceFilter;

typedef struct VoiceWorkItem {
    int voice;
    int list;
} VoiceWorkItem;

typedef struct VoiceWorker {
    QemuThread thread;
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
    float sample_buf[NUM_SAMPLES_PER_FRAME][2];
    VoiceWorkItem queue[MCPX_HW_MAX_VOICES];
    int queue_len;
} VoiceWorker;

typedef struct VoiceWorkDispatch {
    QemuMutex lock;
    int num_workers;
    VoiceWorker *workers;
    bool workers_should_exit;
    QemuCond work_pending;
    uint64_t workers_pending;
    QemuCond work_finished;
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
    VoiceWorkItem queue[MCPX_HW_MAX_VOICES];
    int queue_len;
} VoiceWorkDispatch;

typedef struct MCPXAPUVPState {
    MemoryRegion mmio;
    VoiceWorkDispatch voice_work_dispatch;
    MCPXAPUVoiceFilter filters[MCPX_HW_MAX_VOICES];

    int ssl_base_page;
    MCPXAPUVPSSLData ssl[MCPX_HW_MAX_VOICES];
    uint8_t hrtf_headroom;
    uint8_t hrtf_submix[4];
    uint8_t submix_headroom[NUM_MIXBINS];
    float sample_buf[NUM_SAMPLES_PER_FRAME][2];
    uint64_t voice_locked[4];

    struct {
        int current_entry;
        struct {
            float hrir[2][HRTF_NUM_TAPS];
            float itd;
        } entries[HRTF_ENTRY_COUNT];
    } hrtf;

    uint32_t inbuf_sge_handle;
    uint32_t outbuf_sge_handle;
} MCPXAPUVPState;

/* ============================================================
 * GP/EP State (from dsp/gp_ep.h)
 * ============================================================ */

typedef struct MCPXAPUGPState {
    bool realtime;
    MemoryRegion mmio;
    DSPState *dsp;
    uint32_t regs[0x10000];
} MCPXAPUGPState;

typedef struct MCPXAPUEPState {
    bool realtime;
    MemoryRegion mmio;
    DSPState *dsp;
    uint32_t regs[0x10000];
} MCPXAPUEPState;

/* ============================================================
 * Main APU State (from apu_int.h)
 * ============================================================ */

struct MCPXAPUState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    bool exiting;
    bool set_irq;

    QemuThread apu_thread;
    QemuMutex lock;
    QemuCond cond;
    QemuCond idle_cond;
    bool pause_requested;
    bool is_idle;

    MemoryRegion *ram;
    uint8_t *ram_ptr;
    MemoryRegion mmio;

    MCPXAPUVPState vp;
    MCPXAPUGPState gp;
    MCPXAPUEPState ep;

    uint32_t regs[0x20000];

    int ep_frame_div;
    int sleep_acc_us;
    int frame_count;
    int64_t frame_count_time_ms;
    int64_t next_frame_time_us;

    struct {
        struct {
            int backoff, ok, speedup;
        } pacing;
        struct {
            int64_t last_us;
            int64_t min_us, max_us, sum_us;
            int count;
        } deviation;
        int queued_bytes_min, queued_bytes_max;
        int64_t queued_bytes_sum;
        int queued_bytes_count;
    } throttle;

    struct {
        McpxApuDebugMonitorPoint point;
        int16_t frame_buf[256][2];
        int16_t frame_surr[256][4];   /* 5.1 output only (xa2_surround): the GP's FC LFE SL SR; FL FR stay in frame_buf */
        void *stream; /* SDL_AudioStream* - stubbed */
        int queued_bytes_low, queued_bytes_high;
    } monitor;
};

/* ============================================================
 * Forward declarations for APU sub-module functions
 * ============================================================ */

/* VP functions */
void mcpx_apu_vp_init(MCPXAPUState *d);
void mcpx_apu_vp_finalize(MCPXAPUState *d);
void mcpx_apu_vp_frame(MCPXAPUState *d, float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME]);
int  mcpx_apu_vp_idle_scan(MCPXAPUState *d);
int  mcpx_apu_vp_idle_queued(void);
void mcpx_apu_vp_reset(MCPXAPUState *d);
/* RECOMP_APU_DIAG=1: "[APUDIAG]" lines - register writes of the interrupt/front-end path, traps, voice on/off,
 * every delivery and a 1 s state line (apu_core.c). Off by default. */
int  mcpx_apu_diag(void);

/* DSP functions (stubbed) */
void mcpx_apu_dsp_init(MCPXAPUState *d);
void mcpx_apu_update_dsp_preference(MCPXAPUState *d);
void mcpx_apu_dsp_frame(MCPXAPUState *d, float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME]);
void mcpx_apu_dsp_ack_poll(MCPXAPUState *d);
/* Every eighth frame: the block in monitor.frame_buf (+ test tone, software mixer) goes into the output ring and
 * frame_buf is cleared; full device buffers leave the ring for XAudio2 / waveOut. */
void mcpx_apu_monitor_frame(MCPXAPUState *d);
/* Takes up to max samples from the output ring (what no device took); returns how many. For tests. */
unsigned mcpx_apu_monitor_take(int16_t (*dst)[2], unsigned max);
/* RECOMP_APU_DSP_ACK=auto: the DSP command doorbell, GPSADDR SGE[0] + 0x810
 * (physical); 0 while GPSADDR or its entry 0 is unset. */
uint32_t mcpx_apu_dsp_auto_doorbell(MCPXAPUState *d);

/* Debug globals */
extern MCPXAPUState *g_state;
extern struct McpxApuDebug g_dbg, g_dbg_cache;
extern int g_dbg_voice_monitor;
extern uint64_t g_dbg_muted_voices[4];
