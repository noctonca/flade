/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
#include "voc.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

/* Grow a signed-16 buffer and append 8-bit unsigned PCM, centring to signed. */
static int append_u8(int16_t **buf, size_t *cap, size_t *len, const uint8_t *pcm, size_t n) {
    if (*len + n > *cap) {
        size_t nc = *cap ? *cap * 2 : 4096;
        while (nc < *len + n)
            nc *= 2;
        int16_t *nb = realloc(*buf, nc * sizeof(int16_t));
        if (!nb)
            return -1;
        *buf = nb;
        *cap = nc;
    }
    for (size_t i = 0; i < n; i++)
        (*buf)[*len + i] = (int16_t)(((int)pcm[i] - 128) << 8);
    *len += n;
    return 0;
}

int voc_parse(const uint8_t *data, size_t size, voc_t *out) {
    memset(out, 0, sizeof(*out));
    if (size < 26 || memcmp(data, "Creative Voice File", 19) != 0)
        return -1;

    size_t pos = rd16(data + 20); /* header size -> start of first block */
    int rate = 11025;
    int16_t *pcm = NULL;
    size_t cap = 0, len = 0;

    while (pos + 4 <= size) {
        uint8_t bt = data[pos];
        if (bt == 0) /* terminator */
            break;
        uint32_t bsize = rd24(data + pos + 1);
        size_t bdata = pos + 4;
        if (bdata + bsize > size)
            break;

        if (bt == 1 && bsize >= 2) {
            /* Sound data: [freq divisor][codec][8-bit unsigned PCM]. */
            uint8_t fdiv = data[bdata];
            int r = 1000000 / (256 - fdiv);
            if (len == 0)
                rate = r;
            if (append_u8(&pcm, &cap, &len, data + bdata + 2, bsize - 2) != 0) {
                free(pcm);
                return -1;
            }
        } else if (bt == 9 && bsize >= 12) {
            /* New-style sound data: [rate u32][bits][channels][...]. */
            uint32_t r = rd32(data + bdata);
            uint8_t bits = data[bdata + 4];
            if (len == 0 && r)
                rate = (int)r;
            if (bits == 8 &&
                append_u8(&pcm, &cap, &len, data + bdata + 12, bsize - 12) != 0) {
                free(pcm);
                return -1;
            }
        }
        /* Other block types (silence, repeat markers, text) are ignored. */

        pos = bdata + bsize;
    }

    if (!pcm)
        return -1;
    out->pcm = pcm;
    out->frames = len;
    out->rate = rate > 0 ? rate : 11025;
    return 0;
}

void voc_free(voc_t *v) {
    if (v && v->pcm) {
        free(v->pcm);
        v->pcm = NULL;
    }
}
