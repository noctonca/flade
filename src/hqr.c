/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors
 *
 * expand_lz() is ported from the GPLv2 LBA engine source
 * (ExpandLZ, LBALab/lba2-classic-community). */
#include "hqr.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

/* Faithful port of the engine's ExpandLZ. Back-references encode a 12-bit
 * distance and a 4-bit length; the literal/match selector is a bit in a flag
 * byte emitted every 8 tokens. min_match is the HQR type + 1. */
void expand_lz(uint8_t *dst, const uint8_t *src, uint32_t src_size, uint32_t decomp_size,
               uint32_t min_match) {
    uint32_t si = 0, di = 0;
    while (di < decomp_size) {
        if (si >= src_size) /* ran out of compressed input */
            return;
        uint8_t flag = src[si++];
        for (int i = 0; i < 8; i++) {
            if (di >= decomp_size)
                break;
            if (flag & 0x1) {
                if (si >= src_size)
                    return;
                dst[di++] = src[si++];
            } else {
                if (si + 1 >= src_size)
                    return;
                uint32_t len = (src[si] & 0x0f) + min_match;
                uint32_t off = ((uint32_t)src[si + 1] << 4) | (src[si] >> 4);
                si += 2;
                if (off + 1 > di) /* back-reference before the start: malformed */
                    return;
                for (uint32_t j = 0; j < len; j++) {
                    dst[di] = dst[di - off - 1];
                    di++;
                    if (di >= decomp_size)
                        break;
                }
            }
            flag >>= 1;
        }
    }
}

int hqr_count(const uint8_t *hqr, size_t hqrsize) {
    if (hqrsize < 4)
        return -1;
    uint32_t first = rd32(hqr);
    if (first < 4 || first > hqrsize)
        return -1;
    return (int)(first / 4) - 1;
}

int hqr_entry(const uint8_t *hqr, size_t hqrsize, int index, uint8_t **out, size_t *outsize) {
    int n = hqr_count(hqr, hqrsize);
    if (n < 0 || index < 0 || index >= n)
        return -1;

    uint32_t off = rd32(hqr + (size_t)index * 4);
    if ((size_t)off + 10 > hqrsize)
        return -1;

    uint32_t dsize = rd32(hqr + off);
    uint16_t type = rd16(hqr + off + 8);
    const uint8_t *data = hqr + off + 10;
    size_t avail = hqrsize - ((size_t)off + 10); /* stored/compressed bytes present */

    /* reject an absurd decompressed size before allocating (forged-header DoS).
     * Real entries can be large - LBA2's intro Smacker is ~76 MB - so the cap is
     * generous; it only rules out the multi-GB allocations a forged size drives. */
    if (dsize > 256u * 1024 * 1024)
        return -1;

    uint8_t *buf = malloc(dsize ? dsize : 1);
    if (!buf)
        return -1;

    if (type == 0) {
        if (dsize > avail) { /* stored data must actually fit in the archive */
            free(buf);
            return -1;
        }
        memcpy(buf, data, dsize);
    } else {
        expand_lz(buf, data, (uint32_t)avail, dsize, (uint32_t)type + 1);
    }

    *out = buf;
    *outsize = dsize;
    return 0;
}
