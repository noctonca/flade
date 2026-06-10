/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - ACF/XCF movie decoder (Adeline's Time Commando cinematic codec).
 *
 * An ACF is a flat list of chunks (8-byte tag + u32 size + payload): FrameLen,
 * Format, Palette, KeyFrame / DltFrame (the tile-coded video), plus Sound* and
 * Camera/Recouvre chunks we don't need for playback yet. Each frame is a grid
 * of 8x8 tiles, one 6-bit opcode per tile selecting one of 64 decode routines,
 * double-buffered against the previous frame. */
#ifndef FLADE_ACF_H
#define FLADE_ACF_H

#include <stddef.h>
#include <stdint.h>

#include "movie.h"

typedef struct {
    int width, height; /* frame dimensions from the Format chunk */
    int num_frames;    /* KeyFrame + DltFrame count */
    double fps;
    int cur_frame;

    const uint8_t *frame;     /* current decoded frame, width*height indices */
    uint8_t palette[256 * 3]; /* current palette, full 8-bit RGB */

    /* internal: chunk walk over the borrowed file buffer */
    const uint8_t *data;
    size_t size, pos;
} acf_t;

/* Initialise from an in-memory ACF file (must outlive the decoder). 0 on ok. */
int acf_open(acf_t *a, const uint8_t *data, size_t size);

/* Decode the next frame. Returns 1 if a frame was produced, 0 at end. */
int acf_step(acf_t *a);

/* Release the decode buffers. */
void acf_close(acf_t *a);

/* Wrap an in-memory ACF as a generic movie (see movie.h). 0 on success. */
int acf_movie_open(movie_t *m, const uint8_t *data, size_t size);

#endif /* FLADE_ACF_H */
