/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors
 *
 * The key-frame and delta-frame decoders follow the GPLv2
 * TwinEngine implementation. */
#include "fla.h"
#include "util.h"

#include <string.h>

/* FLA frame opcodes, after the engine's "opcode - 1" normalisation. */
enum {
    OP_PALETTE = 0,
    OP_FADE = 1,
    OP_PLAY_SAMPLE = 2,
    OP_STOP_SAMPLE = 4,
    OP_DELTA_FRAME = 5,
    OP_KEY_FRAME = 7
};

/* Whole-image RLE. Per line: a run count, then runs that are either a literal
 * copy (negative tag) or a colour fill (positive tag). */
static void draw_key_frame(fla_t *f, const uint8_t *ptr, const uint8_t *end) {
    uint8_t *dst = f->frame;
    uint8_t *line = dst;
    uint8_t *cap = f->frame + FLA_W * FLA_H;
    int height = FLA_H;
    do {
        if (ptr >= end)
            break;
        int8_t runs = (int8_t)*ptr++;
        for (int a = 0; a < runs; a++) {
            if (ptr >= end)
                break;
            int8_t tag = (int8_t)*ptr++;
            if (tag < 0) {
                int count = -tag;
                for (int b = 0; b < count; b++)
                    if (dst < cap && ptr < end)
                        *dst++ = *ptr++;
            } else {
                if (ptr >= end)
                    break;
                uint8_t color = *ptr++;
                for (int b = 0; b < tag; b++)
                    if (dst < cap)
                        *dst++ = color;
            }
        }
        line += FLA_W;
        dst = line;
    } while (--height);
}

/* Delta patch over the previous picture. A leading skip selects the first line
 * and the number of lines touched; each line is skip/run encoded. */
static void draw_delta_frame(fla_t *f, const uint8_t *ptr, const uint8_t *end) {
    if (ptr + 4 > end)
        return;
    uint16_t skip = rd16(ptr);
    ptr += 2;
    int16_t height = (int16_t)rd16(ptr);
    ptr += 2;
    if (height <= 0)
        return;

    uint8_t *cap = f->frame + FLA_W * FLA_H;
    uint8_t *line = f->frame + (size_t)skip * FLA_W;
    if (line > cap)
        return;
    uint8_t *dst = line;

    do {
        if (ptr >= end)
            break;
        int8_t runs = (int8_t)*ptr++;
        for (int a = 0; a < runs; a++) {
            if (ptr >= end)
                break;
            dst += (uint8_t)*ptr++; /* horizontal skip */
            if (ptr >= end)
                break;
            int8_t tag = (int8_t)*ptr++;
            if (tag > 0) {
                for (int b = 0; b < tag; b++)
                    if (dst < cap && ptr < end)
                        *dst++ = *ptr++;
            } else {
                int count = -tag;
                if (ptr >= end)
                    break;
                uint8_t color = *ptr++;
                for (int b = 0; b < count; b++)
                    if (dst < cap)
                        *dst++ = color;
            }
        }
        line += FLA_W;
        dst = line;
    } while (--height);
}

int fla_open(fla_t *fla, const uint8_t *data, size_t size) {
    memset(fla, 0, sizeof(*fla));
    if (size < 20 || memcmp(data, "V1.3", 4) != 0)
        return -1;

    fla->data = data;
    fla->size = size;
    fla->num_frames = rd32(data + 6);
    fla->speed = data[10];
    fla->width = rd16(data + 12);
    fla->height = rd16(data + 14);

    uint16_t samples_in_fla = rd16(data + 16);
    /* header (20) + 2-byte pad + one (num,offset) cue pair per sample */
    fla->pos = 20 + (size_t)samples_in_fla * 4;
    if (fla->pos > size)
        return -1;
    return 0;
}

int fla_step(fla_t *fla) {
    if (fla->cur_frame >= fla->num_frames)
        return 0;
    if (fla->pos + 6 > fla->size)
        return 0;

    const uint8_t *d = fla->data;
    uint8_t num_ops = d[fla->pos];
    uint32_t block_size = rd32(d + fla->pos + 2);
    fla->pos += 6;

    const uint8_t *stream = d + fla->pos;
    if (fla->pos + block_size > fla->size)
        return 0;
    fla->pos += block_size;

    fla->n_plays = 0;
    fla->n_stops = 0;
    fla->palette_dirty = 0;
    fla->fade_out = 0;

    const uint8_t *send = stream + block_size;
    size_t p = 0;
    for (int a = 0; a < num_ops; a++) {
        if (p + 4 > block_size)
            break;
        uint8_t opcode = stream[p];
        uint16_t bs = rd16(stream + p + 2);
        p += 4;
        const uint8_t *pay = stream + p;
        const uint8_t *pay_end = (p + bs <= block_size) ? pay + bs : send;

        switch (opcode - 1) {
        case OP_PALETTE: {
            int num = rd16s(pay);
            int start = rd16s(pay + 2);
            if (start >= 0 && num > 0 && (start + num) * 3 <= 256 * 3)
                memcpy(fla->palette + start * 3, pay + 4, (size_t)num * 3);
            fla->palette_dirty = 1;
            break;
        }
        case OP_FADE:
            fla->fade_out = 1;
            break;
        case OP_PLAY_SAMPLE:
            if (fla->n_plays < FLA_MAX_EVENTS && bs >= 9) {
                fla_sample_play *s = &fla->plays[fla->n_plays++];
                s->num = rd16s(pay);
                s->freq = rd16s(pay + 2);
                s->repeat = rd16s(pay + 4);
                s->balance_l = pay[7];
                s->balance_r = pay[8];
            }
            break;
        case OP_STOP_SAMPLE:
            if (fla->n_stops < FLA_MAX_EVENTS && bs >= 2)
                fla->stops[fla->n_stops++] = rd16s(pay);
            break;
        case OP_DELTA_FRAME:
            draw_delta_frame(fla, pay, pay_end);
            break;
        case OP_KEY_FRAME:
            draw_key_frame(fla, pay, pay_end);
            break;
        default:
            break;
        }

        p += bs;
    }

    fla->cur_frame++;
    return 1;
}
