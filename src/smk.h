/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - Smacker (.smk) movie decoder (LBA2 cinematics), via libsmacker.
 * Video frames are 8-bit indexed with a full 8-bit palette; audio tracks
 * (0 = music, 1..3 = per-language voice) are decoded, mixed, and exposed as
 * one streaming track matched to the video duration. */
#ifndef FLADE_SMK_H
#define FLADE_SMK_H

#include <stddef.h>
#include <stdint.h>

#include "movie.h"

/* Wrap an in-memory Smacker file as a generic movie. The data buffer must
 * outlive the movie (libsmacker reads it on demand). Returns 0 on success. */
int smk_movie_open(movie_t *m, const uint8_t *data, size_t size);

/* Choose the default voice track (1..6); -1 = first present. Set before open. */
void smk_set_voice(int track);

/* Voice tracks available for live language switching (separate from the music,
 * which is movie.audio_pcm). Each pcm is interleaved s16 matched to the video,
 * same frames/rate/channels as the music. */
typedef struct {
    const int16_t *pcm[6];
    int track[6];      /* original smk track number (1..6) of each voice */
    int count;
    size_t frames;
    int rate, channels;
    int default_index; /* preferred voice index, or -1 */
} smk_voices;

/* Fill `v` if `m` is a Smacker movie with voice tracks. Returns the count. */
int smk_get_voices(movie_t *m, smk_voices *v);

#endif /* FLADE_SMK_H */
