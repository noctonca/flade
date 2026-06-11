/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors
 *
 * XMI -> SMF conversion is xmidi.c (GPLv2, from TwinEngine/ScummVM/Exult);
 * MIDI parsing is tml.h (zlib) and synthesis is tsf.h (MIT), both by Bernhard
 * Schelling. The render loop here mirrors ../lba-midi-play. */
#include "midi.h"
#include "xmidi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"
#define TML_IMPLEMENTATION
#include "tml.h"

#include "flute_sf2.h" /* the bundled cutscene-flute soundfont */

static tsf *g_sf;

/* Distro/macOS "default GM" locations, tried when no explicit font is given. */
static const char *DEFAULT_SF2[] = {
    "/Library/Audio/Sounds/Banks/FluidR3_GM_GS.sf2",
    "/usr/share/sounds/sf2/default-GM.sf2",
    "/usr/share/sounds/sf2/FluidR3_GM.sf2",
    "/usr/share/sounds/sf2/TimGM6mb.sf2",
    "/usr/share/soundfonts/default.sf2",
    "/usr/share/soundfonts/FluidR3_GM.sf2",
    NULL,
};

int midi_init(const char *sf2_path) {
    if (g_sf)
        return 0;
    if (sf2_path) {
        g_sf = tsf_load_filename(sf2_path);
        if (!g_sf)
            fprintf(stderr, "flade: couldn't load soundfont '%s'; using the bundled flute\n",
                    sf2_path);
    } else {
        /* Prefer a real system GM font if one is installed (richer). */
        for (int i = 0; DEFAULT_SF2[i] && !g_sf; i++) {
            FILE *f = fopen(DEFAULT_SF2[i], "rb");
            if (f) {
                fclose(f);
                g_sf = tsf_load_filename(DEFAULT_SF2[i]);
            }
        }
    }
    /* Always fall back to the bundled flute so the cutscene never goes silent
     * for want of a soundfont. */
    if (!g_sf)
        g_sf = tsf_load_memory(flute_sf2, (int)flute_sf2_len);
    return g_sf ? 0 : -1;
}

int midi_available(void) {
    return g_sf != NULL;
}

void midi_shutdown(void) {
    if (g_sf) {
        tsf_close(g_sf);
        g_sf = NULL;
    }
}

static void dispatch(tml_message *m) {
    switch (m->type) {
    case TML_PROGRAM_CHANGE:
        tsf_channel_set_presetnumber(g_sf, m->channel, m->program, (m->channel == 9));
        break;
    case TML_NOTE_ON:
        if (m->velocity > 0)
            tsf_channel_note_on(g_sf, m->channel, m->key, m->velocity / 127.0f);
        else
            tsf_channel_note_off(g_sf, m->channel, m->key);
        break;
    case TML_NOTE_OFF:
        tsf_channel_note_off(g_sf, m->channel, m->key);
        break;
    case TML_PITCH_BEND:
        tsf_channel_set_pitchwheel(g_sf, m->channel, m->pitch_bend);
        break;
    case TML_CONTROL_CHANGE:
        tsf_channel_midi_control(g_sf, m->channel, m->control, m->control_value);
        break;
    default:
        break;
    }
}

int midi_render_xmi(const uint8_t *xmi, size_t xmi_size, int rate, int16_t **pcm,
                    size_t *frames_out) {
    if (!g_sf || !xmi || xmi_size == 0)
        return -1;

    uint8_t *smf = NULL;
    uint32_t smf_size = convert_to_midi((uint8_t *)xmi, (uint32_t)xmi_size, &smf);
    if (!smf || smf_size == 0)
        return -1;
    tml_message *head = tml_load_memory(smf, (int)smf_size);
    free(smf);
    if (!head)
        return -1;

    int used_ch, used_pr, total_notes;
    unsigned int time_first, time_len;
    tml_get_info(head, &used_ch, &used_pr, &total_notes, &time_first, &time_len);
    double total_ms = (double)time_len + 2000.0; /* leave a release tail */
    size_t total = (size_t)(total_ms / 1000.0 * rate);
    if (total == 0) {
        tml_free(head);
        return -1;
    }

    tsf_reset(g_sf);
    tsf_set_output(g_sf, TSF_STEREO_INTERLEAVED, rate, 0.0f);
    tsf_channel_set_presetnumber(g_sf, 9, 0, 1); /* channel 10 = drums */

    float *fbuf = malloc(total * 2 * sizeof(float));
    if (!fbuf) {
        tml_free(head);
        return -1;
    }

    /* Offline render: between events, render a block; when an event is due,
     * dispatch it. Mirrors the real-time callback in lba-midi-play. */
    tml_message *m = head;
    double time_ms = 0.0;
    size_t pos = 0;
    while (pos < total) {
        int block = (int)(total - pos);
        if (m) {
            double samps = (m->time - time_ms) * rate / 1000.0;
            if (samps <= 0.0) {
                block = 0;
            } else {
                int s = (int)samps;
                if (s == 0)
                    s = 1;
                if (s < block)
                    block = s;
            }
        }
        if (block > 0) {
            tsf_render_float(g_sf, fbuf + pos * 2, block, 0);
            time_ms += (double)block * 1000.0 / rate;
            pos += block;
        }
        while (m && time_ms >= m->time) {
            dispatch(m);
            m = m->next;
        }
        if (!m && block == 0) { /* no events left: render the remaining tail */
            int rest = (int)(total - pos);
            if (rest > 0) {
                tsf_render_float(g_sf, fbuf + pos * 2, rest, 0);
                pos += (size_t)rest;
            }
        }
    }
    tml_free(head);

    int16_t *out = malloc(total * 2 * sizeof(int16_t));
    if (!out) {
        free(fbuf);
        return -1;
    }
    for (size_t i = 0; i < total * 2; i++) {
        float s = tanhf(fbuf[i]); /* soft limiter, matches lba-midi-play */
        int v = (int)(s * 32767.0f);
        out[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
    free(fbuf);
    *pcm = out;
    *frames_out = total;
    return 0;
}
