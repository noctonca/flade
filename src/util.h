/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - little-endian read helpers */
#ifndef FLADE_UTIL_H
#define FLADE_UTIL_H

#include <stdint.h>

static inline uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline int16_t rd16s(const uint8_t *p) {
    return (int16_t)rd16(p);
}
static inline uint32_t rd24(const uint8_t *p) {
    return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
}
static inline uint32_t rd32(const uint8_t *p) {
    return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

#endif /* FLADE_UTIL_H */
