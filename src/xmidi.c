/*
 * XMIDI to SMF converter.
 * Adapted from TwinEngine: twin-e/src/utils/xmidi.c
 * Copyright (C) 2002 The TwinEngine team
 * Copyright (C) 2013 The ScummVM/ExultEngine team
 * Licensed under GPL v2.
 *
 * Converts an XMIDI IFF FORM XMID container (used by Miles Sound System)
 * into standard MIDI (SMF format 0, 60 PPQN, tempo 500 000 µs/beat).
 */

#include "xmidi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAYSIZE(x) ((int)(sizeof(x) / sizeof(x[0])))

/* -------------------------------------------------------------------------
 * Data structures
 * ---------------------------------------------------------------------- */

struct EventInfo {
    uint8_t *start;
    uint32_t delta;
    uint8_t  event;
    union {
        struct { uint8_t param1; uint8_t param2; } basic;
        struct { uint8_t type;   uint8_t *data;  } ext;
    };
    uint32_t length;
};

struct XMIDI_info {
    uint8_t  num_tracks;
    uint8_t *tracks[120];
};

struct CachedEvent {
    struct EventInfo  *eventInfo;
    uint32_t           time;
    struct CachedEvent *next;
};

static struct CachedEvent *cached_events = NULL;

/* -------------------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------------- */

static uint16_t read2low(uint8_t **data);
static uint32_t read4high(uint8_t **data);
static void     write4high(uint8_t **data, uint32_t val);
static void     write2high(uint8_t **data, uint16_t val);
static uint32_t readVLQ2(uint8_t **data);
static uint32_t readVLQ(uint8_t **data);
static int32_t  putVLQ(uint8_t *dest, uint32_t value);

static struct EventInfo *pop_cached_event(uint32_t current_time, uint32_t delta);
static void save_event(struct EventInfo *info, uint32_t current_time);

static int32_t read_event_info(uint8_t *data, struct EventInfo *info, uint32_t current_time);
static int32_t put_event(uint8_t *dest, struct EventInfo *info);
static int32_t convert_to_mtrk(uint8_t *data, uint32_t size, uint8_t *dest);
static int32_t read_XMIDI_header(uint8_t *data, uint32_t size, struct XMIDI_info *info);

/* -------------------------------------------------------------------------
 * Byte-order helpers (adapted from ScummVM / Exult)
 * ---------------------------------------------------------------------- */

static uint16_t read2low(uint8_t **data)
{
    uint8_t *d = *data;
    uint16_t v = (uint16_t)((d[1] << 8) | d[0]);
    *data = d + 2;
    return v;
}

static uint32_t read4high(uint8_t **data)
{
    uint8_t *d = *data;
    uint32_t v = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
                 ((uint32_t)d[2] <<  8) |  (uint32_t)d[3];
    *data = d + 4;
    return v;
}

static void write4high(uint8_t **data, uint32_t val)
{
    uint8_t *d = *data;
    *d++ = (val >> 24) & 0xff;
    *d++ = (val >> 16) & 0xff;
    *d++ = (val >>  8) & 0xff;
    *d++ =  val        & 0xff;
    *data = d;
}

static void write2high(uint8_t **data, uint16_t val)
{
    uint8_t *d = *data;
    *d++ = (val >> 8) & 0xff;
    *d++ =  val       & 0xff;
    *data = d;
}

/* XMIDI variable-length quantity (intervals are plain sums, not 7-bit encoded) */
static uint32_t readVLQ2(uint8_t **data)
{
    uint8_t *pos = *data;
    uint32_t value = 0;
    while (!(pos[0] & 0x80))
        value += *pos++;
    *data = pos;
    return value;
}

/* Standard SMF variable-length quantity */
static uint32_t readVLQ(uint8_t **data)
{
    uint8_t *d = *data;
    uint32_t value = 0;
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t b = *d++;
        value = (value << 7) | (b & 0x7f);
        if (!(b & 0x80)) break;
    }
    *data = d;
    return value;
}

