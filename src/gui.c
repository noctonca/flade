/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* The windowed start screen, drawn with Nuklear over SDL3 - the only TU that
 * compiles the vendored GUI toolkit. The user opens a movie file, or opens a CD
 * image and picks from its list of cinematics; the choice is handed back to the
 * player. The player itself is unchanged. */
#include "gui.h"
#include "iso9660.h"
#include "source.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

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

/* the dialog callback may fire on another thread, so guard the handoff */
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

#define GUI_MAX_ITEMS 512

/* one window/renderer/context, owned by main and set up once via gui_init, so
 * the browser and the player share a single window. */
static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static struct nk_context *g_ctx;
static float g_scale = 1.0f;
static char g_status[160]; /* a transient message for the start screen */

void gui_set_status(const char *msg) {
    snprintf(g_status, sizeof(g_status), "%s", msg ? msg : "");
}

void gui_init(SDL_Window *win, SDL_Renderer *ren) {
    g_win = win;
    g_ren = ren;
    g_scale = SDL_GetWindowDisplayScale(win);
    if (g_scale <= 0)
        g_scale = 1.0f;
    g_ctx = nk_sdl_init(win, ren, nk_sdl_allocator());
    struct nk_font_atlas *atlas = nk_sdl_font_stash_begin(g_ctx);
    struct nk_font_config cfg = nk_font_config(0);
    struct nk_font *font = nk_font_atlas_add_default(atlas, 16 * g_scale, &cfg);
    nk_sdl_font_stash_end(g_ctx);
    if (g_scale > 1.0f)
        font->handle.height /= g_scale;
    nk_style_set_font(g_ctx, &font->handle);
}

void gui_shutdown(void) {
    if (g_ctx)
        nk_sdl_shutdown(g_ctx);
    g_ctx = NULL;
}

/* Browse for something to play. `initial` (or $FLADE_OPEN) is auto-opened on
 * entry, so returning here after a movie re-shows the same disc's list. */
