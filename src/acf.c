/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors
 *
 * The XCF tile-decode core below (the 64 opcode routines, decompress_frame,
 * and the offset tables) is adapted from the GPLv2 Time Commando player
 * (timeco/src/acf.c, LBALab), which is itself based on the Defence-Force
 * ACF2PCX notes and the Adeline DEC_XCF source. The chunk walk and the flade
 * movie adapter are new. Changes from the original: the timeco engine's
 * file_reader / state_t / scale_2x rendering is removed; unaligned word reads
 * are replaced with explicit little-endian byte reads for portability.
 *
 * Single active decoder at a time (the frame buffers are file-scope), which is
 * all flade needs. */
#include "acf.h"

#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;

/* safe little-endian reads (the source used unaligned *(u32*) casts) */
#define RD_U16(p) ((u16)((p)[0] | ((u16)(p)[1] << 8)))
#define RD_I16(p) ((i16)RD_U16(p))
#define RD_U24(p) ((u32)((p)[0] | ((u32)(p)[1] << 8) | ((u32)(p)[2] << 16)))
#define RD_U32(p) \
    ((u32)((p)[0] | ((u32)(p)[1] << 8) | ((u32)(p)[2] << 16) | ((u32)(p)[3] << 24)))

/* ====================================================================== *
 *  XCF tile-decode core (adapted from timeco/src/acf.c, GPLv2)
 * ====================================================================== */

static u32 diagonal_offsets_1[64] = {
    0, 1, 320, 640, 321, 2, 3, 322, 641, 960, 1280, 961, 642, 323, 4, 5, 324, 643, 962, 1281,
    1600, 1920, 1601, 1282, 963, 644, 325, 6, 7, 326, 645, 964, 1283, 1602, 1921, 2240, 2241,
    1922, 1603, 1284, 965, 646, 327, 647, 966, 1285, 1604, 1923, 2242, 2243, 1924, 1605, 1286,
    967, 1287, 1606, 1925, 2244, 2245, 1926, 1607, 1927, 2246, 2247
};

static u32 diagonal_offsets_2[64] = {
    7, 6, 327, 647, 326, 5, 4, 325, 646, 967, 1287, 966, 645, 324, 3, 2, 323, 644, 965, 1286,
    1607, 1927, 1606, 1285, 964, 643, 322, 1, 0, 321, 642, 963, 1284, 1605, 1926, 2247, 2246,
    1925, 1604, 1283, 962, 641, 320, 640, 961, 1282, 1603, 1924, 2245, 2244, 1923, 1602, 1281,
    960, 1280, 1601, 1922, 2243, 2242, 1921, 1600, 1920, 2241, 2240
};

static u32 split_tile_offsets[4] = {0, 4, 320 * 4, 320 * 4 + 4};

static i32 frame_width = 320;
static i32 frame_height = 240;

static u8 *previous_buffer;
static u8 *current_buffer;
static u8 *previous_tile;
static u8 *previous_frame_buffer;
static u8 *current_tile;

static const u8 *aligned_stream;
static const u8 *unaligned_stream;

static inline void set_pixel(i32 x, i32 y, u8 colour) {
    current_tile[x + (y * frame_width)] = colour;
}

/* 3 bytes (6 bits x 4) for the position, 4 bytes for colours */
static void update_4(void) {
    u32 value = RD_U24(unaligned_stream);
    unaligned_stream += 3;
    for (i32 i = 0; i < 4; i++) {
        set_pixel(value & 7, ((value >> 3) & 7), *aligned_stream++);
        value >>= 6;
    }
}

static void update_8(void) {
    update_4();
    update_4();
}

static void update_16(void) {
    for (i32 y = 0; y < 8; y++) {
        u8 mask = *aligned_stream++;
        for (i32 x = 0; x < 8; x++) {
            if (mask & 1)
                set_pixel(x, y, *unaligned_stream++);
            mask >>= 1;
        }
    }
}

