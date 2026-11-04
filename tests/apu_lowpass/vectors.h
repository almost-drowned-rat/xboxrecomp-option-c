/* Fixed input vectors and filter settings for the apu_lowpass test. The
 * reference outputs in old_svf_ref.h were produced from exactly these by the
 * filter apu_lowpass.h replaced (xemu vp/svf.h, run as apu_vp.c ran it: one
 * setup per 32-sample frame, state kept across frames). Change nothing here
 * without regenerating that file. */
#pragma once

#include <math.h>
#include <stdint.h>

#define LP_FRAME    32      /* NUM_SAMPLES_PER_FRAME */
#define LP_LEN      128     /* 4 frames per vector */
#define LP_SETTINGS 6
#define LP_INPUTS   6

/* (f, q) as apu_vp.c passes them: f in 1/256..1, q in 0.0794..1. */
static const float lp_settings[LP_SETTINGS][2] = {
    { 0.003906f, 1.0f     },
    { 0.05f,     0.5f     },
    { 0.25f,     0.079407f },
    { 0.5f,      1.0f     },
    { 1.0f,      0.079407f },
    { 1.0f,      1.0f     },
};

/* 0 impulse, 1 step 0.5, 2 slow sine 0.5, 3 fast sine 0.5, 4 noise 0.5,
 * 5 full-scale sine at the setting's corner frequency (the loud resonant
 * case, where the old filter's soft limit acts). */
static const char *const lp_input_names[LP_INPUTS] = {
    "impulse", "step", "sine_slow", "sine_fast", "noise", "corner_full",
};

static inline void lp_make_input(int input, int setting, float *x)
{
    const double pi = 3.14159265358979323846;
    uint32_t seed = 0x12345678u;
    double corner = asin(lp_settings[setting][0] / 2.0) / pi; /* cycles/sample */
    for (int n = 0; n < LP_LEN; n++) {
        double v = 0.0;
        switch (input) {
        case 0: v = n == 0 ? 1.0 : 0.0; break;
        case 1: v = 0.5; break;
        case 2: v = 0.5 * sin(2.0 * pi * 0.02 * n); break;
        case 3: v = 0.5 * sin(2.0 * pi * 0.2 * n); break;
        case 4:
            seed = seed * 1664525u + 1013904223u;
            v = 0.5 * ((double)(seed >> 8) / 8388608.0 - 1.0);
            break;
        case 5: v = sin(2.0 * pi * corner * n); break;
        }
        x[n] = (float)v;
    }
}
