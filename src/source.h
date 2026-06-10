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