static void block_copy_8x8(u8 *dest, const u8 *source) {
    for (i32 y = 0; y < 8; y++)
        memcpy(dest + (y * frame_width), source + (y * frame_width), 8);
}

static void block_copy_4x4(u8 *dest, const u8 *source) {
    for (i32 y = 0; y < 4; y++)
        memcpy(dest + (y * frame_width), source + (y * frame_width), 4);
}

static void zero_motion_decode(void) {
    block_copy_8x8(current_tile, previous_tile);
}

static void short_motion_8_decode(void) {
    i32 value = *unaligned_stream++;
    i32 dx = (((value & 15) << 28) >> 28);
    i32 dy = ((value << 24) >> 28);
    block_copy_8x8(current_tile, previous_tile + (4 + frame_width * 4) + dx + (dy * frame_width));
}

static void short_motion_4_decode(void) {
    i32 value = *aligned_stream++;
    i32 dx = (((value & 15) << 28) >> 28);
    i32 dy = ((value << 24) >> 28);
    block_copy_4x4(current_tile, previous_tile + 2 + (frame_width * 2) + dx + (dy * frame_width));

    value = *aligned_stream++;
    dx = (((value & 15) << 28) >> 28);
    dy = ((value << 24) >> 28);
    block_copy_4x4(current_tile + 4, previous_tile + 2 + (frame_width * 2) + dx + (dy * frame_width) + 4);

    value = *aligned_stream++;
    dx = (((value & 15) << 28) >> 28);
    dy = ((value << 24) >> 28);
    block_copy_4x4(current_tile + (frame_width * 4),
                   previous_tile + 2 + (frame_width * 2) + dx + (dy * frame_width) + (frame_width * 4));

    value = *aligned_stream++;
    dx = (((value & 15) << 28) >> 28);
    dy = ((value << 24) >> 28);
    block_copy_4x4(current_tile + (frame_width * 4) + 4,
                   previous_tile + 2 + (frame_width * 2) + dx + (dy * frame_width) + (frame_width * 4) + 4);
}

static void motion_8_decode(void) {
    block_copy_8x8(current_tile, previous_frame_buffer + RD_U16(unaligned_stream));
    unaligned_stream += 2;
}

static void motion_4_decode(void) {
    u16 offset = RD_U16(aligned_stream);
    aligned_stream += 2;
    block_copy_4x4(current_tile, previous_frame_buffer + offset);
    offset = RD_U16(aligned_stream);
    aligned_stream += 2;
    block_copy_4x4(current_tile + 4, previous_frame_buffer + offset);
    offset = RD_U16(aligned_stream);
    aligned_stream += 2;
    block_copy_4x4(current_tile + (frame_width * 4), previous_frame_buffer + offset);
    offset = RD_U16(aligned_stream);
    aligned_stream += 2;
    block_copy_4x4(current_tile + (frame_width * 4) + 4, previous_frame_buffer + offset);
}

static void ro_motion_8_decode(void) {
    i16 offset = RD_I16(unaligned_stream);
    unaligned_stream += 2;
    block_copy_8x8(current_tile, previous_tile + offset + 4 + (frame_width * 4));
}

static void ro_motion_4_decode(void) {
    i16 offset = RD_I16(aligned_stream);
    aligned_stream += 2;
    block_copy_4x4(current_tile, previous_tile + offset + 2 + (frame_width * 2));
    offset = RD_I16(aligned_stream);
    aligned_stream += 2;
    block_copy_4x4(current_tile + 4, previous_tile + 4 + offset + 2 + (frame_width * 2));
    offset = RD_I16(aligned_stream);
    aligned_stream += 2;
    block_copy_4x4(current_tile + (frame_width * 4), previous_tile + (frame_width * 4) + offset + 2 + (frame_width * 2));
    offset = RD_I16(aligned_stream);
    aligned_stream += 2;
    block_copy_4x4(current_tile + (frame_width * 4) + 4,
                   previous_tile + (frame_width * 4) + 4 + offset + 2 + (frame_width * 2));
}

