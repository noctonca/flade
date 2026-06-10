/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
#include "audio.h"

#include <SDL3/SDL.h>

#define NUM_CHANNELS 16
#define DEVICE_RATE 44100

typedef struct {
    const int16_t *pcm;
    size_t frames;
    double pos;  /* fractional read position */
    double step; /* src_rate / device_rate */
    int loops;   /* remaining plays */
    float gl, gr;
    int id;
    int active;
} channel_t;

static channel_t g_chan[NUM_CHANNELS];
static SDL_AudioStream *g_stream;
static SDL_Mutex *g_mutex;
static float g_master = 1.0f;

static void SDLCALL mix_cb(void *ud, SDL_AudioStream *stream, int additional, int total) {
    (void)ud;
    (void)total;
    if (additional <= 0)
        return;
    int frames = additional / (int)(sizeof(int16_t) * 2);
    int16_t *out = SDL_malloc((size_t)frames * 2 * sizeof(int16_t));
    if (!out)
        return;

    SDL_LockMutex(g_mutex);
    for (int i = 0; i < frames; i++) {
        float l = 0.0f, r = 0.0f;
        for (int c = 0; c < NUM_CHANNELS; c++) {
            channel_t *ch = &g_chan[c];
            if (!ch->active)
                continue;
            size_t idx = (size_t)ch->pos;
            if (idx >= ch->frames) {
                if (--ch->loops > 0) {
                    ch->pos = 0.0;
                    idx = 0;
                } else {
                    ch->active = 0;
                    continue;
                }
            }
            float s = (float)ch->pcm[idx];
            l += s * ch->gl;
            r += s * ch->gr;
            ch->pos += ch->step;
        }
        l *= g_master;
        r *= g_master;
        if (l > 32767.0f) l = 32767.0f;
        if (l < -32768.0f) l = -32768.0f;
        if (r > 32767.0f) r = 32767.0f;
        if (r < -32768.0f) r = -32768.0f;
        out[i * 2] = (int16_t)l;
        out[i * 2 + 1] = (int16_t)r;
    }
    SDL_UnlockMutex(g_mutex);

    SDL_PutAudioStreamData(stream, out, frames * 2 * (int)sizeof(int16_t));
    SDL_free(out);
}

int audio_init(float master_volume) {
    g_master = master_volume;
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = DEVICE_RATE;

    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, mix_cb, NULL);
    if (!g_stream)
        return -1;
    g_mutex = SDL_CreateMutex();
    if (!g_mutex)
        return -1;
    SDL_ResumeAudioStreamDevice(g_stream);
    return 0;
}

void audio_shutdown(void) {
    if (g_stream) {
        SDL_DestroyAudioStream(g_stream);
        g_stream = NULL;
    }
    if (g_mutex) {
        SDL_DestroyMutex(g_mutex);
        g_mutex = NULL;
    }
}

void audio_play(int id, const int16_t *pcm, size_t frames, int src_rate,
                int loops, float gl, float gr) {
    if (!g_mutex || !pcm || frames == 0)
        return;
    SDL_LockMutex(g_mutex);
    int slot = -1;
    for (int c = 0; c < NUM_CHANNELS; c++) {
        if (!g_chan[c].active) {
            slot = c;
            break;
        }
    }
    if (slot < 0)
        slot = 0; /* steal channel 0 if everything is busy */
    channel_t *ch = &g_chan[slot];
    ch->pcm = pcm;
    ch->frames = frames;
    ch->pos = 0.0;
    ch->step = (double)src_rate / (double)DEVICE_RATE;
    ch->loops = loops > 1 ? loops : 1;
    ch->gl = gl;
    ch->gr = gr;
    ch->id = id;
    ch->active = 1;
    SDL_UnlockMutex(g_mutex);
}

void audio_stop(int id) {
    if (!g_mutex)
        return;
    SDL_LockMutex(g_mutex);
    for (int c = 0; c < NUM_CHANNELS; c++)
        if (g_chan[c].active && g_chan[c].id == id)
            g_chan[c].active = 0;
    SDL_UnlockMutex(g_mutex);
}

void audio_stop_all(void) {
    if (!g_mutex)
        return;
    SDL_LockMutex(g_mutex);
    for (int c = 0; c < NUM_CHANNELS; c++)
        g_chan[c].active = 0;
    SDL_UnlockMutex(g_mutex);
}

int audio_active(void) {
    if (!g_mutex)
        return 0;
    int n = 0;
    SDL_LockMutex(g_mutex);
    for (int c = 0; c < NUM_CHANNELS; c++)
        if (g_chan[c].active)
            n++;
    SDL_UnlockMutex(g_mutex);
    return n;
}

/* ----- streaming audio track (ACF) ---------------------------------------- */
static SDL_AudioStream *g_music;

int audio_stream_start(const int16_t *pcm, size_t frames, int rate, int channels,
                       size_t start_frame, float volume) {
    audio_stream_stop();
    if (!pcm || frames == 0 || channels <= 0)
        return -1;

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = channels;
    spec.freq = rate;

    /* the device resamples this src spec to whatever the hardware wants */
    g_music = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!g_music)
        return -1;
    SDL_SetAudioStreamGain(g_music, volume);

    if (start_frame > frames)
        start_frame = frames;
    size_t off = start_frame * (size_t)channels;
    size_t n = (frames - start_frame) * (size_t)channels;
    if (n)
        SDL_PutAudioStreamData(g_music, pcm + off, (int)(n * sizeof(int16_t)));
    SDL_ResumeAudioStreamDevice(g_music);
    return 0;
}

void audio_stream_set_paused(int paused) {
    if (!g_music)
        return;
    if (paused)
        SDL_PauseAudioStreamDevice(g_music);
    else
        SDL_ResumeAudioStreamDevice(g_music);
}

void audio_stream_stop(void) {
    if (g_music) {
        SDL_DestroyAudioStream(g_music);
        g_music = NULL;
    }
}
