/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* fladump - headless verification harness for the flade decoders.
 * Decodes a movie up to frame N, writes that frame as a PPM, and prints a
 * per-step trace plus a decoded-sample summary. No SDL; links the core only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../src/fla.h"
#include "../src/acf.h"
#include "../src/hqr.h"
#include "../src/voc.h"
#include "../src/iso9660.h"

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n > 0 ? (size_t)n : 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    if (b)
        *size = (size_t)n;
    return b;
}

static uint32_t fnv1a(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* ACF dump. `target` is 0-indexed to match the Python reference decoder
 * (acf_decode.py): flade's cur_frame is 1-based, so frame N == cur_frame N+1. */
static int dump_acf(const uint8_t *mbuf, size_t msize, int target, const char *out) {
    acf_t a;
    if (acf_open(&a, mbuf, msize) != 0) {
        fprintf(stderr, "not an ACF\n");
        return 1;
    }
    printf("ACF %dx%d  frames=%d  fps=%.0f\n", a.width, a.height, a.num_frames, a.fps);
    int pal_max = 0;
    while (acf_step(&a)) {
        int idx = a.cur_frame - 1;
        for (int i = 0; i < 768; i++)
            if (a.palette[i] > pal_max)
                pal_max = a.palette[i];
        if (idx < 3 || idx == target)
            printf("  frame %4d: framehash=%08x\n", idx,
                   fnv1a(a.frame, (size_t)a.width * a.height));
        if (idx == target) {
            FILE *f = fopen(out, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", a.width, a.height);
                for (int i = 0; i < a.width * a.height; i++)
                    fwrite(&a.palette[a.frame[i] * 3], 1, 3, f); /* ACF palette is full 8-bit */
                fclose(f);
                printf("  wrote %s (frame %d)\n", out, target);
            }
        }
    }
    printf("summary: %d frames decoded, palette max channel=%d %s\n", a.cur_frame, pal_max,
           pal_max <= 63 ? "(6-bit! needs <<2)" : "(full 8-bit)");
    acf_close(&a);
    return 0;
}

int main(int argc, char **argv) {
    const char *cd = NULL, *movie = NULL, *out = "frame.ppm";
    int target = 60;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--cd") && i + 1 < argc)
            cd = argv[++i];
        else if (!strcmp(argv[i], "--frame") && i + 1 < argc)
            target = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out = argv[++i];
        else if (argv[i][0] != '-')
            movie = argv[i];
    }
    if (!movie) {
        fprintf(stderr, "usage: fladump [--cd LBA.DOT] <movie> [--frame N] [--out f.ppm]\n");
        return 2;
    }

    iso9660_t *iso = NULL;
    uint8_t *mbuf = NULL, *hqr = NULL;
    size_t msize = 0, hsize = 0;
    if (cd) {
        iso = iso_open(cd);
        if (!iso) {
            fprintf(stderr, "bad CD image\n");
            return 1;
        }
        char name[64], path[128];
        size_t k = 0;
        for (const char *p = movie; *p && *p != '.' && k < 63; p++)
            name[k++] = (char)toupper((unsigned char)*p);
        name[k] = 0;
        snprintf(path, sizeof(path), "LBA/FLA/%s.FLA", name);
        if (iso_read(iso, path, &mbuf, &msize) != 0) {
            fprintf(stderr, "movie %s not found\n", path);
            return 1;
        }
        iso_read(iso, "LBA/FLA/FLASAMP.HQR", &hqr, &hsize);
    } else {
        mbuf = read_file(movie, &msize);
    }
    if (!mbuf) {
        fprintf(stderr, "cannot read movie\n");
        return 1;
    }

    if (msize >= 8 && memcmp(mbuf, "FrameLen", 8) == 0) {
        int rc = dump_acf(mbuf, msize, target, out);
        free(mbuf);
        free(hqr);
        if (iso)
            iso_close(iso);
        return rc;
    }

    fla_t fla;
    if (fla_open(&fla, mbuf, msize) != 0) {
        fprintf(stderr, "not a FLA\n");
        return 1;
    }
    printf("FLA %ux%u  frames=%u  fps=%d\n", fla.width, fla.height, fla.num_frames,
           fla.speed + 1);

    int total_plays = 0, palette_loads = 0;
    int first_sample = -1;
    while (fla_step(&fla)) {
        if (fla.palette_dirty)
            palette_loads++;
        total_plays += fla.n_plays;
        if (first_sample < 0 && fla.n_plays > 0)
            first_sample = fla.plays[0].num;
        if ((int)fla.cur_frame <= 5 || fla.n_plays || (int)fla.cur_frame == target)
            printf("  frame %4u: ops palette=%d plays=%d stops=%d  framehash=%08x\n",
                   fla.cur_frame, fla.palette_dirty, fla.n_plays, fla.n_stops,
                   fnv1a(fla.frame, FLA_W * FLA_H));
        if ((int)fla.cur_frame == target) {
            FILE *f = fopen(out, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", FLA_W, FLA_H);
                for (int i = 0; i < FLA_W * FLA_H; i++) {
                    uint8_t idx = fla.frame[i];
                    uint8_t rgb[3];
                    for (int c = 0; c < 3; c++) {
                        uint8_t v = fla.palette[idx * 3 + c];
                        v |= v >> 6;
                        rgb[c] = v;
                    }
                    fwrite(rgb, 1, 3, f);
                }
                fclose(f);
                printf("  wrote %s (frame %d)\n", out, target);
            }
        }
    }
    printf("summary: %u frames decoded, %d palette loads, %d sample plays\n",
           fla.cur_frame, palette_loads, total_plays);

    if (hqr && first_sample >= 0) {
        uint8_t *raw = NULL;
        size_t rs = 0;
        if (hqr_entry(hqr, hsize, first_sample, &raw, &rs) == 0) {
            if (rs && raw[0] != 'C')
                raw[0] = 'C';
            voc_t v;
            if (voc_parse(raw, rs, &v) == 0) {
                printf("first sample #%d: VOC %zu frames @ %d Hz (%.2fs)\n",
                       first_sample, v.frames, v.rate, (double)v.frames / v.rate);
                voc_free(&v);
            } else {
                printf("first sample #%d: VOC parse failed (%zu raw bytes)\n",
                       first_sample, rs);
            }
            free(raw);
        }
    }

    free(mbuf);
    free(hqr);
    if (iso)
        iso_close(iso);
    return 0;
}