gui_choice gui_browse(const char *initial) {
    gui_choice result = {NULL, NULL, -1};
    SDL_Window *win = g_win; /* local aliases keep the loop body unchanged */
    SDL_Renderer *ren = g_ren;
    struct nk_context *ctx = g_ctx;
    float scale = g_scale;
    if (!ctx)
        return result;
    SDL_SetRenderScale(ren, scale, scale);
    SDL_SetWindowTitle(win, "flade"); /* reset from a movie title */

    /* The first filter is the default, so make it everything flade can read - a
     * disc image, an archive, or a loose movie - which is what the user expects
     * to see (their movies live inside the .gog / .dot / .bin images). */
    static const SDL_DialogFileFilter filters[] = {
        {"flade files (movies & discs)", "fla;acf;smk;gog;dot;bin;iso;hqr;FLA;ACF;SMK;GOG;DOT;HQR"},
        {"All files", "*"},
    };

    struct pick pick;
    pick.path = NULL;
    SDL_SetAtomicInt(&pick.done, 0);

    char *pending = NULL;       /* a path awaiting classification */
    char *container = NULL;     /* the open disc or movie-HQR (browse state) */
    char container_name[128] = {0};
    int container_is_disc = 0;
    static source_item items[GUI_MAX_ITEMS];
    int n_items = 0;
    int dialog_open = 0, running = 1;

    /* dev hooks for headless screenshots: FLADE_OPEN=<path> auto-opens it on
     * start; FLADE_SHOT=<file.bmp> saves the rendered frame and exits. Lets the
     * UI be captured and reviewed without a person at the keyboard. */
    const char *shot_path = getenv("FLADE_SHOT");
    int shot_frames = 0;
    static int open_consumed = 0; /* FLADE_OPEN fires once, not every browse */
    const char *open_env = open_consumed ? NULL : getenv("FLADE_OPEN");
    open_consumed = 1;
    if (initial)
        pending = SDL_strdup(initial); /* re-show this disc/HQR (back-to-list) */
    else if (open_env)
        pending = SDL_strdup(open_env);

    while (running) {
        if (dialog_open && SDL_GetAtomicInt(&pick.done)) {
            dialog_open = 0;
            SDL_SetAtomicInt(&pick.done, 0);
            if (pick.path) {
                pending = pick.path;
                pick.path = NULL;
            }
        }
        /* classify a freshly picked/dropped path: a CD image and a loose
         * movie-HQR (VIDEO.HQR) both open a browse list; anything else is a
         * loose movie file we hand straight back. */
        if (pending) {
            g_status[0] = 0; /* a fresh pick clears the last message */
            iso9660_t *iso = iso_open(pending);
            int nh = 0;
            if (iso) {
                n_items = source_movies(iso, items, GUI_MAX_ITEMS);
                iso_close(iso);
                container_is_disc = 1;
            } else if ((nh = source_hqr_movies(pending, items, GUI_MAX_ITEMS)) > 0) {
                n_items = nh;
                container_is_disc = 0;
            }
            if (iso || nh > 0) {
                SDL_free(container);
                container = pending;
                pending = NULL;
                const char *b = strrchr(container, '/');
                snprintf(container_name, sizeof(container_name), "%s", b ? b + 1 : container);
            } else {
                result.movie = pending; /* a plain loose movie */
                pending = NULL;
                running = 0;
            }
        }
        if (!running)
            break;

        nk_input_begin(ctx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT)
                running = 0;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                if (container) { /* leave the list, back to the start screen */
                    SDL_free(container);
                    container = NULL;
                    n_items = 0;
                    g_status[0] = 0;
                } else {
                    running = 0; /* quit from the start screen */
                }
            } else if (e.type == SDL_EVENT_DROP_FILE && e.drop.data && !pending)
                pending = SDL_strdup(e.drop.data);
            SDL_ConvertEventToRenderCoordinates(ren, &e);
            nk_sdl_handle_event(ctx, &e);
        }
        nk_input_end(ctx);

        int ow, oh;
        SDL_GetRenderOutputSize(ren, &ow, &oh);
        float w = ow / (scale > 0 ? scale : 1), h = oh / (scale > 0 ? scale : 1);

        if (nk_begin(ctx, "flade", nk_rect(0, 0, w, h), NK_WINDOW_BORDER)) {
            if (container) {
                /* ----- browse a disc / movie-HQR ----- */
                nk_layout_row_dynamic(ctx, 28, 1);
                nk_labelf(ctx, NK_TEXT_LEFT, "%s  -  %d movie%s (click to play)", container_name,
                          n_items, n_items == 1 ? "" : "s");
                nk_layout_row_dynamic(ctx, h - 96, 1);
                if (nk_group_begin(ctx, "list", NK_WINDOW_BORDER)) {
                    nk_layout_row_dynamic(ctx, 26, 1);
                    if (n_items == 0)
                        nk_label(ctx, "(no movies found here)", NK_TEXT_CENTERED);
                    for (int i = 0; i < n_items; i++) {
                        if (nk_button_label(ctx, items[i].name)) {
                            if (container_is_disc) {
                                result.cd_path = container; /* play --cd <disc> <arg> */
                                result.movie = SDL_strdup(items[i].arg);
                            } else {
                                result.movie = container; /* play <hqr> --index N */
                                result.video_index = items[i].index;
                            }
                            container = NULL; /* ownership moved to result */
                            running = 0;
                            break;
                        }
                    }
                    nk_group_end(ctx);
                }
                nk_layout_row_dynamic(ctx, 30, 1);
                if (nk_button_label(ctx, "Open another...")) {
                    dialog_open = 1;
                    SDL_ShowOpenFileDialog(dialog_cb, &pick, win, filters,
                                           (int)(sizeof(filters) / sizeof(filters[0])), NULL, false);
                }
            } else {
                /* ----- start screen ----- */
                nk_layout_row_dynamic(ctx, h * 0.16f, 1);
                nk_label(ctx, "flade", NK_TEXT_CENTERED);
                nk_layout_row_dynamic(ctx, 22, 1);
                nk_label(ctx, "a player for Adeline movies (LBA1 / Time Commando / LBA2)",
                         NK_TEXT_CENTERED);
                nk_layout_row_dynamic(ctx, 16, 1);
                nk_spacing(ctx, 1);
                nk_layout_row_dynamic(ctx, 46, 1);
                if (!dialog_open && nk_button_label(ctx, "Open movie or disc...")) {
                    dialog_open = 1;
                    SDL_ShowOpenFileDialog(dialog_cb, &pick, win, filters,
                                           (int)(sizeof(filters) / sizeof(filters[0])), NULL, false);
                }
                nk_layout_row_dynamic(ctx, 22, 1);
                nk_label(ctx, "...or drag a movie or disc onto this window", NK_TEXT_CENTERED);
                if (g_status[0]) {
                    nk_layout_row_dynamic(ctx, 18, 1);
                    nk_spacing(ctx, 1);
                    nk_layout_row_dynamic(ctx, 22, 1);
                    nk_label_colored(ctx, g_status, NK_TEXT_CENTERED, nk_rgb(232, 178, 92));
                }
            }
        }
        nk_end(ctx);

        SDL_SetRenderDrawColor(ren, 18, 18, 24, 255);
        SDL_RenderClear(ren);
        nk_sdl_render(ctx, NK_ANTI_ALIASING_ON);
        if (shot_path && ++shot_frames >= 3) { /* let the UI settle, then grab it */
            SDL_Surface *snap = SDL_RenderReadPixels(ren, NULL);
            if (snap) {
                SDL_SaveBMP(snap, shot_path);
                SDL_DestroySurface(snap);
            }
            running = 0;
        }
        SDL_RenderPresent(ren);
        SDL_DelayNS(8 * 1000000);
    }

    SDL_free(pending);
    SDL_free(container); /* the un-chosen disc/HQR, if any (a chosen one moved to result) */
    return result;       /* window/context persist; main owns them */
}

