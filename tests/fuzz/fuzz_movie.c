/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* Fuzz the whole movie front: format detection + the FLA/ACF/SMK decoders,
 * including each one's audio extraction. */
#include "movie.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    movie_t mv;
    if (movie_open(&mv, data, size, "fuzz") != 0)
        return 0;
    movie_frame fr;
    long cap = (long)mv.num_frames + 8;
    if (cap < 0 || cap > 20000)
        cap = 20000; /* bound a forged frame count */
    for (long n = 0; n < cap && mv.step(&mv, &fr); n++)
        (void)fr;
    mv.close(&mv);
    return 0;
}
