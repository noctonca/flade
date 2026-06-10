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

/* Choose which voice track (1..6) to mix with the music; -1 = first present.
 * Set before opening the movie. */
void smk_set_voice(int track);

#endif /* FLADE_SMK_H */
