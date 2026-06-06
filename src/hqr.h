/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - HQR ("High Quality Resource") archive reader.
 * Layout: a leading table of uint32 little-endian offsets (entry count =
 * first_offset/4 - 1), then per entry a header { uint32 decompSize,
 * uint32 compSize, uint16 type } followed by the (optionally LZSS) payload.
 * type 0 = stored; type N>0 = LZSS with min match length N+1. */
#ifndef FLADE_HQR_H
#define FLADE_HQR_H

#include <stddef.h>
#include <stdint.h>

/* Number of entries in the archive, or -1 if the buffer is not a valid HQR. */
int hqr_count(const uint8_t *hqr, size_t hqrsize);

/* Decode entry `index` into a freshly malloc'd buffer (caller frees *out).
 * Returns 0 on success. */
int hqr_entry(const uint8_t *hqr, size_t hqrsize, int index, uint8_t **out, size_t *outsize);

/* LBA HQR LZSS expander (a faithful port of the engine's ExpandLZ).
 * min_match is the HQR type + 1. */
void expand_lz(uint8_t *dst, const uint8_t *src, uint32_t decomp_size, uint32_t min_match);

#endif /* FLADE_HQR_H */