/* Write SMF VLQ; returns byte count written (or needed if dest==NULL) */
static int32_t putVLQ(uint8_t *dest, uint32_t value)
{
    int32_t buf, j, i = 1;
    buf = (int32_t)(value & 0x7f);
    while (value >>= 7) {
        buf <<= 8;
        buf |= (int32_t)((value & 0x7f) | 0x80);
        i++;
    }
    if (!dest) return i;
    for (j = 0; j < i; j++) {
        *dest++ = (uint8_t)(buf & 0xff);
        buf >>= 8;
    }
    return i;
}

/* -------------------------------------------------------------------------
 * Note-off cache (deferred note-off injection)
 * ---------------------------------------------------------------------- */

static void save_event(struct EventInfo *info, uint32_t current_time)
{
    uint32_t target = current_time + info->length;
    struct CachedEvent *prev, *next, *temp;

    temp = (struct CachedEvent *)malloc(sizeof(struct CachedEvent));
    temp->eventInfo = info;
    temp->time = target;
    temp->next = NULL;

    if (!cached_events) {
        cached_events = temp;
        return;
    }

    prev = NULL;
    next = cached_events;
    while (next && next->time < target) {
        prev = next;
        next = next->next;
    }
    if (!next) {
        prev->next = temp;
    } else if (prev) {
        temp->next = prev->next;
        prev->next = temp;
    } else {
        temp->next = cached_events;
        cached_events = temp;
    }
}

static struct EventInfo *pop_cached_event(uint32_t current_time, uint32_t delta)
{
    struct EventInfo *info = NULL;
    struct CachedEvent *old;

    if (cached_events && cached_events->time < current_time + delta) {
        info = cached_events->eventInfo;
        info->delta = cached_events->time - current_time;
        old = cached_events;
        cached_events = cached_events->next;
        free(old);
    }
    return info;
}

/* -------------------------------------------------------------------------
 * Event parsing / writing (adapted from ScummVM / Exult)
 * ---------------------------------------------------------------------- */

static int32_t read_event_info(uint8_t *data, struct EventInfo *info, uint32_t current_time)
{
    struct EventInfo *inj;

    info->start = data;
    info->delta = readVLQ2(&data);
    info->event = *data++;
    current_time += info->delta;

    switch (info->event >> 4) {
    case 0x9: /* Note On */
        info->basic.param1 = *data++;
        info->basic.param2 = *data++;
        info->length = readVLQ(&data);
        if (info->basic.param2 == 0) {
            /* velocity 0 → treat as note-off */
            info->event = (info->event & 0x0f) | 0x80;
            info->length = 0;
        } else {
            /* Schedule a note-off for later */
            inj = (struct EventInfo *)malloc(sizeof(struct EventInfo));
            inj->event          = 0x80 | (info->event & 0x0f);
            inj->basic.param1   = info->basic.param1;
            inj->basic.param2   = info->basic.param2;
            inj->length         = info->length;
            save_event(inj, current_time);
        }
        break;

    case 0xc:
    case 0xd:
        info->basic.param1 = *data++;
        info->basic.param2 = 0;
        break;

    case 0x8:
    case 0xa:
    case 0xe:
        info->basic.param1 = *data++;
        info->basic.param2 = *data++;
        break;

    case 0xb:
        info->basic.param1 = *data++;
        info->basic.param2 = *data++;
        /* XMIDI controller events 0x6e-0x78 are Miles-specific; pass through */
        break;

    case 0xf:
        switch (info->event & 0x0f) {
        case 0x2:
            info->basic.param1 = *data++;
            info->basic.param2 = *data++;
            break;
        case 0x3:
            info->basic.param1 = *data++;
            info->basic.param2 = 0;
            break;
        case 0x6: case 0x8: case 0xa:
        case 0xb: case 0xc: case 0xe:
            info->basic.param1 = info->basic.param2 = 0;
            break;
        case 0x0: /* SysEx */
            info->length   = readVLQ(&data);
            info->ext.data = data;
            data += info->length;
            break;
        case 0xf: /* META */
            info->ext.type = *data++;
            info->length   = readVLQ(&data);
            info->ext.data = data;
            data += info->length;
            if (info->ext.type == 0x51 && info->length == 3) {
                /* Force tempo to 500 000 µs/beat (120 BPM) */
                info->ext.data[0] = 0x07;
                info->ext.data[1] = 0xa1;
                info->ext.data[2] = 0x20;
            }
            break;
        default:
            return 0;
        }
        break;

    default:
        break;
    }

    return (int32_t)(data - info->start);
}

