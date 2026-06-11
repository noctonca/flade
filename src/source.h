/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - movie sources: finding, listing, resolving and extracting movies
 * from CD images and HQR archives. SDL-free; depends only on the container
 * readers (iso9660, hqr). */
#ifndef FLADE_SOURCE_H
#define FLADE_SOURCE_H

#include <stddef.h>
#include <stdint.h>

#include "iso9660.h"

/* Read a whole file into a malloc'd buffer (caller frees); NULL on failure. */
uint8_t *read_file(const char *path, size_t *size);

/* Print the movies in an image: loose .fla/.acf plus the VIDEO.HQR cinematics
 * (named from the RESS.HQR catalogue). `label` is shown in the heading. */
void source_list_movies(iso9660_t *iso, const char *label);

/* A playable movie in a list: a display name plus how to play it. For an image
 * movie, `arg` is a name / in-image path (and index is -1). For an entry of a
 * loose movie-HQR, `index` is the entry number (and arg is unused). */
typedef struct {
    char name[64];
    char arg[260];
    int index;
} source_item;

/* Fill `items` (up to `cap`) with the image's movies - loose .fla/.acf and the
 * VIDEO.HQR cinematics. Returns the count. */
int source_movies(iso9660_t *iso, source_item *items, int cap);

/* If `path` is a loose movie-HQR (e.g. LBA2's VIDEO.HQR), fill `items` with its
 * entries by index - named from a sibling RESS.HQR catalogue when present, else
 * numbered. Returns the count, or 0 if `path` is not a movie-HQR. */
int source_hqr_movies(const char *path, source_item *items, int cap);

/* Resolve a bare movie name within an image to an in-image path (sans leading
 * '/'). On success returns 0 and fills `inpath`; if the name is an LBA2
 * cinematic it also sets `*video_index` to its VIDEO.HQR entry. Returns -1 when
 * nothing matches. */
int source_resolve(iso9660_t *iso, const char *name, char *inpath, size_t cap, int *video_index);

/* Unpack a CD image / HQR archive (or, with cd_path, a path inside an image)
 * into outdir, smart-named by content magic + the RESS catalogue. Returns the
 * process exit code (0 on success). */
int run_extract(const char *src, const char *cd_path, const char *outdir);

#endif /* FLADE_SOURCE_H */
