#ifndef XMIDI_H
#define XMIDI_H

#include <stdint.h>

/*
 * Convert XMIDI data (Miles Sound System IFF FORM XMID container) to
 * standard MIDI (SMF format 0).
 *
 * Adapted from TwinEngine (twin-e/src/utils/xmidi.c), which was itself
 * adapted from ScummVM / Exult.
 *
 * Each HQR entry contains one XMIDI song; this converts track index 0.
 *
 * Returns the size of the allocated SMF buffer stored in *dest, or 0 on
 * error. Caller must free(*dest).
 */
uint32_t convert_to_midi(uint8_t *data, uint32_t size, uint8_t **dest);

#endif /* XMIDI_H */