static int32_t put_event(uint8_t *dest, struct EventInfo *info)
{
    int32_t i = 0, j, rc;
    static uint8_t last_event = 0;

    rc = putVLQ(dest, info->delta);
    if (dest) dest += rc;
    i += rc;

    if ((info->event != last_event) || (info->event >= 0xf0)) {
        if (dest) *dest++ = info->event;
        i++;
    }
    last_event = info->event;

    switch (info->event >> 4) {
    case 0x8: case 0x9: case 0xa: case 0xb: case 0xe:
        if (dest) { *dest++ = info->basic.param1; *dest++ = info->basic.param2; }
        i += 2;
        break;
    case 0xc: case 0xd:
        if (dest) *dest++ = info->basic.param1;
        i++;
        break;
    case 0xf:
        if (info->event == 0xff) {
            if (dest) *dest++ = info->ext.type;
            i++;
        }
        rc = putVLQ(dest, info->length);
        if (dest) dest += rc;
        i += rc;
        for (j = 0; j < (int)info->length; j++) {
            if (dest) *dest++ = info->ext.data[j];
            i++;
        }
        break;
    default:
        break;
    }
    return i;
}

/* -------------------------------------------------------------------------
 * XMIDI IFF header parser (adapted from ScummVM / Exult)
 * ---------------------------------------------------------------------- */

static int32_t read_XMIDI_header(uint8_t *data, uint32_t size, struct XMIDI_info *info)
{
    uint32_t i = 0, len, chunkLen;
    uint8_t *start, *pos = data;
    char buf[32];
    int tracksRead = 0;

    (void)size;

    if (memcmp(pos, "FORM", 4) != 0)
        return 0;
    pos += 4;

    len   = read4high(&pos);
    start = pos;

    if (memcmp(pos, "XMID", 4) == 0) {
        /* XDIRless XMIDI */
        pos += 4;
        info->num_tracks = 1;
    } else if (memcmp(pos, "XDIR", 4) != 0) {
        return 0;
    } else {
        pos += 4;
        info->num_tracks = 0;

        for (i = 4; i < len; i++) {
            memcpy(buf, pos, 4);
            pos += 4;
            chunkLen = read4high(&pos);
            i += 8;

            if (memcmp(buf, "INFO", 4) == 0) {
                if (chunkLen < 2) return 0;
                info->num_tracks = (uint8_t)read2low(&pos);
                pos += 2;
                break;
            }
            pos += (chunkLen + 1) & ~1u;
            i   += (chunkLen + 1) & ~1u;
        }

        if (info->num_tracks == 0) return 0;

        pos = start + ((len + 1) & ~1u);

        if (memcmp(pos, "CAT ", 4) != 0) return 0;
        pos += 4;
        len  = read4high(&pos);
        if (memcmp(pos, "XMID", 4) != 0) return 0;
        pos += 4;
    }

    if (info->num_tracks > ARRAYSIZE(info->tracks)) return 0;

    while (tracksRead < info->num_tracks) {
        if (!memcmp(pos, "FORM", 4)) {
            pos += 8;
        } else if (!memcmp(pos, "XMID", 4)) {
            pos += 4;
        } else if (!memcmp(pos, "TIMB", 4)) {
            pos += 4;
            len = read4high(&pos);
            pos += (len + 1) & ~1u;
        } else if (!memcmp(pos, "EVNT", 4)) {
            info->tracks[tracksRead] = pos + 8;
            pos += 4;
            len = read4high(&pos);
            pos += (len + 1) & ~1u;
            tracksRead++;
        } else {
            return 0;
        }
    }
    return 1;
}

