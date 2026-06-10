/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors
 *
 * Smacker playback via libsmacker (LGPL 2.1, vendored in libsmacker/). Video is
 * forward-decoded per frame. Audio tracks (0 = music, 1.. = one voice per
 * language) are decoded up front and each normalised to a common format
 * (matched to the video duration); the music becomes the movie's streaming
 * track and the voices are exposed separately so the player can switch
 * language live on its own audio channel. */
#include "smk.h"
#include "libsmacker/smacker.h"

#include <stdlib.h>
#include <string.h>

#define SMK_MAX_VOICE 6

typedef struct {
    smk h;
    unsigned long frame_count;
    unsigned long cur;

    int16_t *music;                    /* track 0, normalised; = movie.audio_pcm */
    int16_t *voice[SMK_MAX_VOICE];     /* one per language voice track */
    int voice_track[SMK_MAX_VOICE];    /* original smk track number of each */
    int nvoice;
    int default_voice;                 /* index into voice[], or -1 */
    size_t aframes;
    int arate, achannels;
} smk_ctx;

/* Preferred voice track (1..6), or -1 for "the first present". */
static int g_pref_voice = -1;
void smk_set_voice(int track) {
    g_pref_voice = track;
}

static int append_bytes(uint8_t **buf, size_t *len, size_t *cap, const uint8_t *src, size_t n) {
    if (*len + n > *cap) {
        size_t nc = *cap ? *cap * 2 : (1u << 16);
        while (nc < *len + n)
            nc *= 2;
        uint8_t *nb = realloc(*buf, nc);
        if (!nb)
            return -1;
        *buf = nb;
        *cap = nc;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    return 0;
}

/* Resample/channel-convert one raw track to out_frames of interleaved s16 at
 * out_ch channels (mono is duplicated to every channel; rate via nearest). */
static int16_t *normalize_track(const uint8_t *tb, size_t tlen, int tch, int tbd, int trate,
                                int out_rate, int out_ch, size_t out_frames) {
    int16_t *out = malloc(out_frames * (size_t)out_ch * sizeof(int16_t));
    if (!out)
        return NULL;
    int b16 = tbd >= 16, bps = b16 ? 2 : 1;
    size_t tframes = tlen / (size_t)(bps * tch);
    double step = (trate ? (double)trate : out_rate) / out_rate;
    for (size_t of = 0; of < out_frames; of++) {
        size_t sf = (size_t)(of * step);
        for (int c = 0; c < out_ch; c++) {
            int s = 0;
            if (sf < tframes) {
                int sc = c < tch ? c : tch - 1;
                size_t idx = sf * (size_t)tch + (size_t)sc;
                s = b16 ? (int16_t)(tb[idx * 2] | (tb[idx * 2 + 1] << 8)) : (((int)tb[idx] - 128) * 256);
            }
            out[of * (size_t)out_ch + (size_t)c] = (int16_t)s;
        }
    }
    return out;
}

static void extract_audio(smk_ctx *c, movie_t *m) {
    smk h = c->h;
    unsigned char mask = 0, ch[7] = {0}, bd[7] = {0};
    unsigned long rate[7] = {0};
    smk_info_audio(h, &mask, ch, bd, rate);
    if (!mask)
        return;

    for (int t = 0; t < 7; t++)
        if (mask & (1 << t))
            smk_enable_audio(h, (unsigned char)t, 1);
    smk_enable_video(h, 0); /* audio-only decode pass (fast); caller re-enables */

    uint8_t *tbuf[7] = {0};
    size_t tlen[7] = {0}, tcap[7] = {0};
    if (smk_first(h) == SMK_ERROR)
        return;
    for (unsigned long f = 0; f < c->frame_count; f++) {
        for (int t = 0; t < 7; t++)
            if (mask & (1 << t)) {
                const unsigned char *a = smk_get_audio(h, (unsigned char)t);
                unsigned long asz = smk_get_audio_size(h, (unsigned char)t);
                if (a && asz)
                    append_bytes(&tbuf[t], &tlen[t], &tcap[t], a, asz);
            }
        if (f + 1 < c->frame_count && smk_next(h) == SMK_ERROR)
            break; /* stream exhausted early (e.g. a forged frame count) */
    }

    /* Clamp sample rates to a sane PCM range: a forged rate (huge or tiny)
     * would otherwise blow up out_frames below and request a giant buffer. */
    int out_ch = 1, out_rate = 0;
    for (int t = 0; t < 7; t++)
        if (tlen[t]) {
            int tch = ch[t] ? ch[t] : 1;
            if (tch > out_ch)
                out_ch = tch;
            if (rate[t] < 2000 || rate[t] > 48000)
                rate[t] = 22050;
            if ((int)rate[t] > out_rate)
                out_rate = (int)rate[t];
        }
    if (out_rate <= 0)
        out_rate = 22050;

    size_t out_frames = 0;
    for (int t = 0; t < 7; t++)
        if (tlen[t]) {
            int tch = ch[t] ? ch[t] : 1, bps = bd[t] >= 16 ? 2 : 1;
            size_t tframes = tlen[t] / (size_t)(bps * tch);
            size_t of = (size_t)((double)tframes * out_rate / (rate[t] ? (double)rate[t] : out_rate));
            if (of > out_frames)
                out_frames = of;
        }
    /* bail on absurd audio length (~12 min at 22 kHz is far past any cinematic) */
    if (out_frames == 0 || out_frames > 16u * 1024 * 1024) {
        for (int t = 0; t < 7; t++)
            free(tbuf[t]);
        return;
    }

    /* track 0 = music; tracks 1.. = voices. Each normalised separately - they
     * play as independent streams that SDL mixes, so the active voice can be
     * swapped live without touching the music. */
    if (tlen[0])
        c->music = normalize_track(tbuf[0], tlen[0], ch[0] ? ch[0] : 1, bd[0], (int)rate[0],
                                   out_rate, out_ch, out_frames);
    for (int t = 1; t < 7; t++)
        if (tlen[t] && c->nvoice < SMK_MAX_VOICE) {
            int16_t *v = normalize_track(tbuf[t], tlen[t], ch[t] ? ch[t] : 1, bd[t], (int)rate[t],
                                         out_rate, out_ch, out_frames);
            if (v) {
                c->voice[c->nvoice] = v;
                c->voice_track[c->nvoice] = t;
                c->nvoice++;
            }
        }
    for (int t = 0; t < 7; t++)
        free(tbuf[t]);

    c->aframes = out_frames;
    c->arate = out_rate;
    c->achannels = out_ch;

    c->default_voice = -1;
    if (g_pref_voice >= 1)
        for (int i = 0; i < c->nvoice; i++)
            if (c->voice_track[i] == g_pref_voice)
                c->default_voice = i;
    if (c->default_voice < 0 && c->nvoice > 0)
        c->default_voice = 0;

    int16_t *track = c->music ? c->music : (c->nvoice ? c->voice[0] : NULL);
    if (track) {
        m->audio_pcm = track;
        m->audio_frames = out_frames;
        m->audio_rate = out_rate;
        m->audio_channels = out_ch;
    }
}

int smk_get_voices(movie_t *m, smk_voices *v) {
    if (!m || m->kind != MOVIE_SMK)
        return 0;
    smk_ctx *c = (smk_ctx *)m->impl;
    v->count = c->nvoice;
    for (int i = 0; i < c->nvoice; i++) {
        v->pcm[i] = c->voice[i];
        v->track[i] = c->voice_track[i];
    }
    v->frames = c->aframes;
    v->rate = c->arate;
    v->channels = c->achannels;
    v->default_index = c->default_voice;
    return c->nvoice;
}

static int smk_movie_step(movie_t *m, movie_frame *out) {
    smk_ctx *c = (smk_ctx *)m->impl;
    if (c->cur >= c->frame_count)
        return 0;
    char r = (c->cur == 0) ? smk_first(c->h) : smk_next(c->h);
    if (r == SMK_ERROR)
        return 0;
    out->pixels = smk_get_video(c->h);
    out->palette = smk_get_palette(c->h);
    out->duration = (m->fps > 0) ? 1.0 / m->fps : 1.0 / 15.0;
    c->cur++;
    return 1;
}

static void smk_movie_close(movie_t *m) {
    smk_ctx *c = (smk_ctx *)m->impl;
    if (c) {
        if (c->h)
            smk_close(c->h);
        free(c->music);
        for (int i = 0; i < c->nvoice; i++)
            free(c->voice[i]);
        free(c);
    }
    m->impl = NULL;
}

int smk_movie_open(movie_t *m, const uint8_t *data, size_t size) {
    /* Pre-validate the header before libsmacker allocates from it: a forged
     * width/height/frame-count would otherwise drive a multi-GB allocation
     * inside smk_open_memory. Smacker header: magic[4], then LE u32 w, h, frames. */
    if (size < 16)
        return -1;
    uint32_t hw = data[4] | (data[5] << 8) | (data[6] << 16) | ((uint32_t)data[7] << 24);
    uint32_t hh0 = data[8] | (data[9] << 8) | (data[10] << 16) | ((uint32_t)data[11] << 24);
    uint32_t hf = data[12] | (data[13] << 8) | (data[14] << 16) | ((uint32_t)data[15] << 24);
    if (hw == 0 || hw > 4096 || hh0 == 0 || hh0 > 4096 || hf == 0 || hf > 100000)
        return -1;

    smk h = smk_open_memory(data, size);
    if (!h)
        return -1;
    smk_ctx *c = calloc(1, sizeof(*c));
    if (!c) {
        smk_close(h);
        return -1;
    }
    c->h = h;

    unsigned long frame_count = 0, w = 0, hh = 0;
    double usf = 0;
    smk_info_all(h, NULL, &frame_count, &usf);
    smk_info_video(h, &w, &hh, NULL);
    /* reject absurd dimensions / frame counts before they drive huge allocations
     * (a forged Smacker header would otherwise OOM during audio extraction) */
    if (frame_count > 100000u || w == 0 || w > 8192 || hh == 0 || hh > 8192) {
        smk_close(h);
        free(c);
        return -1;
    }
    c->frame_count = frame_count;

    m->kind = MOVIE_SMK;
    m->width = (int)w;
    m->height = (int)hh;
    m->num_frames = (int)frame_count;
    m->fps = (usf > 0) ? 1e6 / usf : 15.0;
    m->step = smk_movie_step;
    m->close = smk_movie_close;
    m->impl = c;

    extract_audio(c, m);    /* audio-only decode pass (video disabled inside) */
    smk_enable_video(h, 1); /* (re)enable for the player's forward video pass */
    c->cur = 0;             /* step() will smk_first to restart at frame 0 */
    return 0;
}