static void rc_motion_8_decode(void) {
    i16 offset = (i16)((i8)unaligned_stream[0] + (i8)unaligned_stream[1] * frame_width / 2);
    unaligned_stream += 2;
    block_copy_8x8(current_tile, previous_tile + offset + 4 + (frame_width * 4));
}

static void rc_motion_4_decode(void) {
    i16 offset = (i16)((i8)aligned_stream[0] + (i8)aligned_stream[1] * frame_width / 2);
    aligned_stream += 2;
    block_copy_4x4(current_tile, previous_tile + offset + 2 + (frame_width * 2));
    offset = (i16)((i8)aligned_stream[0] + (i8)aligned_stream[1] * frame_width / 2);
    aligned_stream += 2;
    block_copy_4x4(current_tile + 4, previous_tile + offset + 2 + (frame_width * 2) + 4);
    offset = (i16)((i8)aligned_stream[0] + (i8)aligned_stream[1] * frame_width / 2);
    aligned_stream += 2;
    block_copy_4x4(current_tile + (frame_width * 4), previous_tile + offset + 2 + (frame_width * 2) + (frame_width * 4));
    offset = (i16)((i8)aligned_stream[0] + (i8)aligned_stream[1] * frame_width / 2);
    aligned_stream += 2;
    block_copy_4x4(current_tile + (frame_width * 4) + 4,
                   previous_tile + offset + 2 + (frame_width * 2) + (frame_width * 4) + 4);
}

static void single_colour_fill_decode(void) {
    u8 colour_tile = *unaligned_stream++;
    for (i32 y = 0; y < 8; y++)
        memset(current_tile + (y * frame_width), colour_tile, 8);
}

static void four_color_fill_decode(void) {
    u8 colour_top_left = *aligned_stream++;
    u8 colour_top_right = *aligned_stream++;
    u8 colour_bottom_left = *aligned_stream++;
    u8 colour_bottom_right = *aligned_stream++;
    for (i32 y = 0; y < 4; y++) {
        memset(current_tile + (y * frame_width), colour_top_left, 4);
        memset(current_tile + (y * frame_width) + 4, colour_top_right, 4);
        memset(current_tile + ((y + 4) * frame_width), colour_bottom_left, 4);
        memset(current_tile + ((y + 4) * frame_width) + 4, colour_bottom_right, 4);
    }
}

static void one_bit_tile_decode(void) {
    for (i32 y = 0; y < 8; y++) {
        u8 a = *aligned_stream++;
        for (i32 x = 0; x < 8; x++) {
            set_pixel(x, y, unaligned_stream[a & 1]);
            a >>= 1;
        }
    }
    unaligned_stream += 2;
}

static void two_bit_tile_decode(void) {
    const u8 *colors = aligned_stream;
    aligned_stream += 4;
    for (i32 y = 0; y < 8; y++) {
        i32 a = RD_U16(aligned_stream);
        aligned_stream += 2;
        for (i32 x = 0; x < 8; x++) {
            set_pixel(x, y, colors[a & 3]);
            a >>= 2;
        }
    }
}

static void three_bit_tile_decode(void) {
    for (i32 y = 0; y < 8; y++) {
        u32 a = RD_U24(aligned_stream);
        aligned_stream += 3;
        for (i32 x = 0; x < 8; x++) {
            set_pixel(x, y, unaligned_stream[a & 7]);
            a >>= 3;
        }
    }
    unaligned_stream += 8;
}

static void four_bit_tile_decode(void) {
    for (i32 y = 0; y < 8; y++) {
        u32 a = RD_U32(aligned_stream);
        aligned_stream += 4;
        for (i32 x = 0; x < 8; x++) {
            set_pixel(x, y, unaligned_stream[a & 15]);
            a >>= 4;
        }
    }
    unaligned_stream += 16;
}

static void one_bit_split_tile_decode(void) {
    for (u32 i = 0; i < 4; i++) {
        u32 offset = split_tile_offsets[i];
        u16 a = RD_U16(aligned_stream);
        aligned_stream += 2;
        for (i32 y = 0; y < 4; y++) {
            for (i32 x = 0; x < 4; x++) {
                current_tile[x + y * frame_width + offset] = aligned_stream[a & 1];
                a >>= 1;
            }
        }
        aligned_stream += 2;
    }
}