/* -------------------------------------------------------------------------
 * MTrk builder (adapted from Exult)
 * ---------------------------------------------------------------------- */

static int32_t convert_to_mtrk(uint8_t *data, uint32_t size, uint8_t *dest)
{
    int32_t time = 0, rc;
    uint32_t i = 8;
    uint8_t *size_pos = NULL;
    uint8_t *data_end = data + size;
    struct XMIDI_info xmidi_info;
    struct EventInfo info, *cached_info;

    /* Reset the global cache (important if called twice for dry-run + real) */
    cached_events = NULL;

    if (dest) {
        *dest++ = 'M'; *dest++ = 'T'; *dest++ = 'r'; *dest++ = 'k';
        size_pos = dest;
        dest += 4;
    }

    if (!read_XMIDI_header(data, size, &xmidi_info)) return 0;
    data = xmidi_info.tracks[0];

    while (data < data_end) {
        /* Skip the end-of-stream marker here; we'll write our own later */
        if (data[0] == 0xff && data[1] == 0x2f) break;

        rc = read_event_info(data, &info, (uint32_t)time);
        if (!rc) return 0;
        data += rc;

        cached_info = pop_cached_event((uint32_t)time, info.delta);
        while (cached_info) {
            rc = put_event(dest, cached_info);
            if (!rc) return 0;
            if (dest) dest += rc;
            i   += (uint32_t)rc;
            time += (int32_t)cached_info->delta;
            info.delta -= cached_info->delta;
            free(cached_info);
            cached_info = pop_cached_event((uint32_t)time, info.delta);
        }

        rc = put_event(dest, &info);
        if (!rc) return 0;
        if (dest) dest += rc;
        i    += (uint32_t)rc;
        time += (int32_t)info.delta;

        if (info.event == 0xff && info.ext.type == 0x2f)
            break;
    }

    /* End-of-track */
    rc = putVLQ(dest, 0);
    if (dest) dest += rc;
    i += (uint32_t)rc;
    if (dest) { *dest++ = 0xff; *dest++ = 0x2f; }
    rc = putVLQ(dest, 0);
    i += 2 + (uint32_t)rc;

    if (dest) {
        dest += rc;
        write4high(&size_pos, i - 8);
    }
    return (int32_t)i;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

uint32_t convert_to_midi(uint8_t *data, uint32_t size, uint8_t **dest)
{
    int32_t len;
    uint8_t *d, *start;

    if (!dest) return 0;

    /* Dry run to measure output size */
    len = convert_to_mtrk(data, size, NULL);
    if (!len) {
        fprintf(stderr, "XMIDI: failed to parse XMIDI data\n");
        return 0;
    }

    d = (uint8_t *)malloc((size_t)(len + 14));
    if (!d) {
        fprintf(stderr, "XMIDI: out of memory\n");
        return 0;
    }
    start = d;

    /* MThd header */
    *d++ = 'M'; *d++ = 'T'; *d++ = 'h'; *d++ = 'd';
    write4high(&d, 6);
    write2high(&d, 0);   /* format 0 */
    write2high(&d, 1);   /* 1 track  */
    write2high(&d, 60);  /* 60 PPQN  */

    len = convert_to_mtrk(data, size, d);
    if (!len) {
        fprintf(stderr, "XMIDI: conversion failed on second pass\n");
        free(start);
        return 0;
    }

    *dest = start;
    return (uint32_t)(len + 14);
}
