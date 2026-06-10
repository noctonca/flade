/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* Fuzz the Creative Voice File (.VOC) parser. */
#include "voc.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    voc_t v;
    if (voc_parse(data, size, &v) == 0)
        voc_free(&v);
    return 0;
}
