/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - Creative Voice File (.VOC) decoder.
 * FLA samples are stored as 8-bit unsigned mono VOCs (block type 1, codec 0).
 * We decode every sound block into one contiguous signed-16-bit mono buffer. */
#ifndef FLADE_VOC_H
#define FLADE_VOC_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t *pcm;    /* mono, signed 16-bit, malloc'd (caller frees) */
    size_t frames;   /* number of samples */
    int rate;        /* sample rate in Hz */
} voc_t;

/* Parse a VOC image into `out`. Returns 0 on success. */
int voc_parse(const uint8_t *data, size_t size, voc_t *out);
void voc_free(voc_t *v);

#endif /* FLADE_VOC_H */
