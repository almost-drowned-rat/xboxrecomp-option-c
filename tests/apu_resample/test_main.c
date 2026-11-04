/* apu_resample.h, driven the way voice_resample drives it: prime on two source samples, then per call ask
 * apu_resample_need for n outputs, fetch exactly that many from the source, run. Checks:
 *
 *   step 1 (pitch 0):        output = source, sample for sample, one source sample per output
 *   any step, frames of 256: output k = linear interpolation of the source at k * step (|err| <= 1e-4), and the
 *                            source consumed after K outputs is exactly floor((K - 1) * step) + 2 - nothing
 *                            fetched is dropped, so a stream drains at the pitch's speed
 *   44.1 kHz sine at 48 kHz: 1000 Hz in -> 1000 Hz out (+-0.2 %); at step 1 it came out at 1088 Hz before
 *   pitch one octave down:   a 2490 Hz tone (Burnout 3's whine at step 1) -> 1245 Hz
 *   shortfall:               fewer source samples than needed -> fewer outputs, and the next call carries on
 *                            from the same position */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "apu_resample.h"

#define PI 3.14159265358979323846

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define N_SRC 200000
static float s_src[N_SRC][2];
static int s_read;

static int fetch(float (*dst)[2], int n)
{
    int i;
    for (i = 0; i < n && s_read < N_SRC; i++, s_read++) {
        dst[i][0] = s_src[s_read][0];
        dst[i][1] = s_src[s_read][1];
    }
    return i;
}

/* voice_resample's loop: returns the outputs made. */
static int run_voice(ApuResampler *r, float (*out)[2], int total, float rate, int frame, int cap)
{
    static float buf[4096][2];
    float step = apu_resample_step(rate);
    int made = 0;

    if (!r->have) {
        if (fetch(buf, 2) < 2)
            return 0;
        apu_resample_prime(r, buf[0], buf[1]);
    }
    while (made < total) {
        int n = total - made < frame ? total - made : frame;
        int need = apu_resample_need(r, n, step), got, k;
        if (need > cap)
            need = cap;
        got = fetch(buf, need);
        k = apu_resample_run(r, (const float (*)[2])buf, got, out + made, n, step);
        made += k;
        if (k == 0)
            break;
    }
    return made;
}

static void fill_sine(double freq, double sr)
{
    for (int i = 0; i < N_SRC; i++) {
        s_src[i][0] = (float)sin(2.0 * PI * freq * i / sr);
        s_src[i][1] = (float)(0.5 * cos(2.0 * PI * freq * i / sr));
    }
}

static double freq_of(float (*x)[2], int n, double sr)
{
    int first = -1, last = -1, crossings = 0;
    for (int i = 1; i < n; i++)
        if (x[i - 1][0] < 0.0f && x[i][0] >= 0.0f) {
            if (first < 0) first = i;
            last = i;
            crossings++;
        }
    return crossings > 1 ? (crossings - 1) * sr / (last - first) : 0.0;
}

static float s_out[100000][2];

int main(void)
{
    ApuResampler r;

    /* step 1 */
    for (int i = 0; i < N_SRC; i++) { s_src[i][0] = (float)(i % 97) / 97.0f; s_src[i][1] = -(float)(i % 31); }
    apu_resample_reset(&r); s_read = 0;
    int made = run_voice(&r, s_out, 10000, 1.0f, 256, 4096);
    CHECK(made == 10000, "step 1: %d outputs", made);
    int bad = 0;
    for (int k = 0; k < made; k++)
        if (s_out[k][0] != s_src[k][0] || s_out[k][1] != s_src[k][1]) bad++;
    CHECK(bad == 0, "step 1: %d outputs differ from the source", bad);
    CHECK(s_read == 10000 + 1, "step 1: %d source samples read for 10000 outputs (want 10001)", s_read);

    /* interpolation and exact consumption, several steps */
    static const float rates[] = { 1.0884354f, 0.5f, 2.0f, 1.3f, 0.77f, 0.25f };
    for (unsigned t = 0; t < sizeof rates / sizeof rates[0]; t++) {
        for (int i = 0; i < N_SRC; i++) { s_src[i][0] = (float)sin(i * 0.01); s_src[i][1] = (float)(i % 7); }
        apu_resample_reset(&r); s_read = 0;
        float step = apu_resample_step(rates[t]);
        int K = 20000;
        made = run_voice(&r, s_out, K, rates[t], 256, 4096);
        CHECK(made == K, "rate %g: %d outputs", rates[t], made);
        double maxerr = 0;
        for (int k = 0; k < made; k++) {
            double p = (double)k * step;          /* float accumulation in the resampler: compare loosely */
            int i0 = (int)floor(p);
            double f = p - i0;
            double want = s_src[i0][0] + f * (s_src[i0 + 1][0] - s_src[i0][0]);
            double e = fabs(want - s_out[k][0]);
            if (e > maxerr) maxerr = e;
        }
        CHECK(maxerr <= 1e-3, "rate %g: interpolation error %g", rates[t], maxerr);
        long consumed = s_read;
        long expect = (long)floor((double)(K - 1) * step + 1e-6) + 2;
        CHECK(labs(consumed - expect) <= 1, "rate %g: %ld source samples for %d outputs (want %ld)",
              rates[t], consumed, K, expect);
    }

    /* 44.1 kHz material at 48 kHz: rate = 48000/44100 as the pitch encodes it */
    fill_sine(1000.0, 44100.0);
    apu_resample_reset(&r); s_read = 0;
    made = run_voice(&r, s_out, 96000, 48000.0f / 44100.0f, 256, 4096);
    double fo = freq_of(s_out, made, 48000.0);
    CHECK(fabs(fo - 1000.0) <= 2.0, "44.1k sine: %.2f Hz out (want 1000)", fo);
    for (int k = 0; k < 96000; k++) { s_out[k][0] = s_src[k][0]; }
    double f_old = freq_of(s_out, 96000, 48000.0);
    printf("44.1 kHz 1000 Hz sine at 48 kHz: %.2f Hz resampled, %.2f Hz one sample per output (old)\n", fo, f_old);

    /* one octave down */
    fill_sine(2490.0, 48000.0);
    apu_resample_reset(&r); s_read = 0;
    made = run_voice(&r, s_out, 48000, 2.0f, 256, 4096);
    fo = freq_of(s_out, made, 48000.0);
    CHECK(fabs(fo - 1245.0) <= 2.5, "octave down: %.2f Hz (want 1245)", fo);

    /* shortfall: cap the fetch, the next call continues where it stopped */
    for (int i = 0; i < N_SRC; i++) { s_src[i][0] = (float)i; s_src[i][1] = 0; }
    apu_resample_reset(&r); s_read = 0;
    made = run_voice(&r, s_out, 1000, 0.5f, 256, 50);   /* step 2: 256 outputs want ~510 source, get 50 */
    CHECK(made == 1000, "shortfall: %d outputs over several calls", made);
    bad = 0;
    for (int k = 0; k < made; k++)
        if (fabsf(s_out[k][0] - 2.0f * k) > 1e-3f) bad++;
    CHECK(bad == 0, "shortfall: %d outputs off the 2k line", bad);

    printf(failures ? "apu_resample: %d failure(s)\n" : "apu_resample: all checks passed\n", failures);
    return failures ? 1 : 0;
}