static void two_bit_split_tile_decode(void) {
    for (u32 i = 0; i < 4; i++) {
        u32 offset = split_tile_offsets[i];
        u32 a = RD_U32(aligned_stream);
        aligned_stream += 4;
        for (i32 y = 0; y < 4; y++) {
            for (i32 x = 0; x < 4; x++) {
                current_tile[x + y * frame_width + offset] = aligned_stream[a & 3];
                a >>= 2;
            }
        }
        aligned_stream += 4;
    }
}

static void three_bit_split_tile_decode(void) {
    for (u32 i = 0; i < 4; i++) {
        u32 offset = split_tile_offsets[i];
        u32 a = 0;
        for (i32 y = 0; y < 4; y++) {
            if (!(y & 1)) {
                a = RD_U24(aligned_stream);
                aligned_stream += 3;
            }
            for (i32 x = 0; x < 4; x++) {
                current_tile[x + y * frame_width + offset] = unaligned_stream[a & 7];
                a >>= 3;
            }
        }
        unaligned_stream += 8;
    }
}

static void cross_decode(void) {
    u32 value = RD_U32(aligned_stream);
    aligned_stream += 4;
    for (u32 i = 0; i < 4; i++) {
        u32 offset = split_tile_offsets[i];
        u8 *dest = current_tile + offset;

        dest[0] = aligned_stream[(value & 1)];
        dest[1] = aligned_stream[0];
        dest[2] = aligned_stream[0];
        dest[3] = aligned_stream[((value & 2) >> 1) * 3];

        dest[320] = aligned_stream[1];
        dest[321] = aligned_stream[(value & 4) >> 2];
        dest[322] = aligned_stream[((value & 8) >> 3) * 3];
        dest[323] = aligned_stream[3];

        dest[640] = aligned_stream[1];
        dest[641] = aligned_stream[1 + ((value & 16) >> 4)];
        dest[642] = aligned_stream[2 + ((value & 32) >> 5)];
        dest[643] = aligned_stream[3];

        dest[960] = aligned_stream[1 + ((value & 64) >> 6)];
        dest[961] = aligned_stream[2];
        dest[962] = aligned_stream[2];
        dest[963] = aligned_stream[2 + ((value & 128) >> 7)];

        aligned_stream += 4;
        value >>= 8;
    }
}

static void prime_decode(void) {
    i32 prime_colour = *unaligned_stream++;
    for (i32 y = 0; y < 8; y++) {
        u8 a = *aligned_stream++;
        for (i32 x = 0; x < 8; x++) {
            if (a & 1)
                set_pixel(x, y, *unaligned_stream++);
            else
                set_pixel(x, y, (u8)prime_colour);
            a >>= 1;
        }
    }
}

static void raw_tile_decode(void) {
    for (i32 y = 0; y < 8; y++) {
        memcpy(current_tile + (y * frame_width), aligned_stream, 8);
        aligned_stream += 8;
    }
}

static void one_bank_tile_decode(void) {
    u8 bank = *unaligned_stream++;
    for (i32 y = 0; y < 8; y++) {
        for (i32 x = 0; x < 8; x++) {
            if (x & 1)
                set_pixel(x, y, bank + ((*aligned_stream++) >> 4));
            else
                set_pixel(x, y, bank + ((*aligned_stream) & 15));
        }
    }
}

static void two_banks_tile_decode(void) {
    u8 bank[2];
    bank[0] = ((*unaligned_stream) & 0x0f) << 4;
    bank[1] = ((*unaligned_stream) & 0xf0);
    unaligned_stream++;
    for (u32 y = 0; y < 8; y++) {
        u32 part1 = RD_U32(aligned_stream);
        u32 part2 = RD_U32(aligned_stream + 4);
        aligned_stream += 5;
        for (u32 x = 0; x < 8; x++) {
            set_pixel(x, y, bank[(part1 & 16) >> 4] + (part1 & 15));
            part1 >>= 5;
            part1 |= (part2 << 27);
            part2 >>= 5;
        }
    }
}

