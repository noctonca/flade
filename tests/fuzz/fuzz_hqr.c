/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* Fuzz the HQR archive reader + the LZSS/LZMIT expander (expand_lz). */
#include "hqr.h"
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int n = hqr_count(data, size);
    if (n < 0)
        return 0;
    if (n > 4096)
        n = 4096; /* don't iterate a forged huge count */
    for (int i = 0; i < n; i++) {
        uint8_t *out = NULL;
        size_t outsize = 0;
        if (hqr_entry(data, size, i, &out, &outsize) == 0)
            free(out);
    }
    return 0;
}
