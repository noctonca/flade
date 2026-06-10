/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - FLA movie decoder (Adeline's "V1.3" full-motion video format).
 *
 * A FLA is a header, a sample-cue table, then a stream of frames. Each frame
 * is a list of typed opcode blocks that load palette, trigger samples, or
 * paint the 320x200 8-bit indexed picture as either a key frame (whole-image
 * RLE) or a delta frame (skip/run patch over the previous picture).
 *
 * The decoder is platform-free: fla_step() advances one frame, updating the
 * persistent indexed picture and palette and reporting any sample cues. The
 * caller turns `frame` + `palette` into pixels and drives audio. */
#ifndef FLADE_FLA_H
#define FLADE_FLA_H

#include <stddef.h>
#include <stdint.h>

#include "movie.h"

#define FLA_W 320
#define FLA_H 200
#define FLA_MAX_EVENTS 16

typedef struct {
    int num;        /* sample index into FLASAMP.HQR */
    int freq;       /* requested frequency (advisory; VOC carries its own) */
    int repeat;     /* loop count */
    int balance_l;  /* 0..63 left balance (0 => unset) */
    int balance_r;  /* 0..63 right balance */
} fla_sample_play;

typedef struct {
    const uint8_t *data;   /* borrowed; must outlive the player */
    size_t size, pos;

    uint16_t width, height;
    uint32_t num_frames;
    uint8_t speed;         /* frame period is 1000/(speed+1) ms */
    uint32_t cur_frame;

    uint8_t frame[FLA_W * FLA_H]; /* persistent indexed picture */
    uint8_t palette[256 * 3];     /* persistent palette, 8-bit channels */

    /* per-frame outputs, refreshed by each fla_step() */
    int palette_dirty;
    int fade_out;
    int midi_play; /* FLA_INFO Info==1: start the cutscene MIDI (XMI track 26) */
    int midi_fade; /* FLA_INFO Info==4: fade the cutscene MIDI out */
    fla_sample_play plays[FLA_MAX_EVENTS];
    int n_plays;
    int stops[FLA_MAX_EVENTS];
    int n_stops;
} fla_t;

/* Initialise from an in-memory FLA file. Returns 0 on success. */
int fla_open(fla_t *fla, const uint8_t *data, size_t size);

/* Decode the next frame. Returns 1 if a frame was produced, 0 at end. */
int fla_step(fla_t *fla);

/* Wrap an in-memory FLA as a generic movie (see movie.h). The adapter owns the
 * 6-bit->8-bit palette expansion and the scene-fade state machine, so the
 * frame it hands out is display-ready. Returns 0 on success. */
int fla_movie_open(movie_t *m, const uint8_t *data, size_t size);

/* Transitional: the underlying FLA decoder behind a movie, so the cue-based
 * sound-effect path in main can read this frame's sample triggers. Returns
 * NULL if m is not an FLA. Removed once audio is unified across formats. */
fla_t *fla_from_movie(movie_t *m);

#endif /* FLADE_FLA_H */