static void block_decode_horizontal(void) {
    u8 last_colour = 0;
    for (u32 y = 0; y < 8; y++) {
        u8 a = *aligned_stream++;
        for (u32 x = 0; x < 8; x++) {
            if (a & 1)
                last_colour = *unaligned_stream++;
            a >>= 1;
            set_pixel(x, y, last_colour);
        }
    }
}

static void block_decode_vertical(void) {
    u8 last_colour = 0;
    for (i32 x = 0; x < 8; x++) {
        u8 a = *aligned_stream++;
        for (i32 y = 0; y < 8; y++) {
            if (a & 1)
                last_colour = *unaligned_stream++;
            a >>= 1;
            set_pixel(x, y, last_colour);
        }
    }
}

static void block_decode_2(void) {
    u8 last_colour = 0;
    u32 *offsets = diagonal_offsets_1;
    for (i32 y = 0; y < 8; y++) {
        u8 a = *aligned_stream++;
        for (i32 x = 0; x < 8; x++) {
            if (a & 1)
                last_colour = *unaligned_stream++;
            a >>= 1;
            current_tile[*offsets++] = last_colour;
        }
    }
}

static void block_decode_3(void) {
    u8 last_colour = 0;
    u32 *offsets = diagonal_offsets_2;
    for (i32 y = 0; y < 8; y++) {
        u8 a = *aligned_stream++;
        for (i32 x = 0; x < 8; x++) {
            if (a & 1)
                last_colour = *unaligned_stream++;
            a >>= 1;
            current_tile[*offsets++] = last_colour;
        }
    }
}

static void block_bank_1_decode_horizontal(void) {
    u8 last_colour = 0;
    u8 bank = (*unaligned_stream) << 4;
    u8 flag = 1;
    for (i32 y = 0; y < 8; y++) {
        u8 a = *aligned_stream++;
        for (i32 x = 0; x < 8; x++) {
            if (a & 1) {
                if (flag) {
                    last_colour = (*unaligned_stream) >> 4;
                    flag = 0;
                    unaligned_stream++;
                } else {
                    last_colour = (*unaligned_stream) & 15;
                    flag++;
                }
            }
            a >>= 1;
            set_pixel(x, y, bank + last_colour);
        }
    }
    if (flag)
        unaligned_stream++;
}

static void block_bank_1_decode_vertical(void) {
    u8 last_colour = 0;
    u8 bank = (*unaligned_stream) << 4;
    u8 flag = 1;
    for (i32 x = 0; x < 8; x++) {
        u8 a = *aligned_stream++;
        for (i32 y = 0; y < 8; y++) {
            if (a & 1) {
                if (flag) {
                    last_colour = (*unaligned_stream) >> 4;
                    flag = 0;
                    unaligned_stream++;
                } else {
                    last_colour = (*unaligned_stream) & 15;
                    flag++;
                }
            }
            a >>= 1;
            set_pixel(x, y, bank + last_colour);
        }
    }
    if (flag)
        unaligned_stream++;
}

static void block_bank_1_decode_2(void) {
    u8 last_colour = 0;
    u8 bank = (*unaligned_stream) << 4;
    u8 flag = 1;
    u32 *offsets = diagonal_offsets_1;
    for (i32 y = 0; y < 8; y++) {
        u8 a = *aligned_stream++;
        for (i32 x = 0; x < 8; x++) {
            if (a & 1) {
                if (flag) {
                    last_colour = (*unaligned_stream) >> 4;
                    flag = 0;
                    unaligned_stream++;
                } else {
                    last_colour = (*unaligned_stream) & 15;
                    flag++;
                }
            }
            a >>= 1;
            current_tile[*offsets++] = bank + last_colour;
        }
    }
    if (flag)
        unaligned_stream++;
}

