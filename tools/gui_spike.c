/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* Nuklear + SDL3 de-risk spike: a window with a transport bar (Pause button,
 * seek slider, speed) driving a "playback" state, exactly the way the real
 * --gui would drive flade's transport. Proves the vendored Nuklear core + the
 * official SDL3 renderer backend build, link and run against SDL3. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <limits.h>
#include <time.h>

/* ----- Nuklear config (from the upstream sdl3_renderer demo) -------------- */
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_COMMAND_USERDATA    /* mandatory for sdl3_renderer */
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT /* mandatory for sdl3_renderer */

#ifndef NK_INCLUDE_FIXED_TYPES
#define NK_INT8 Sint8
#define NK_UINT8 Uint8
#define NK_INT16 Sint16
#define NK_UINT16 Uint16
#define NK_INT32 Sint32
#define NK_UINT32 Uint32
#define NK_SIZE_TYPE uintptr_t
#define NK_POINTER_TYPE uintptr_t
#endif
#ifndef NK_INCLUDE_STANDARD_BOOL
#define NK_BOOL bool
#endif

#define NK_ASSERT(c) SDL_assert(c)
#define NK_MEMSET(dst, c, len) SDL_memset(dst, c, len)
#define NK_MEMCPY(dst, src, len) SDL_memcpy(dst, src, len)
#define NK_VSNPRINTF(s, n, f, a) SDL_vsnprintf(s, n, f, a)
#define NK_STRTOD(str, endptr) SDL_strtod(str, endptr)
static char *nk_sdl_dtoa(char *str, double d);
#define NK_DTOA(str, d) nk_sdl_dtoa(str, d)
#define NK_INV_SQRT(f) (1.0f / SDL_sqrtf(f))
#define NK_SIN(f) SDL_sinf(f)
#define NK_COS(f) SDL_cosf(f)

/* keep stb's font baking off libc, on SDL */
#define STBTT_ifloor(x) ((int)SDL_floor(x))
#define STBTT_iceil(x) ((int)SDL_ceil(x))
#define STBTT_sqrt(x) SDL_sqrt(x)
#define STBTT_pow(x, y) SDL_pow(x, y)
#define STBTT_fmod(x, y) SDL_fmod(x, y)
#define STBTT_cos(x) SDL_cosf(x)
#define STBTT_acos(x) SDL_acos(x)
#define STBTT_fabs(x) SDL_fabs(x)
#define STBTT_assert(x) SDL_assert(x)
#define STBTT_strlen(x) SDL_strlen(x)
#define STBTT_memcpy SDL_memcpy
#define STBTT_memset SDL_memset
#define stbtt_uint8 Uint8
#define stbtt_int8 Sint8
#define stbtt_uint16 Uint16
#define stbtt_int16 Sint16
#define stbtt_uint32 Uint32
#define stbtt_int32 Sint32
#define STBRP_SORT SDL_qsort
#define STBRP_ASSERT SDL_assert

#define NK_IMPLEMENTATION
#include "nuklear/nuklear.h"
#define NK_SDL3_RENDERER_IMPLEMENTATION
#include "nuklear/nuklear_sdl3_renderer.h"

static char *nk_sdl_dtoa(char *str, double d) {
    if (!str)
        return NULL;
    (void)SDL_snprintf(str, 99999, "%.17g", d);
    return str;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* the "real playback state" the transport bar drives */
    int paused = 0, presses = 0;
    float pos = 0.0f, speed = 1.0f, volume = 0.7f;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_Window *win;
    SDL_Renderer *ren;
    if (!SDL_CreateWindowAndRenderer("flade --gui (Nuklear spike)", 720, 480,
                                     SDL_WINDOW_RESIZABLE, &win, &ren)) {
        SDL_Log("CreateWindowAndRenderer: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);
    float scale = SDL_GetWindowDisplayScale(win);
    SDL_SetRenderScale(ren, scale, scale);

    struct nk_context *ctx = nk_sdl_init(win, ren, nk_sdl_allocator());
    {
        struct nk_font_atlas *atlas;
        struct nk_font_config cfg = nk_font_config(0);
        struct nk_font *font;
        atlas = nk_sdl_font_stash_begin(ctx);
        font = nk_font_atlas_add_default(atlas, 15 * scale, &cfg);
        nk_sdl_font_stash_end(ctx);
        if (scale > 1.0f)
            font->handle.height /= scale;
        nk_style_set_font(ctx, &font->handle);
    }

    int running = 1;
    while (running) {
        nk_input_begin(ctx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT)
                running = 0;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)
                running = 0;
            SDL_ConvertEventToRenderCoordinates(ren, &e);
            nk_sdl_handle_event(ctx, &e);
        }
        nk_input_end(ctx);

        /* the transport bar - each widget writes the same state the keyboard
         * would, exactly as the real flade --gui would over its play loop */
        if (nk_begin(ctx, "transport", nk_rect(20, 20, 380, 230),
                     NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE)) {
            nk_layout_row_dynamic(ctx, 34, 1);
            if (nk_button_label(ctx, paused ? "Play" : "Pause")) {
                paused = !paused;
                SDL_Log("pause toggled -> %s (press #%d)", paused ? "PAUSED" : "playing", ++presses);
            }
            nk_layout_row_dynamic(ctx, 22, 1);
            nk_labelf(ctx, NK_TEXT_LEFT, "state: %s", paused ? "PAUSED" : "playing");
            nk_label(ctx, "seek", NK_TEXT_LEFT);
            nk_slider_float(ctx, 0.0f, &pos, 100.0f, 0.5f);
            nk_property_float(ctx, "speed:", 0.125f, &speed, 8.0f, 0.125f, 0.01f);
            nk_property_float(ctx, "volume:", 0.0f, &volume, 1.0f, 0.05f, 0.005f);
        }
        nk_end(ctx);

        SDL_SetRenderDrawColor(ren, 18, 18, 24, 255);
        SDL_RenderClear(ren);
        nk_sdl_render(ctx, NK_ANTI_ALIASING_ON);
        SDL_RenderPresent(ren);
    }

    nk_sdl_shutdown(ctx);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    printf("spike ok: %d button presses, final paused=%d pos=%.1f speed=%.2f\n", presses, paused,
           pos, speed);
    return 0;
}
