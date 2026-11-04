/*
 * apu_lowpass.h - the per-voice resonant low-pass of the voice processor
 * (used when a voice's FMODE turns the filter on).
 *
 * This project's own file, MIT (LICENSE). It replaces the state variable
 * filter that came with xemu's voice processor (vp/svf.h there, GPLv2) and is
 * not derived from that code: it is the digital state variable filter as Hal
 * Chamberlin describes it ("Musical Applications of Microprocessors", 2nd ed.,
 * 1985, digital filters chapter) - two integrators in a feedback loop:
 *
 *     high = in - low - q * band
 *     band = band + f * high
 *     low  = low  + f * band
 *
 * Only the low-pass output is kept; it is the only one the voice processor
 * uses. The integrators are stepped high -> band -> low within one sample, so
 * an impulse reaches the output in the same sample (as the old filter did; a
 * voice delayed by one sample against the others would change the mix).
 *
 *   f  tuning: the voice's cutoff argument 2^(FC0/4096), clamped to
 *      1/256..1 by the caller, is used as f directly.
 *   q  damping (1/Q): the voice's resonance argument FC1/32768, clamped to
 *      0.0794..1 by the caller, is used as q directly.
 *
 * Level: the input is scaled by sqrt(q/2 + 0.01) so that a resonant setting
 * does not run away and every voice keeps the loudness it had with the old
 * filter (passband -2.9 dB at q = 1, -13.0 dB at q = 0.0794).
 *
 * Difference to the old filter: this one is linear. The old one softly
 * limited the band integrator, which only shows on loud signals at the corner
 * of a strongly resonant setting. So that such a peak (up to ~2.4 x full
 * scale) is not clipped hard by the [-1, 1] clamp that follows, the caller
 * passes the filter output through apu_lowpass_limit: unchanged up to the
 * knee 0.75, above it a tanh curve that meets the line with slope 1 and
 * approaches +-1 (full scale comes out at 0.940, -0.5 dB). The unit test
 * tests/apu_lowpass compares filter + limit with the old filter + the old
 * hard clamp on fixed input vectors and states the tolerance.
 *
 * The loop is stable for f*f + 2*f*q < 4; the caller's ranges (f, q <= 1)
 * stay below 3. apu_lowpass_set clamps f under the limit for any other use.
 *
 * Takedawn's xboxrecomp fork made the same replacement (its own
 * apu_lowpass.h, MIT); this file is written for this tree, not copied.
 */
#pragma once

#include <math.h>

/* The two integrators; all zero is silence (memset resets). */
typedef struct ApuLowpass {
    float band;
    float low;
} ApuLowpass;

/* One setting, computed once per frame of samples. */
typedef struct ApuLowpassCoef {
    float f;      /* tuning */
    float q;      /* damping */
    float gain;   /* input level, sqrt(q/2 + 0.01) */
} ApuLowpassCoef;

static inline ApuLowpassCoef apu_lowpass_set(float f, float q)
{
    ApuLowpassCoef c;
    if (!(q > 0.0f)) q = 0.0f;
    if (q > 2.0f) q = 2.0f;
    /* Stability: f*f + 2*f*q < 4, i.e. f < sqrt(q*q + 4) - q; keep 2 % off. */
    float f_lim = 0.98f * (sqrtf(q * q + 4.0f) - q);
    if (!(f > 0.0f)) f = 0.0f;
    if (f > f_lim) f = f_lim;
    c.f = f;
    c.q = q;
    c.gain = sqrtf(q * 0.5f + 0.01f);
    return c;
}

/* Once per frame, before stepping: a state that is not a finite number is
 * cleared (one bad sample must not silence the voice for good), and one that
 * has decayed far below audibility is zeroed so it does not idle in
 * denormals. */
static inline void apu_lowpass_sanitize(ApuLowpass *s)
{
    if (!isfinite(s->band) || !isfinite(s->low) ||
        (fabsf(s->band) < 1e-20f && fabsf(s->low) < 1e-20f)) {
        s->band = 0.0f;
        s->low = 0.0f;
    }
}

static inline float apu_lowpass_step(ApuLowpass *s, const ApuLowpassCoef *c,
                                     float in)
{
    float high = c->gain * in - s->low - c->q * s->band;
    s->band += c->f * high;
    s->low += c->f * s->band;
    return s->low;
}

/* Soft limit for the filter output: identity for |x| <= APU_LOWPASS_KNEE,
 * above it knee + (1 - knee) * tanh((|x| - knee) / (1 - knee)), continuous
 * with slope 1 at the knee, never beyond +-1. A NaN passes through
 * (the [-1, 1] clamp after it and apu_lowpass_sanitize deal with it). */
#define APU_LOWPASS_KNEE 0.75f

static inline float apu_lowpass_limit(float x)
{
    const float k = APU_LOWPASS_KNEE, r = 1.0f - APU_LOWPASS_KNEE;
    float a = fabsf(x);
    if (!(a > k))
        return x;
    float y = k + r * tanhf((a - k) / r);
    return x < 0.0f ? -y : y;
}
