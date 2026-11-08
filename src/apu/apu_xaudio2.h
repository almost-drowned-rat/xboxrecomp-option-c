/**
 * XAudio2 Audio Output Backend
 */
#ifndef APU_XAUDIO2_H
#define APU_XAUDIO2_H

#include <stdint.h>

/* Initialize XAudio2. Returns 1 on success, 0 on failure. */
int xa2_init(void);

/* Shut down XAudio2 and release all resources. */
void xa2_shutdown(void);

/* Returns 1 if XAudio2 is active. */
int xa2_is_active(void);

/* Submit interleaved 16-bit samples, xa2_channels() per sample frame (2, or 6 = FL FR FC LFE SL SR). Returns 1 if
 * accepted. */
int xa2_submit_samples(const int16_t *samples, int num_samples);

/* 5.1 output (option-c, 2026-11-08). RECOMP_AUDIO_51=1 (the port sets it from its setting before the boot)
 * asks for six channels; xa2_init grants it only when the output device itself has six or more (channel mask with
 * FC, LFE and two surrounds) - a stereo device keeps the stereo fold. xa2_surround(): 1 while the 5.1 output runs.
 * XC_AUDIO's Dolby Digital bit follows xa2_surround() (the title then mixes for six speakers itself); a stereo
 * device keeps the stereo answer, so its mix stays exactly today's. xa2_surround_requested(): the request alone. */
int xa2_surround(void);
int xa2_surround_requested(void);
int xa2_channels(void);

/* Get the preferred buffer size in samples. */
int xa2_get_buffer_size(void);

#endif /* APU_XAUDIO2_H */
