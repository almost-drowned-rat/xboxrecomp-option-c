/**
 * XAudio2 Audio Output Backend
 *
 * Provides low-latency audio output via XAudio2 (Win7+).
 * Called from the APU monitor frame to submit mixed samples.
 * Falls back gracefully if XAudio2 is unavailable.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apu_xaudio2.h"

/* The XAudio2 backend is Windows-only. On Linux all xa2_* functions are
 * stubbed to report inactive; real audio output via SDL2 comes later. */
#if defined(_WIN32)

#define COBJMACROS
#include <windows.h>
#include <xaudio2.h>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

#define XA2_SAMPLE_RATE   48000
#define XA2_CHANNELS      g_xa2_channels
#define XA2_BUF_SAMPLES   1024   /* ~21ms per submission */
#define XA2_NUM_BUFS      3
#define XA2_MASK_51       (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | \
                           SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT)

static IXAudio2               *g_xa2 = NULL;
static IXAudio2MasteringVoice *g_xa2_master = NULL;
static IXAudio2SourceVoice    *g_xa2_source = NULL;
static int                     g_xa2_channels = 2;
extern volatile int            g_xbox_audio_surround;   /* kernel_xbox.c: XC_AUDIO's Dolby Digital bit */
static int16_t                 g_xa2_bufs[XA2_NUM_BUFS][XA2_BUF_SAMPLES][6];

int xa2_surround_requested(void)
{
    static int req = -1;
    if (req < 0) {
        const char *e = getenv("RECOMP_AUDIO_51");
        req = e && *e && *e != '0';
    }
    return req;
}

int xa2_surround(void) { return g_xa2_channels == 6; }
int xa2_channels(void) { return g_xa2_channels; }
static int                     g_xa2_next_buf = 0;
static int                     g_xa2_initialized = 0;
static int                     g_xa2_frames_written = 0;

int xa2_init(void)
{
    HRESULT hr;
    int com_initialized;
    WAVEFORMATEX wfx = { 0 };

    if (g_xa2_initialized) return 1;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "[XA2] CoInitializeEx failed: 0x%08lX\n", hr);
        return 0;
    }
    com_initialized = SUCCEEDED(hr);

    hr = XAudio2Create(&g_xa2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr) || !g_xa2) {
        fprintf(stderr, "[XA2] XAudio2Create failed: 0x%08lX\n", hr);
        goto fail;
    }

    g_xa2_channels = 2;
    if (xa2_surround_requested()) {
        /* 5.1 only on a device that has the speakers: its own channel layout decides (mastering voice at the
         * device's channel count, then its mask) - never XAudio2's own downmix of six channels to two */
        DWORD mask = 0;
        XAUDIO2_VOICE_DETAILS det;
        hr = IXAudio2_CreateMasteringVoice(g_xa2, &g_xa2_master, XAUDIO2_DEFAULT_CHANNELS, XA2_SAMPLE_RATE, 0, NULL,
                                           NULL, 0);
        if (SUCCEEDED(hr)) {
            IXAudio2MasteringVoice_GetVoiceDetails(g_xa2_master, &det);
            IXAudio2MasteringVoice_GetChannelMask(g_xa2_master, &mask);
            {
                const DWORD front = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
                                    SPEAKER_LOW_FREQUENCY;
                const DWORD back = SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
                const DWORD side = SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;
                const char *e = getenv("RECOMP_AUDIO_51");
                if (det.InputChannels >= 6 && (mask & front) == front &&
                    ((mask & back) == back || (mask & side) == side))
                    g_xa2_channels = 6;
                else if (e && !strcmp(e, "force"))         /* tests only: the six-channel path on a stereo device */
                    g_xa2_channels = 6;
            }
            fprintf(stderr, "[XA2] 5.1 asked: device %u channels, mask 0x%lX -> %s\n", det.InputChannels,
                    (unsigned long)mask, g_xa2_channels == 6 ? "5.1 output" : "stereo (the device has no 5.1)");
            if (g_xa2_channels != 6) {
                g_xa2_master->lpVtbl->DestroyVoice(g_xa2_master);
                g_xa2_master = NULL;
            }
        }
    }
    if (!g_xa2_master)
        hr = IXAudio2_CreateMasteringVoice(g_xa2, &g_xa2_master,
            XA2_CHANNELS, XA2_SAMPLE_RATE, 0, NULL, NULL, 0);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] CreateMasteringVoice failed: 0x%08lX\n", hr);
        goto fail;
    }

    WAVEFORMATEXTENSIBLE wfe = { 0 };
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = (WORD)XA2_CHANNELS;
    wfx.nSamplesPerSec  = XA2_SAMPLE_RATE;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = (WORD)(XA2_CHANNELS * 2);
    wfx.nAvgBytesPerSec = XA2_SAMPLE_RATE * wfx.nBlockAlign;
    if (XA2_CHANNELS == 6) {
        /* six channels need the extensible format with the speaker mask: FL FR FC LFE BL BR = the GP's FL FR FC
         * LFE SL SR (XAudio2 maps them onto a side-surround device's SL SR itself) */
        static const GUID pcm = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
        wfe.Format = wfx;
        wfe.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wfe.Format.cbSize = sizeof wfe - sizeof wfe.Format;
        wfe.Samples.wValidBitsPerSample = 16;
        wfe.dwChannelMask = XA2_MASK_51;
        wfe.SubFormat = pcm;
    }

    hr = IXAudio2_CreateSourceVoice(g_xa2, &g_xa2_source,
        XA2_CHANNELS == 6 ? &wfe.Format : &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] CreateSourceVoice failed: 0x%08lX\n", hr);
        goto fail;
    }

    hr = IXAudio2SourceVoice_Start(g_xa2_source, 0, XAUDIO2_COMMIT_NOW);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] Start failed: 0x%08lX\n", hr);
        goto fail;
    }

    g_xa2_next_buf = 0;
    g_xa2_initialized = 1;
    g_xbox_audio_surround = XA2_CHANNELS == 6;
    g_xa2_frames_written = 0;

    fprintf(stderr, "[XA2] XAudio2 initialized (%d Hz %s 16-bit, %d x %d-sample buffers)\n",
            XA2_SAMPLE_RATE, XA2_CHANNELS == 6 ? "5.1" : "stereo", XA2_NUM_BUFS, XA2_BUF_SAMPLES);
    return 1;