static void block_bank_1_decode_3(void) {
    u8 last_colour = 0;
    u8 bank = (*unaligned_stream) << 4;
    u8 flag = 1;
    u32 *offsets = diagonal_offsets_2;
    for (i32 y = 0; y < 8; y++) {
        u8 a = *aligned_stream++;
        for (i32 x = 0; x < 8; x++) {
            if (a & 1) {
                if (flag) {
                    last_colour = (*unaligned_stream) >> 4;
                    flag = 0;
                    unaligned_stream++;
                } else {
                    last_colour = (*unaligned_stream) & 15;
                    flag++;
                }
            }
            a >>= 1;
            current_tile[*offsets++] = bank + last_colour;
        }
    }
    if (flag)
        unaligned_stream++;
}

/* Decode one frame chunk payload (u32 colour_offset, then the opcode stream)
 * into current_buffer, referencing previous_buffer for motion. */
static void decompress_frame(const u8 *fd) {
    previous_tile = previous_frame_buffer = previous_buffer;
    current_tile = current_buffer;

    u32 colour_offset = RD_U32(fd);
    unaligned_stream = fd + colour_offset;
    const u8 *opcodes = fd + 4;
    aligned_stream = opcodes + (frame_height / 8) * 30;

    i32 opcode = -1;
    const u8 *opcode_ptr = opcodes;

    for (i32 y = 0; y < (frame_height / 8); y++) {
        for (i32 x = 0; x < (frame_width / 8); x++) {
            if (opcode == -1) {
                opcode = (i32)(RD_U24(opcode_ptr) | 0xff000000u);
                opcode_ptr += 3;
            }

            switch (opcode & 63) {
            case 0: raw_tile_decode(); break;

            case 1: zero_motion_decode(); break;
            case 2: zero_motion_decode(); update_4(); break;
            case 3: zero_motion_decode(); update_8(); break;
            case 4: zero_motion_decode(); update_16(); break;

            case 5: short_motion_8_decode(); break;
            case 6: short_motion_8_decode(); update_4(); break;
            case 7: short_motion_8_decode(); update_8(); break;
            case 8: short_motion_8_decode(); update_16(); break;

            case 9: motion_8_decode(); break;
            case 10: motion_8_decode(); update_4(); break;
            case 11: motion_8_decode(); update_8(); break;
            case 12: motion_8_decode(); update_16(); break;

            case 13: short_motion_4_decode(); break;
            case 14: short_motion_4_decode(); update_4(); break;
            case 15: short_motion_4_decode(); update_8(); break;
            case 16: short_motion_4_decode(); update_16(); break;

            case 17: motion_4_decode(); break;
            case 18: motion_4_decode(); update_4(); break;
            case 19: motion_4_decode(); update_8(); break;
            case 20: motion_4_decode(); update_16(); break;

            case 21: single_colour_fill_decode(); break;
            case 22: single_colour_fill_decode(); update_4(); break;
            case 23: single_colour_fill_decode(); update_8(); break;
            case 24: single_colour_fill_decode(); update_16(); break;

            case 25: four_color_fill_decode(); break;
            case 26: four_color_fill_decode(); update_4(); break;
            case 27: four_color_fill_decode(); update_8(); break;
            case 28: four_color_fill_decode(); update_16(); break;

            case 29: one_bit_tile_decode(); break;
            case 30: two_bit_tile_decode(); break;
            case 31: three_bit_tile_decode(); break;
            case 32: four_bit_tile_decode(); break;

            case 33: one_bit_split_tile_decode(); break;
            case 34: two_bit_split_tile_decode(); break;
            case 35: three_bit_split_tile_decode(); break;

            case 36: cross_decode(); break;
            case 37: prime_decode(); break;

            case 38: one_bank_tile_decode(); break;
            case 39: two_banks_tile_decode(); break;

            case 40: block_decode_horizontal(); break;
            case 41: block_decode_vertical(); break;
            case 42: block_decode_2(); break;
            case 43: block_decode_3(); break;

            case 44: block_bank_1_decode_horizontal(); break;
            case 45: block_bank_1_decode_vertical(); break;
            case 46: block_bank_1_decode_2(); break;
            case 47: block_bank_1_decode_3(); break;

            case 48: ro_motion_8_decode(); break;
            case 49: ro_motion_8_decode(); update_4(); break;
            case 50: ro_motion_8_decode(); update_8(); break;
            case 51: ro_motion_8_decode(); update_16(); break;

            case 52: rc_motion_8_decode(); break;
            case 53: rc_motion_8_decode(); update_4(); break;
            case 54: rc_motion_8_decode(); update_8(); break;
            case 55: rc_motion_8_decode(); update_16(); break;

            case 56: ro_motion_4_decode(); break;
            case 57: ro_motion_4_decode(); update_4(); break;
            case 58: ro_motion_4_decode(); update_8(); break;
            case 59: ro_motion_4_decode(); update_16(); break;

            case 60: rc_motion_4_decode(); break;
            case 61: rc_motion_4_decode(); update_4(); break;
            case 62: rc_motion_4_decode(); update_8(); break;
            case 63: rc_motion_4_decode(); update_16(); break;
            }

            opcode >>= 6;
            previous_tile += 8;
            current_tile += 8;
        }
        previous_tile += (frame_width * 7);
        current_tile += (frame_width * 7);
    }
}

