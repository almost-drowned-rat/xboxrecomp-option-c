/* apu_lowpass.h against the filter it replaced (xemu vp/svf.h, GPLv2, no
 * longer in the tree): its outputs for the fixed vectors in vectors.h are
 * stored in old_svf_ref.h. The new chain, run the way apu_vp.c runs it (one
 * setting per 32-sample frame, state kept; filter, apu_lowpass_limit, then
 * the [-1, 1] clamp), is compared with the old filter followed by the same
 * clamp and must stay within:
 *
 *   inputs up to half scale, impulse, noise:  |new - old| <= 1e-3 per sample
 *                                             and energy within +-0.01 dB
 *   full-scale sine on the resonant corner:   |new - old| <= 0.1 per sample
 *                                             and energy within +-0.1 dB
 *
 * (measured 2026-10-24: 6.1e-4 / +0.005 dB and 0.049 / -0.049 dB; without the
 * soft limit the second was 0.153 / +0.41 dB, from the old filter's soft limit
 * on the band integrator). Plus: the limit's shape, the full-scale resonant
 * case never reaching the hard clamp, an impulse reaching the output in the
 * same sample, a non-finite state cleared, f held under the stability limit. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "apu_lowpass.h"
#include "vectors.h"
#include "old_svf_ref.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void run(int s, const float *x, float *y)
{
    ApuLowpass lp;
    memset(&lp, 0, sizeof lp);
    for (int fr = 0; fr < LP_LEN / LP_FRAME; fr++) {
        ApuLowpassCoef c = apu_lowpass_set(lp_settings[s][0], lp_settings[s][1]);
        apu_lowpass_sanitize(&lp);
        for (int k = 0; k < LP_FRAME; k++) {
            int n = fr * LP_FRAME + k;
            /* as apu_vp.c: filter, soft limit, then the hard clamp */
            y[n] = fminf(fmaxf(apu_lowpass_limit(apu_lowpass_step(&lp, &c, x[n])), -1.0f), 1.0f);
        }
    }
}

