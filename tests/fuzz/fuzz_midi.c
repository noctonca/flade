/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* Fuzz the XMI->SMF conversion (xmidi.c) and the MIDI loader (tml). The
 * synthesis (tsf) is not fuzzed - it needs a soundfont, not movie data. */
#include "xmidi.h"
#include "tml.h"
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint8_t *smf = NULL;
    uint32_t smf_size = convert_to_midi((uint8_t *)data, (uint32_t)size, &smf);
    if (smf && smf_size) {
        tml_message *m = tml_load_memory(smf, (int)smf_size);
        if (m) {
            int uc, up, tn;
            unsigned int tf, tl;
            tml_get_info(m, &uc, &up, &tn, &tf, &tl);
            tml_free(m);
        }
    }
    free(smf);
    return 0;
}