/* ----- the in-playback transport overlay ---------------------------------- */
void gui_input_begin(void) {
    if (g_ctx)
        nk_input_begin(g_ctx);
}
void gui_input_event(SDL_Event *e) {
    if (g_ctx)
        nk_sdl_handle_event(g_ctx, e);
}
void gui_input_end(void) {
    if (g_ctx)
        nk_input_end(g_ctx);
}

void gui_overlay(transport_ui *t) {
    static const char *LANG[] = {"FR", "DE", "EN", "L4"};
    struct nk_context *ctx = g_ctx;
    if (!ctx)
        return;
    if (t->visible) {
        int ow, oh;
        SDL_GetRenderOutputSize(g_ren, &ow, &oh);
        float barh = 80.0f + (t->n_voices > 0 ? 30.0f : 0.0f);
        if (nk_begin(ctx, "transport", nk_rect(0, oh - barh, (float)ow, barh),
                     NK_WINDOW_NO_SCROLLBAR)) {
            /* row 1: play/pause, seek, time */
            float r1[] = {0.14f, 0.64f, 0.22f};
            nk_layout_row(ctx, NK_DYNAMIC, 30, 3, r1);
            if (nk_button_label(ctx, t->ended ? "Replay" : t->paused ? "Play" : "Pause"))
                t->toggle_pause = 1;
            float sf = (float)(int)t->pos; /* integer frame, so the step doesn't self-trigger */
            float maxf = (float)(t->num_frames > 1 ? t->num_frames - 1 : 1);
            if (nk_slider_float(ctx, 0.0f, &sf, maxf, 1.0f))
                t->seek_to = sf; /* user dragged the playhead */
            int cs = t->fps > 0 ? (int)(t->pos / t->fps) : 0;
            int ts = t->fps > 0 ? (int)((t->num_frames - 1) / t->fps) : 0;
            nk_labelf(ctx, NK_TEXT_RIGHT, "%d:%02d / %d:%02d", cs / 60, cs % 60, ts / 60, ts % 60);
            /* row 2: back, speed, fullscreen */
            float r2[] = {0.3f, 0.4f, 0.3f};
            nk_layout_row(ctx, NK_DYNAMIC, 26, 3, r2);
            if (nk_button_label(ctx, t->has_list ? "back to list" : "back"))
                t->back = 1;
            nk_labelf(ctx, NK_TEXT_CENTERED, "%.2fx", t->speed);
            if (nk_button_label(ctx, "fullscreen"))
                t->toggle_fullscreen = 1;
            /* row 3: language (Smacker only) */
            if (t->n_voices > 0) {
                nk_layout_row_dynamic(ctx, 24, t->n_voices);
                for (int i = 0; i < t->n_voices; i++) {
                    int sel = (i == t->active_voice);
                    if (nk_selectable_label(ctx, i < 4 ? LANG[i] : "L", NK_TEXT_CENTERED, &sel) &&
                        sel)
                        t->set_voice = i;
                }
            }
        }
        nk_end(ctx);
    }
    nk_sdl_render(ctx, NK_ANTI_ALIASING_ON); /* draws the bar (or nothing) and clears the frame */
}
