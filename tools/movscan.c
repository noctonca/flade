/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* movscan - headless decode harness for hardening/regression.
 *
 * Opens any movie (loose file, a file inside a CD image, or an entry of an
 * HQR), decodes every frame, checks audio extraction, and prints a stable
 * digest plus stats. Exits 0 on a clean decode, non-zero on any failure - so
 * it doubles as the corpus-sweep and golden-regression engine. No SDL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "movie.h"
#include "hqr.h"
#include "iso9660.h"

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *b = malloc((size_t)n ? (size_t)n : 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    if (b)
        *size = (size_t)n;
    return b;
}

static uint32_t fnv1a(uint32_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

int main(int argc, char **argv) {
    const char *path = NULL, *cd = NULL;
    int index = -1; /* >=0 => treat the loaded buffer as an HQR, play entry n */
    int quiet = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--cd") && i + 1 < argc)
            cd = argv[++i];
        else if (!strcmp(argv[i], "--index") && i + 1 < argc)
            index = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-q"))
            quiet = 1;
        else if (argv[i][0] != '-')
            path = argv[i];
    }
    if (!path) {
        fprintf(stderr, "usage: movscan [--cd img] <path> [--index n] [-q]\n");
        return 64;
    }

    /* ----- load the movie bytes ----- */
    uint8_t *buf = NULL;
    size_t size = 0;
    iso9660_t *iso = NULL;
    if (cd) {
        iso = iso_open(cd);
        if (!iso) {
            fprintf(stderr, "movscan: bad CD image '%s'\n", cd);
            return 2;
        }
        const char *p = path[0] == '/' ? path + 1 : path;
        if (iso_read(iso, p, &buf, &size) != 0) {
            fprintf(stderr, "movscan: '%s' not in image\n", path);
            iso_close(iso);
            return 2;
        }
    } else {
        buf = read_file(path, &size);
        if (!buf) {
            fprintf(stderr, "movscan: cannot read '%s'\n", path);
            return 2;
        }
    }
    if (index >= 0) { /* the buffer is an HQR; pull the requested entry */
        uint8_t *entry = NULL;
        size_t esize = 0;
        if (hqr_entry(buf, size, index, &entry, &esize) != 0) {
            fprintf(stderr, "movscan: HQR entry %d failed\n", index);
            free(buf);
            if (iso)
                iso_close(iso);
            return 2;
        }
        free(buf);
        buf = entry;
        size = esize;
    }

    /* ----- decode every frame ----- */
    movie_t mv;
    if (movie_open(&mv, buf, size, path) != 0) {
        fprintf(stderr, "movscan: %s: not a movie flade can open\n", path);
        free(buf);
        if (iso)
            iso_close(iso);
        return 3;
    }

    const char *kind = mv.kind == MOVIE_FLA   ? "FLA"
                       : mv.kind == MOVIE_ACF ? "ACF"
                       : mv.kind == MOVIE_SMK ? "SMK"
                                              : "?";
    uint32_t digest = 2166136261u;
    int nframes = 0;
    int rc = 0;
    movie_frame fr;
    while (mv.step(&mv, &fr)) {
        if (!fr.pixels || !fr.palette) {
            fprintf(stderr, "movscan: %s: null frame at %d\n", path, nframes);
            rc = 4;
            break;
        }
        digest = fnv1a(digest, fr.pixels, (size_t)mv.width * (size_t)mv.height);
        digest = fnv1a(digest, fr.palette, 256 * 3);
        nframes++;
        if (nframes > mv.num_frames + 8) { /* runaway guard */
            fprintf(stderr, "movscan: %s: runaway step past %d frames\n", path, mv.num_frames);
            rc = 5;
            break;
        }
    }

    /* a clean decode produces exactly the advertised frame count */
    if (rc == 0 && nframes != mv.num_frames) {
        fprintf(stderr, "movscan: %s: decoded %d frames, header says %d\n", path, nframes,
                mv.num_frames);
        rc = 6;
    }

    double adur = mv.audio_rate ? (double)mv.audio_frames / mv.audio_rate : 0.0;
    double vdur = mv.fps > 0 ? nframes / mv.fps : 0.0;
    if (!quiet)
        printf("%s %s %dx%d %d/%d fps=%.2f digest=%08x audio=%.2fs/%dch%s\n",
               rc == 0 ? "OK  " : "FAIL", kind, mv.width, mv.height, nframes, mv.num_frames, mv.fps,
               digest, adur, mv.audio_channels,
               mv.audio_pcm ? (adur > vdur - 0.6 && adur < vdur + 0.6 ? " sync" : " DRIFT") : "");

    mv.close(&mv);
    free(buf);
    if (iso)
        iso_close(iso);
    return rc;
}