/* ====================================================================== *
 *  flade shell: chunk walk + movie adapter (new)
 * ====================================================================== */

static int chunk_is(const u8 *p, const char *tag) {
    return memcmp(p, tag, 8) == 0;
}

static void read_format(acf_t *a, const u8 *pay, u32 size) {
    if (size >= 28) {
        a->width = (int)RD_U32(pay + 4);
        a->height = (int)RD_U32(pay + 8);
        u32 play_rate = RD_U32(pay + 24);
        a->fps = (play_rate >= 5 && play_rate <= 60) ? (double)play_rate : 15.0;
    }
    if (size >= 36) {
        u32 sampling_rate = RD_U32(pay + 28);
        u32 sample_type = RD_U32(pay + 32); /* 2 = stereo */
        a->audio_rate = (sampling_rate >= 4000 && sampling_rate <= 48000) ? (int)sampling_rate : 22050;
        a->audio_channels = (sample_type == 2) ? 2 : 1;
    }
}

static int alloc_buffers(int w, int h) {
    size_t n = (size_t)w * (size_t)h;
    free(current_buffer);
    free(previous_buffer);
    current_buffer = calloc(1, n ? n : 1);
    previous_buffer = calloc(1, n ? n : 1);
    return (current_buffer && previous_buffer) ? 0 : -1;
}

int acf_open(acf_t *a, const uint8_t *data, size_t size) {
    memset(a, 0, sizeof(*a));
    if (size < 12 || !chunk_is(data, "FrameLen"))
        return -1;

    a->data = data;
    a->size = size;
    a->width = 320;
    a->height = 240;
    a->fps = 15.0;
    a->audio_rate = 22050;
    a->audio_channels = 2;

    /* pre-walk: read Format, count frames, and gather the audio (the Sound*
     * chunks are raw 8-bit unsigned PCM, contiguous = the whole soundtrack) */
    size_t pos = 0;
    int frames = 0;
    uint8_t *araw = NULL;
    size_t alen = 0, acap = 0;
    while (pos + 12 <= size) {
        const u8 *c = data + pos;
        u32 csize = RD_U32(c + 8);
        if (pos + 12 + (size_t)csize > size)
            break;
        if (chunk_is(c, "End     "))
            break;
        if (chunk_is(c, "Format  ")) {
            read_format(a, c + 12, csize);
        } else if (chunk_is(c, "KeyFrame") || chunk_is(c, "DltFrame")) {
            frames++;
        } else if (chunk_is(c, "SoundBuf") || chunk_is(c, "SoundFrm")) {
            if (alen + csize > acap) {
                size_t ncap = acap ? acap * 2 : 1u << 20;
                while (ncap < alen + csize)
                    ncap *= 2;
                uint8_t *n = realloc(araw, ncap);
                if (n) {
                    araw = n;
                    acap = ncap;
                }
            }
            if (acap >= alen + csize) {
                memcpy(araw + alen, c + 12, csize);
                alen += csize;
            }
        }
        pos += 12 + csize;
    }
    a->num_frames = frames;

    if (alen > 0 && a->audio_channels > 0) {
        a->audio = malloc(alen * sizeof(int16_t));
        if (a->audio) {
            for (size_t i = 0; i < alen; i++)
                a->audio[i] = (int16_t)(((int)araw[i] - 128) << 8);
            a->audio_frames = alen / (size_t)a->audio_channels;
        }
    }
    free(araw);

    if (alloc_buffers(a->width, a->height) != 0)
        return -1;
    frame_width = a->width;
    frame_height = a->height;
    a->pos = 0;
    a->cur_frame = 0;
    return 0;
}

