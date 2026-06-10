/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - movie format detection and dispatch. */
#include "movie.h"
#include "fla.h"

#include <string.h>

int movie_open(movie_t *m, const uint8_t *data, size_t size, const char *name) {
    (void)name;
    memset(m, 0, sizeof(*m));

    /* FLA: the header begins with the ASCII version tag "V1.3". */
    if (size >= 4 && memcmp(data, "V1.3", 4) == 0)
        return fla_movie_open(m, data, size);

    /* ACF (XCF) detection lands in the next commit. */
    return -1;
}
