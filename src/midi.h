/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - MIDI rendering for FLA cutscene music (the "flute", XMI track 26 of
 * MIDI_MI.HQR). Loads a General MIDI soundfont once, then renders an XMI song
 * to a PCM buffer via TinySoundFont. A .sf2 is required - the HQR holds only
 * note data, not instruments. */
#ifndef FLADE_MIDI_H
#define FLADE_MIDI_H

#include <stddef.h>
#include <stdint.h>

/* Load a soundfont. If sf2_path is NULL, search common system locations.
 * Returns 0 if a font is loaded. */
int midi_init(const char *sf2_path);
int midi_available(void);
void midi_shutdown(void);

/* Render an XMI song (one HQR entry) to interleaved stereo s16 at `rate`.
 * Caller frees *pcm. Returns 0 on success. */
int midi_render_xmi(const uint8_t *xmi, size_t xmi_size, int rate,
                    int16_t **pcm, size_t *frames);

#endif /* FLADE_MIDI_H */