int acf_step(acf_t *a) {
    while (a->pos + 12 <= a->size) {
        const u8 *c = a->data + a->pos;
        u32 csize = RD_U32(c + 8);
        if (a->pos + 12 + (size_t)csize > a->size)
            return 0;
        const u8 *pay = c + 12;
        size_t next = a->pos + 12 + csize;

        if (chunk_is(c, "End     ")) {
            a->pos = a->size;
            return 0;
        } else if (chunk_is(c, "Format  ")) {
            read_format(a, pay, csize);
            frame_width = a->width;
            frame_height = a->height;
            size_t n = (size_t)frame_width * (size_t)frame_height;
            memset(current_buffer, 0, n);
            memset(previous_buffer, 0, n);
        } else if (chunk_is(c, "Palette ")) {
            memcpy(a->palette, pay, csize < sizeof(a->palette) ? csize : sizeof(a->palette));
        } else if (chunk_is(c, "KeyFrame") || chunk_is(c, "DltFrame")) {
            decompress_frame(pay);
            a->frame = current_buffer;
            a->cur_frame++;
            a->pos = next;
            /* double buffer: this frame becomes the reference for the next */
            u8 *t = current_buffer;
            current_buffer = previous_buffer;
            previous_buffer = t;
            return 1;
        }
        a->pos = next;
    }
    return 0;
}

void acf_close(acf_t *a) {
    if (a) {
        free(a->audio);
        a->audio = NULL;
    }
    free(current_buffer);
    free(previous_buffer);
    current_buffer = NULL;
    previous_buffer = NULL;
}

/* ----- movie-interface adapter -------------------------------------------- */
typedef struct {
    acf_t acf;
} acf_movie_t;

static int acf_movie_step(movie_t *m, movie_frame *out) {
    acf_movie_t *am = (acf_movie_t *)m->impl;
    if (!acf_step(&am->acf))
        return 0;
    out->pixels = am->acf.frame;
    out->palette = am->acf.palette;
    out->duration = 1.0 / am->acf.fps;
    return 1;
}

static void acf_movie_close(movie_t *m) {
    acf_movie_t *am = (acf_movie_t *)m->impl;
    if (am) {
        acf_close(&am->acf);
        free(am);
    }
    m->impl = NULL;
}

int acf_movie_open(movie_t *m, const uint8_t *data, size_t size) {
    acf_movie_t *am = calloc(1, sizeof(*am));
    if (!am)
        return -1;
    if (acf_open(&am->acf, data, size) != 0) {
        free(am);
        return -1;
    }
    m->kind = MOVIE_ACF;
    m->width = am->acf.width;
    m->height = am->acf.height;
    m->num_frames = am->acf.num_frames;
    m->fps = am->acf.fps;
    m->step = acf_movie_step;
    m->close = acf_movie_close;
    m->impl = am;
    m->audio_pcm = am->acf.audio;
    m->audio_frames = am->acf.audio_frames;
    m->audio_rate = am->acf.audio_rate;
    m->audio_channels = am->acf.audio_channels;
    return 0;
}
