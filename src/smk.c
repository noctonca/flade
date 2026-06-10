/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors
 *
 * Smacker playback via libsmacker (LGPL 2.1, vendored in libsmacker/). The
 * video is forward-decoded per frame; the audio tracks (music + voice) are
 * decoded up front, mixed, and exposed as one streaming track matched to the
 * video duration - the same model the ACF path uses. */
#include "smk.h"
#include "libsmacker/smacker.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    smk h;
    unsigned long frame_count;
    unsigned long cur;
    int16_t *audio; /* owned; also referenced by movie.audio_pcm */
} smk_ctx;

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

/* Decode every frame once, concatenating each audio track, then mix the tracks
 * (music + voice) sample-wise into one interleaved s16 buffer. */
static void extract_audio(smk_ctx *c, movie_t *m) {
    smk h = c->h;
    unsigned char mask = 0, ch[7] = {0}, bd[7] = {0};
    unsigned long rate[7] = {0};
    smk_info_audio(h, &mask, ch, bd, rate);
    if (!mask)
        return;

    int first = -1;
    for (int t = 0; t < 7; t++)
        if (mask & (1 << t)) {
            smk_enable_audio(h, (unsigned char)t, 1);
            if (first < 0)
                first = t;
        }
    if (first < 0)
        return;
    /* decode audio only here - skip the (costly) video pass; the player
     * decodes video later. Caller re-enables video after this returns. */
    smk_enable_video(h, 0);
    int out_ch = ch[first] ? ch[first] : 1;
    int out_rate = rate[first] ? (int)rate[first] : 22050;

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
        if (f + 1 < c->frame_count)
            smk_next(h);
    }

    size_t max_samples = 0;
    for (int t = 0; t < 7; t++)
        if (tlen[t]) {
            size_t ns = (bd[t] >= 16) ? tlen[t] / 2 : tlen[t];
            if (ns > max_samples)
                max_samples = ns;
        }
    if (max_samples == 0) {
        for (int t = 0; t < 7; t++)
            free(tbuf[t]);
        return;
    }

    int32_t *acc = calloc(max_samples, sizeof(int32_t));
    if (!acc) {
        for (int t = 0; t < 7; t++)
            free(tbuf[t]);
        return;
    }
    for (int t = 0; t < 7; t++) {
        if (!tlen[t])
            continue;
        if (bd[t] >= 16) {
            size_t ns = tlen[t] / 2;
            const uint8_t *p = tbuf[t];
            for (size_t i = 0; i < ns; i++)
                acc[i] += (int16_t)(p[i * 2] | (p[i * 2 + 1] << 8));
        } else {
            for (size_t i = 0; i < tlen[t]; i++)
                acc[i] += ((int)tbuf[t][i] - 128) << 8;
        }
        free(tbuf[t]);
    }

    int16_t *out = malloc(max_samples * sizeof(int16_t));
    if (!out) {
        free(acc);
        return;
    }
    for (size_t i = 0; i < max_samples; i++) {
        int32_t v = acc[i];
        out[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
    free(acc);

    c->audio = out;
    m->audio_pcm = out;
    m->audio_frames = max_samples / (size_t)out_ch;
    m->audio_rate = out_rate;
    m->audio_channels = out_ch;
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
        free(c->audio);
        free(c);
    }
    m->impl = NULL;
}

int smk_movie_open(movie_t *m, const uint8_t *data, size_t size) {
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