fail:
    xa2_shutdown();
    /* Failed initialization still runs on the COM-initializing thread. */
    if (com_initialized) CoUninitialize();
    return 0;
}

void xa2_shutdown(void)
{
    if (g_xa2_source) {
        IXAudio2SourceVoice_Stop(g_xa2_source, 0, XAUDIO2_COMMIT_NOW);
        IXAudio2SourceVoice_FlushSourceBuffers(g_xa2_source);
        g_xa2_source->lpVtbl->DestroyVoice(g_xa2_source);
        g_xa2_source = NULL;
    }
    if (g_xa2_master) {
        g_xa2_master->lpVtbl->DestroyVoice(g_xa2_master);
        g_xa2_master = NULL;
    }
    if (g_xa2) {
        IXAudio2_Release(g_xa2);
        g_xa2 = NULL;
    }

    if (g_xa2_initialized)
        fprintf(stderr, "[XA2] Shut down (%d frames written)\n", g_xa2_frames_written);
    g_xa2_initialized = 0;
    g_xa2_channels = 2;
}

int xa2_is_active(void)
{
    return g_xa2_initialized;
}

/* RECOMP_APU_WAV=<file>: record what goes to XAudio2 (48 kHz stereo s16),
 * as the oracle for the DirectSound HLE's RECOMP_DSOUND_WAV. */
static void apu_wav(const int16_t *samples, int frames)
{
    static FILE *f;
    static int tried;
    static uint32_t bytes;
    if (!tried) {
        const char *p = getenv("RECOMP_APU_WAV");
        tried = 1;
        if (p && *p && (f = fopen(p, "wb")) != NULL) {
            static const uint8_t hdr[44] = {
                'R','I','F','F', 36,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0,
                1,0, 2,0, 0x80,0xBB,0,0, 0x00,0xEE,0x02,0, 4,0, 16,0, 'd','a','t','a', 0,0,0,0 };
            fwrite(hdr, 1, sizeof hdr, f);
        }
    }
    if (!f || frames <= 0)
        return;
    fwrite(samples, 4, (size_t)frames, f);
    bytes += (uint32_t)frames * 4u;
    {
        long at = ftell(f);
        uint32_t riff = 36u + bytes;
        fseek(f, 4, SEEK_SET);  fwrite(&riff, 4, 1, f);
        fseek(f, 40, SEEK_SET); fwrite(&bytes, 4, 1, f);
        fseek(f, at, SEEK_SET);
    }
}

/* Submit a buffer of mixed samples to XAudio2.
 * Called from APU frame thread. Returns 1 if buffer was submitted. */
int xa2_submit_samples(const int16_t *samples, int num_samples)
{
    XAUDIO2_VOICE_STATE state;
    XAUDIO2_BUFFER xbuf;
    int idx;
    int copy_samples;
    HRESULT hr;

    if (!g_xa2_initialized || !g_xa2_source) return 0;

    IXAudio2SourceVoice_GetState(g_xa2_source, &state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if ((int)state.BuffersQueued >= XA2_NUM_BUFS) return 0;

    idx = g_xa2_next_buf;
    copy_samples = (num_samples > XA2_BUF_SAMPLES) ? XA2_BUF_SAMPLES : num_samples;
    memcpy(g_xa2_bufs[idx], samples, copy_samples * XA2_CHANNELS * sizeof(int16_t));

    memset(&xbuf, 0, sizeof(xbuf));
    xbuf.AudioBytes = copy_samples * XA2_CHANNELS * sizeof(int16_t);
    xbuf.pAudioData = (const BYTE *)g_xa2_bufs[idx];

    hr = IXAudio2SourceVoice_SubmitSourceBuffer(g_xa2_source, &xbuf, NULL);
    if (FAILED(hr)) return 0;

    if (XA2_CHANNELS == 2)
        apu_wav(samples, copy_samples);           /* the WAV oracle is stereo */
    g_xa2_next_buf = (idx + 1) % XA2_NUM_BUFS;
    g_xa2_frames_written++;
    return 1;
}

int xa2_get_buffer_size(void)
{
    return XA2_BUF_SAMPLES;
}

#else /* !_WIN32 -- POSIX stubs (no audio output yet) */

int  xa2_init(void)                                   { return 0; }
void xa2_shutdown(void)                               {}
int  xa2_is_active(void)                              { return 0; }
int  xa2_submit_samples(const int16_t *s, int n)      { (void)s; (void)n; return 0; }
int  xa2_get_buffer_size(void)                        { return 0; }
int  xa2_surround(void)                               { return 0; }
int  xa2_surround_requested(void)                     { return 0; }
int  xa2_channels(void)                               { return 2; }

#endif /* _WIN32 */
