/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - generic Adeline movie decoder interface.
 *
 * A movie is a forward-only video codec: movie_open() detects the format and
 * sets up a decoder, step() produces the next frame (8-bit indexed pixels plus
 * a display-ready palette), close() tears it down. Random access (rewind,
 * scrub) is a caching layer above this seam; per-frame audio is driven by the
 * decoder and added later. FLA and ACF (and later SMK) implement this. */
#ifndef FLADE_MOVIE_H
#define FLADE_MOVIE_H

#include <stddef.h>
#include <stdint.h>

typedef enum { MOVIE_UNKNOWN = 0, MOVIE_FLA, MOVIE_ACF } movie_kind;

typedef struct movie movie_t;

/* One decoded frame. Buffers are owned by the decoder and valid until the
 * next step() call. */
typedef struct {
    const uint8_t *pixels;  /* width*height, 8-bit palette indices */
    const uint8_t *palette; /* 256*3, display-ready full 8-bit RGB */
    double duration;        /* seconds this frame should be shown */
} movie_frame;

struct movie {
    movie_kind kind;
    int width, height, num_frames;
    double fps;
    int (*step)(movie_t *, movie_frame *); /* 1 = frame produced, 0 = end */
    void (*close)(movie_t *);
    void *impl;

    /* Streaming audio track, whole-movie, matched in duration to the video
     * (ACF). NULL for cue-based formats (FLA). Interleaved signed 16-bit. */
    const int16_t *audio_pcm;
    size_t audio_frames; /* per-channel sample count */
    int audio_rate;
    int audio_channels;
};

/* Detect the format of an in-memory movie (by content; `name` is a hint) and
 * open the matching decoder. The data buffer must outlive the movie.
 * Returns 0 on success, -1 if the format is unknown. */
int movie_open(movie_t *m, const uint8_t *data, size_t size, const char *name);

#endif /* FLADE_MOVIE_H */
