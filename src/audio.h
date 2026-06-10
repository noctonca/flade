/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - tiny SDL3 software mixer for FLA samples.
 * Channels reference caller-owned PCM (the VOC cache) and resample on the fly
 * to the device rate. PCM passed to audio_play must outlive playback. */
#ifndef FLADE_AUDIO_H
#define FLADE_AUDIO_H

#include <stddef.h>
#include <stdint.h>

int audio_init(float master_volume); /* 0 on success */
void audio_shutdown(void);

/* Start playing `pcm` (mono s16 at src_rate). `id` tags the channel so it can
 * be stopped later. loops <= 1 plays once. gl/gr are 0..1 channel gains. */
void audio_play(int id, const int16_t *pcm, size_t frames, int src_rate,
                int loops, float gl, float gr);
void audio_stop(int id);
void audio_stop_all(void);
int audio_active(void); /* number of channels still sounding */

/* ----- streaming audio track (ACF) ---------------------------------------
 * A whole-movie PCM track played on its own device stream, started from an
 * arbitrary sample offset (for seeking). Independent of the cue mixer above.
 * `pcm` must outlive playback. SDL_INIT_AUDIO must already be up. */
int audio_stream_start(const int16_t *pcm, size_t frames, int rate, int channels,
                       size_t start_frame, float volume); /* 0 on success */
void audio_stream_set_paused(int paused);
void audio_stream_stop(void);

#endif /* FLADE_AUDIO_H */