int main(void)
{
    double worst[2] = { 0, 0 }, worst_db[2] = { 0, 0 };

    for (int s = 0; s < LP_SETTINGS; s++) {
        for (int i = 0; i < LP_INPUTS; i++) {
            float x[LP_LEN], y[LP_LEN];
            const float *ref = old_svf_ref[s][i];
            int loud = (i == 5);
            double tol = loud ? 0.1 : 1e-3, tol_db = loud ? 0.1 : 0.01;
            double maxd = 0, e_old = 0, e_new = 0;

            lp_make_input(i, s, x);
            run(s, x, y);
            for (int n = 0; n < LP_LEN; n++) {
                double r = fmin(fmax(ref[n], -1.0), 1.0);   /* old: hard clamp only */
                double d = fabs((double)y[n] - r);
                if (d > maxd) maxd = d;
                e_old += r * r;
                e_new += (double)y[n] * y[n];
            }
            double db = e_old > 0 ? 10.0 * log10(e_new / e_old) : 0.0;
            if (maxd > worst[loud]) worst[loud] = maxd;
            if (fabs(db) > fabs(worst_db[loud])) worst_db[loud] = db;
            CHECK(maxd <= tol, "setting %d (f=%g q=%g) %s: max deviation %.3g > %.3g",
                  s, lp_settings[s][0], lp_settings[s][1], lp_input_names[i], maxd, tol);
            CHECK(fabs(db) <= tol_db, "setting %d (f=%g q=%g) %s: energy %+.4f dB, limit +-%.2f",
                  s, lp_settings[s][0], lp_settings[s][1], lp_input_names[i], db, tol_db);
        }
    }
    printf("apu_lowpass vs old filter: worst |dev| %.3g / %+.4f dB (normal), %.3g / %+.4f dB (full-scale resonant)\n",
           worst[0], worst_db[0], worst[1], worst_db[1]);

    /* Same-sample response. */
    {
        ApuLowpass lp = { 0 };
        ApuLowpassCoef c = apu_lowpass_set(0.5f, 1.0f);
        CHECK(apu_lowpass_step(&lp, &c, 1.0f) != 0.0f, "an impulse reaches the output in its own sample");
    }
    /* A non-finite state is cleared, a finite one kept. */
    {
        ApuLowpass lp = { NAN, 0.25f };
        apu_lowpass_sanitize(&lp);
        CHECK(lp.band == 0.0f && lp.low == 0.0f, "a NaN state is cleared");
        lp.band = 0.5f; lp.low = 0.25f;
        apu_lowpass_sanitize(&lp);
        CHECK(lp.band == 0.5f && lp.low == 0.25f, "a finite state is kept");
    }
    /* Soft limit: identity up to the knee, monotonic, slope ~1 at the knee,
     * never beyond +-1, odd. */
    {
        int ok_id = 1, ok_mono = 1, ok_bound = 1, ok_odd = 1;
        float prev = -INFINITY;
        for (int k = -40000; k <= 40000; k++) {
            float x = k * 0.0001f;              /* -4 .. 4 */
            float y = apu_lowpass_limit(x);
            if (fabsf(x) <= APU_LOWPASS_KNEE && y != x) ok_id = 0;
            if (y < prev) ok_mono = 0;
            if (fabsf(y) > 1.0f) ok_bound = 0;
            if (apu_lowpass_limit(-x) != -y) ok_odd = 0;
            prev = y;
        }
        CHECK(ok_id, "the limit leaves |x| <= knee unchanged");
        CHECK(ok_mono, "the limit is monotonic");
        CHECK(ok_bound, "the limit never leaves [-1, 1]");
        CHECK(ok_odd, "the limit is symmetric");
        float e = 1e-3f, k = APU_LOWPASS_KNEE;
        float slope = (apu_lowpass_limit(k + e) - apu_lowpass_limit(k)) / e;
        CHECK(fabsf(slope - 1.0f) < 0.01f, "slope 1 at the knee: %g", slope);
        CHECK(fabsf(apu_lowpass_limit(1.0f) - 0.9402f) < 1e-3f, "full scale -> 0.940: %g", apu_lowpass_limit(1.0f));
        CHECK(apu_lowpass_limit(1e30f) <= 1.0f && isnan(apu_lowpass_limit(NAN)), "huge input <= 1, NaN passes");
    }
    /* The full-scale resonant case no longer clips hard: the old filter's
     * output above full scale was cut flat by the clamp; filter + limit stays
     * below 1 everywhere. */
    {
        int flat_old = 0, flat_new = 0;
        for (int s = 0; s < LP_SETTINGS; s++) {
            float x[LP_LEN], y[LP_LEN];
            lp_make_input(5, s, x);
            run(s, x, y);
            for (int n = 0; n < LP_LEN; n++) {
                if (fabsf(old_svf_ref[s][5][n]) >= 1.0f) flat_old++;
                if (fabsf(y[n]) >= 1.0f) flat_new++;
            }
        }
        printf("full-scale resonant: %d old samples clipped hard, %d new\n", flat_old, flat_new);
        CHECK(flat_old > 0, "the vectors do drive the old filter into the clamp");
        CHECK(flat_new == 0, "filter + limit never reaches the hard clamp (%d samples)", flat_new);
    }
    /* Stability: f*f + 2*f*q < 4 after the clamp, for any input. */
    {
        float qs[] = { 0.0f, 0.5f, 1.0f, 2.0f, 5.0f };
        for (unsigned k = 0; k < sizeof qs / sizeof qs[0]; k++) {
            ApuLowpassCoef c = apu_lowpass_set(10.0f, qs[k]);
            CHECK(c.f * c.f + 2.0f * c.f * c.q < 4.0f, "f clamped under the stability limit at q=%g", qs[k]);
        }
        ApuLowpass lp = { 0 };
        ApuLowpassCoef c = apu_lowpass_set(10.0f, 0.0f);
        float out = 0.0f;
        for (int n = 0; n < 100000; n++) out = apu_lowpass_step(&lp, &c, n == 0 ? 1.0f : 0.0f);
        CHECK(isfinite(out) && fabsf(out) < 10.0f, "an undamped, clamped filter stays bounded: %g", out);
    }

    if (failures) { printf("apu_lowpass_test: %d failure(s)\n", failures); return 1; }
    printf("apu_lowpass_test: all checks passed\n");
    return 0;
}
