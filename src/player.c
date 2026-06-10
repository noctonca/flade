/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
#include "player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *pixels;          /* width*height indices */
    uint8_t palette[256 * 3]; /* display-ready 8-bit RGB */
    double duration;
} cframe;

struct player {
    movie_t *mv;
    int w, h;
    cframe *frames;
    int count, cap;
    int complete;
    size_t bytes, byte_cap;
};

player_t *player_open(movie_t *mv) {
    player_t *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->mv = mv;
    p->w = mv->width;
    p->h = mv->height;
    p->byte_cap = (size_t)1536 * 1024 * 1024; /* 1.5 GiB soft cap */
    return p;
}

/* Decode and cache one more frame. Returns 1 if a frame was added, 0 at end. */
static int decode_one(player_t *p) {
    if (p->complete)
        return 0;
    movie_frame fr;
    if (!p->mv->step(p->mv, &fr)) {
        p->complete = 1;
        return 0;
    }
    size_t px = (size_t)p->w * (size_t)p->h;
    if (p->bytes + px + sizeof(cframe) > p->byte_cap) {
        fprintf(stderr, "flade: frame-cache cap reached at %d frames; "
                        "seeking is limited beyond here\n",
                p->count);
        p->complete = 1;
        return 0;
    }
    if (p->count == p->cap) {
        int ncap = p->cap ? p->cap * 2 : 256;
        cframe *nf = realloc(p->frames, (size_t)ncap * sizeof(cframe));
        if (!nf) {
            p->complete = 1;
            return 0;
        }
        p->frames = nf;
        p->cap = ncap;
    }
    cframe *c = &p->frames[p->count];
    c->pixels = malloc(px ? px : 1);
    if (!c->pixels) {
        p->complete = 1;
        return 0;
    }
    memcpy(c->pixels, fr.pixels, px);
    memcpy(c->palette, fr.palette, sizeof(c->palette));
    c->duration = fr.duration;
    p->count++;
    p->bytes += px + sizeof(cframe);
    return 1;
}

int player_get(player_t *p, int index, movie_frame *out) {
    if (index < 0)
        return 0;
    while (index >= p->count && decode_one(p)) {
    }
    if (index >= p->count)
        return 0;
    cframe *c = &p->frames[index];
    out->pixels = c->pixels;
    out->palette = c->palette;
    out->duration = c->duration;
    return 1;
}

int player_count(const player_t *p) {
    return p->count;
}

int player_complete(const player_t *p) {
    return p->complete;
}

void player_close(player_t *p) {
    if (!p)
        return;
    for (int i = 0; i < p->count; i++)
        free(p->frames[i].pixels);
    free(p->frames);
    free(p);
}
