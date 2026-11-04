/*
 * apu_resample.h - a voice's samples at the rate its pitch asks for.
 *
 * The voice processor runs at 48 kHz; a voice's pitch says how many of its
 * own samples one output step covers. voice_process turns the pitch into
 * `rate` (xemu's formula, libsamplerate's src_ratio: outputs per source
 * sample), so each output advances the source by step = 1 / rate. Before
 * this file voice_resample ignored the rate and fetched one source sample per
 * output: every voice played at 48 kHz whatever the title set - Burnout 3's
 * engines held one fixed tone (a 2490 Hz whine from the CPU cars at each
 * race start) and every pitch bend was lost.
 *
 * Linear interpolation between the two source samples an output sits between
 * (last at 0, next at 1, pos in [0,1) between them). The caller asks
 * apu_resample_need how many new source samples the next n outputs step
 * past - exactly that many, so nothing fetched is dropped (fetching one more
 * per call plays a stream ~1 sample per frame fast) - fetches them, and
 * apu_resample_run makes the outputs. A shortfall (voice ended, stream dry)
 * makes fewer outputs.
 *
 * Ported from Takedawn's xboxrecomp fork (e34e52e "apu: resample voices by
 * their pitch (linear interpolation)", MIT; NOTICE), split out of
 * voice_resample so tests/apu_resample can drive it without a voice
 * processor.
 */
#pragma once

#include <math.h>

typedef struct ApuResampler {
    float last[2], next[2], pos;
    int   have;                     /* last/next fetched (the first two source samples) */
} ApuResampler;

static inline void apu_resample_reset(ApuResampler *r)
{
    r->have = 0;
    r->pos = 0.0f;
}

static inline float apu_resample_step(float rate)
{
    return rate > 0.0f ? 1.0f / rate : 1.0f;
}

/* Starts the voice on its first two source samples. */
static inline void apu_resample_prime(ApuResampler *r, const float s0[2], const float s1[2])
{
    r->last[0] = s0[0]; r->last[1] = s0[1];
    r->next[0] = s1[0]; r->next[1] = s1[1];
    r->pos = 0.0f;
    r->have = 1;
}

/* New source samples the next n outputs step past: output k sits at pos + k * step, and every whole unit that
 * position reaches before the last output is one consumed. The last output's own step stays pending in pos. */
static inline int apu_resample_need(const ApuResampler *r, int n, float step)
{
    int need;
    if (n <= 0)
        return 0;
    need = (int)floorf(r->pos + (float)(n - 1) * step);
    return need < 0 ? 0 : need;
}

/* Makes up to n outputs from the got source samples in src; returns how many were made. */
static inline int apu_resample_run(ApuResampler *r, const float (*src)[2], int got, float (*out)[2], int n,
                                   float step)
{
    int in = 0, k;

    for (k = 0; k < n; k++) {
        while (r->pos >= 1.0f) {
            if (in >= got)
                return k;
            r->last[0] = r->next[0]; r->last[1] = r->next[1];
            r->next[0] = src[in][0]; r->next[1] = src[in][1];
            in++;
            r->pos -= 1.0f;
        }
        out[k][0] = r->last[0] + r->pos * (r->next[0] - r->last[0]);
        out[k][1] = r->last[1] + r->pos * (r->next[1] - r->last[1]);
        r->pos += step;
    }
    return k;
}
