/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* The windowed start screen, drawn with Nuklear over SDL3. This is the only
 * translation unit that compiles the vendored GUI toolkit. For now it just
 * lets the user pick a movie (button -> native file dialog, or drag-and-drop)
 * and hands the path back; the player itself is unchanged. */
#include "gui.h"

#include <SDL3/SDL.h>
#include <stdlib.h>

/* ----- Nuklear config (matches tools/gui_spike.c) ------------------------- */
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_COMMAND_USERDATA
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
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
static char *nk_flade_dtoa(char *str, double d);
#define NK_DTOA(str, d) nk_flade_dtoa(str, d)
#define NK_INV_SQRT(f) (1.0f / SDL_sqrtf(f))
#define NK_SIN(f) SDL_sinf(f)
#define NK_COS(f) SDL_cosf(f)
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

static char *nk_flade_dtoa(char *str, double d) {
    if (str)
        (void)SDL_snprintf(str, 99999, "%.17g", d);
    return str;
}

/* the file the dialog / drag-drop produced (the dialog callback may fire on
 * another thread, so guard the handoff) */
struct pick {
    char *path;
    SDL_AtomicInt done;
};

static void SDLCALL dialog_cb(void *userdata, const char *const *filelist, int filter) {
    (void)filter;
    struct pick *p = (struct pick *)userdata;
    if (filelist && filelist[0])
        p->path = SDL_strdup(filelist[0]);
    SDL_SetAtomicInt(&p->done, 1);
}

char *gui_pick_movie(void) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("flade: SDL_Init failed: %s", SDL_GetError());
        return NULL;
    }
    SDL_Window *win;
    SDL_Renderer *ren;
    if (!SDL_CreateWindowAndRenderer("flade", 560, 360, SDL_WINDOW_RESIZABLE, &win, &ren)) {
        SDL_Log("flade: window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return NULL;
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
        font = nk_font_atlas_add_default(atlas, 16 * scale, &cfg);
        nk_sdl_font_stash_end(ctx);
        if (scale > 1.0f)
            font->handle.height /= scale;
        nk_style_set_font(ctx, &font->handle);
    }

    static const SDL_DialogFileFilter filters[] = {
        {"Adeline movies (fla, acf, smk)", "fla;acf;smk;FLA;ACF;SMK"},
        {"CD images / archives (gog, dot, hqr, bin, iso)", "gog;dot;hqr;bin;iso;GOG;DOT;HQR"},
        {"All files", "*"},
    };

    struct pick pick;
    pick.path = NULL;
    SDL_SetAtomicInt(&pick.done, 0);
    int dialog_open = 0;
    int running = 1;

    while (running && !pick.path) {
        if (dialog_open && SDL_GetAtomicInt(&pick.done))
            break; /* the dialog returned a file (or was cancelled) */

        nk_input_begin(ctx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT)
                running = 0;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)
                running = 0;
            else if (e.type == SDL_EVENT_DROP_FILE && e.drop.data)
                pick.path = SDL_strdup(e.drop.data);
            SDL_ConvertEventToRenderCoordinates(ren, &e);
            nk_sdl_handle_event(ctx, &e);
        }
        nk_input_end(ctx);

        int ow, oh;
        SDL_GetRenderOutputSize(ren, &ow, &oh);
        float w = ow / (scale > 0 ? scale : 1), h = oh / (scale > 0 ? scale : 1);
        if (nk_begin(ctx, "flade", nk_rect(0, 0, w, h), NK_WINDOW_BORDER)) {
            nk_layout_row_dynamic(ctx, h * 0.18f, 1);
            nk_label(ctx, "flade", NK_TEXT_CENTERED);
            nk_layout_row_dynamic(ctx, 24, 1);
            nk_label(ctx, "a player for Adeline movies (LBA1 / Time Commando / LBA2)",
                     NK_TEXT_CENTERED);
            nk_layout_row_dynamic(ctx, 16, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 44, 1);
            if (!dialog_open && nk_button_label(ctx, "Open movie or disc...")) {
                dialog_open = 1;
                SDL_ShowOpenFileDialog(dialog_cb, &pick, win, filters,
                                       (int)(sizeof(filters) / sizeof(filters[0])), NULL, false);
            }
            nk_layout_row_dynamic(ctx, 20, 1);
            nk_label(ctx, "...or drag a file onto this window", NK_TEXT_CENTERED);
        }
        nk_end(ctx);

        SDL_SetRenderDrawColor(ren, 18, 18, 24, 255);
        SDL_RenderClear(ren);
        nk_sdl_render(ctx, NK_ANTI_ALIASING_ON);
        SDL_RenderPresent(ren);
        SDL_DelayNS(8 * 1000000);
    }

    char *result = pick.path; /* SDL_strdup'd; caller owns it until exit */

    nk_sdl_shutdown(ctx);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    /* leave SDL initialised: the player path re-uses the subsystems */
    return result;
}
