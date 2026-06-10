/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - playback layer: a lazy decoded-frame cache over a movie.
 *
 * The decoders are forward-only; this layer keeps every frame it decodes so
 * the player above can jump anywhere - rewind, scrub, frame-step - by index.
 * Adeline movie frames are small (<=320x240 indexed), so caching the whole
 * movie is cheap; a byte cap guards against a pathologically long one. */
#ifndef FLADE_PLAYER_H
#define FLADE_PLAYER_H

#include "movie.h"

typedef struct player player_t;

/* Wrap a movie. The player borrows `mv` (the caller still closes it). */
player_t *player_open(movie_t *mv);
void player_close(player_t *p);

/* Fetch frame `index`, decoding forward and caching as needed. Fills `out`
 * (buffers owned by the player, valid until player_close). Returns 1 on
 * success, 0 if the index is past the end of the movie. */
int player_get(player_t *p, int index, movie_frame *out);

int player_count(const player_t *p);    /* frames decoded/cached so far */
int player_complete(const player_t *p); /* whole movie cached (or cap hit) */

#endif /* FLADE_PLAYER_H */
