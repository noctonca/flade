/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - a standalone player for Adeline movies, with SDL3 for video and
 * audio. Plays from a loose movie file or straight out of a raw CD image.
 * Video is decoded through the generic movie interface (see movie.h); FLA
 * sound effects still ride the cue-based path until audio is unified. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "movie.h"
#include "fla.h"
#include "hqr.h"
#include "voc.h"
#include "audio.h"
#include "iso9660.h"

/* ----- a decoded-sample cache, keyed by FLASAMP.HQR index ----------------- */
typedef struct {
    voc_t voc;
    int loaded; /* attempted */
    int ok;     /* usable */
} sample_slot;

typedef struct {
    uint8_t *hqr;
    size_t hqr_size;
    int count;
    sample_slot *slots;
} sample_bank;

static int bank_init(sample_bank *b, uint8_t *hqr, size_t size) {
    memset(b, 0, sizeof(*b));
    b->count = hqr_count(hqr, size);
    if (b->count <= 0)
        return -1;
    b->hqr = hqr;
    b->hqr_size = size;
    b->slots = calloc((size_t)b->count, sizeof(sample_slot));
    return b->slots ? 0 : -1;
}

static const voc_t *bank_get(sample_bank *b, int index) {
    if (!b->slots || index < 0 || index >= b->count)
        return NULL;
    sample_slot *s = &b->slots[index];
    if (!s->loaded) {
        s->loaded = 1;
        uint8_t *raw = NULL;
        size_t rawsize = 0;
        if (hqr_entry(b->hqr, b->hqr_size, index, &raw, &rawsize) == 0) {
            /* The first byte of an FLASAMP entry is a flag, not 'C'. */
            if (rawsize > 0 && raw[0] != 'C')
                raw[0] = 'C';
            if (voc_parse(raw, rawsize, &s->voc) == 0)
                s->ok = 1;
            free(raw);
        }
    }
    return s->ok ? &s->voc : NULL;
}

/* ----- helpers ------------------------------------------------------------ */
static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = malloc((size_t)n ? (size_t)n : 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf)
        *size = (size_t)n;
    return buf;
}

static void list_cb(void *ud, const char *name, uint32_t size) {
    (void)ud;
    printf("  %-16s %9u bytes\n", name, size);
}

static void usage(void) {
    printf("flade - Adeline movie player\n\n"
           "Usage:\n"
           "  flade <movie.fla> [--flasamp FLASAMP.HQR] [options]\n"
           "  flade --cd <LBA.DOT> <MOVIENAME> [options]\n"
           "  flade --cd <LBA.DOT> --list\n\n"
           "Options:\n"
           "  --cd <image>     read the movie (and samples) from a raw LBA1 CD image\n"
           "  --flasamp <file> FLASAMP.HQR for a loose .fla (default: alongside it)\n"
           "  --list           with --cd, list the movies in the image and exit\n"
           "  --scale <n>      initial window scale (default 3)\n"
           "  --no-audio       video only\n"
           "  --volume <f>     master volume 0..1 (default 0.7)\n");
}

/* Derive "<dir-of-path>/FLASAMP.HQR". */
static void sibling_flasamp(const char *movie, char *out, size_t cap) {
    const char *slash = strrchr(movie, '/');
#ifdef _WIN32
    const char *bslash = strrchr(movie, '\\');
    if (bslash > slash)
        slash = bslash;
#endif
    if (slash) {
        size_t dlen = (size_t)(slash - movie) + 1;
        if (dlen >= cap)
            dlen = cap - 1;
        memcpy(out, movie, dlen);
        out[dlen] = 0;
        strncat(out, "FLASAMP.HQR", cap - strlen(out) - 1);
    } else {
        snprintf(out, cap, "FLASAMP.HQR");
    }
}

int main(int argc, char **argv) {
    const char *movie = NULL;
    const char *cd_path = NULL;
    const char *flasamp_path = NULL;
    int do_list = 0, no_audio = 0, scale = 3;
    float volume = 0.7f;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--cd") && i + 1 < argc)
            cd_path = argv[++i];
        else if (!strcmp(a, "--flasamp") && i + 1 < argc)
            flasamp_path = argv[++i];
        else if (!strcmp(a, "--list"))
            do_list = 1;
        else if (!strcmp(a, "--no-audio"))
            no_audio = 1;
        else if (!strcmp(a, "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(a, "--volume") && i + 1 < argc)
            volume = (float)atof(argv[++i]);
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage();
            return 0;
        } else if (a[0] != '-')
            movie = a;
    }
    if (scale < 1)
        scale = 1;

    iso9660_t *iso = NULL;
    if (cd_path) {
        iso = iso_open(cd_path);
        if (!iso) {
            fprintf(stderr, "flade: '%s' is not a CD image I can read\n", cd_path);
            return 1;
        }
        if (do_list) {
            printf("Movies in %s (LBA/FLA):\n", cd_path);
            if (iso_list(iso, "LBA/FLA", list_cb, NULL) != 0)
                fprintf(stderr, "flade: no LBA/FLA directory in image\n");
            iso_close(iso);
            return 0;
        }
    }

    if (!movie) {
        usage();
        if (iso)
            iso_close(iso);
        return 1;
    }

    /* ----- load the movie bytes ------------------------------------------- */
    uint8_t *movie_buf = NULL;
    size_t movie_size = 0;
    if (iso) {
        char name[64], path[128];
        size_t k = 0;
        for (const char *p = movie; *p && k < sizeof(name) - 1; p++)
            if (*p != '.')
                name[k++] = (char)toupper((unsigned char)*p);
            else
                break;
        name[k] = 0;
        snprintf(path, sizeof(path), "LBA/FLA/%s.FLA", name);
        if (iso_read(iso, path, &movie_buf, &movie_size) != 0) {
            fprintf(stderr, "flade: '%s' not found in image (try --list)\n", path);
            iso_close(iso);
            return 1;
        }
    } else {
        movie_buf = read_file(movie, &movie_size);
        if (!movie_buf) {
            fprintf(stderr, "flade: cannot open '%s'\n", movie);
            return 1;
        }
    }

    movie_t mv;
    if (movie_open(&mv, movie_buf, movie_size, movie) != 0) {
        fprintf(stderr, "flade: '%s' is not a movie I can play\n", movie);
        free(movie_buf);
        if (iso)
            iso_close(iso);
        return 1;
    }
    printf("flade: %s  %dx%d  %d frames  %.0f fps\n", movie, mv.width, mv.height,
           mv.num_frames, mv.fps);

    /* Transitional: FLA sound effects still come from the cue arrays. */
    fla_t *cues = fla_from_movie(&mv);

    /* ----- SDL setup ------------------------------------------------------ */
    Uint32 init_flags = SDL_INIT_VIDEO | (no_audio ? 0 : SDL_INIT_AUDIO);
    if (!SDL_Init(init_flags)) {
        fprintf(stderr, "flade: SDL_Init failed: %s\n", SDL_GetError());
        mv.close(&mv);
        free(movie_buf);
        if (iso)
            iso_close(iso);
        return 1;
    }

    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    if (!SDL_CreateWindowAndRenderer("flade", mv.width * scale, mv.height * scale,
                                     SDL_WINDOW_RESIZABLE, &win, &ren)) {
        fprintf(stderr, "flade: window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        mv.close(&mv);
        free(movie_buf);
        if (iso)
            iso_close(iso);
        return 1;
    }
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, mv.width, mv.height);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    /* ----- audio + sample bank (FLA cue path; entirely optional) ---------- */
    sample_bank bank;
    int have_audio = 0;
    memset(&bank, 0, sizeof(bank));
    if (!no_audio && cues) {
        uint8_t *hqr = NULL;
        size_t hqr_size = 0;
        int explicit_req = 0;
        if (iso) {
            iso_read(iso, "LBA/FLA/FLASAMP.HQR", &hqr, &hqr_size);
        } else if (flasamp_path) {
            explicit_req = 1;
            hqr = read_file(flasamp_path, &hqr_size);
        } else {
            char fp[1024];
            sibling_flasamp(movie, fp, sizeof(fp));
            hqr = read_file(fp, &hqr_size);
        }

        if (hqr && bank_init(&bank, hqr, hqr_size) == 0) {
            if (audio_init(volume) == 0) {
                have_audio = 1;
            } else {
                fprintf(stderr, "flade: no audio device; playing video only\n");
                free(bank.slots);
                free(bank.hqr);
                bank.slots = NULL;
            }
        } else {
            /* No sample bank is a normal mode, not an error - samples live in
             * FLASAMP.HQR, not the movie. Only speak up if one was asked for. */
            free(hqr);
            if (explicit_req)
                fprintf(stderr, "flade: '%s' not usable; playing video only\n", flasamp_path);
        }
    }
    if (!no_audio && !have_audio)
        printf("flade: no samples - playing video only\n");

    /* ----- playback loop -------------------------------------------------- */
    Uint64 next = SDL_GetTicksNS();
    int quit = 0;
    movie_frame fr;

    while (!quit && mv.step(&mv, &fr)) {
        /* FLA sound-effect cues */
        if (have_audio && cues) {
            for (int i = 0; i < cues->n_stops; i++) {
                if (cues->stops[i] < 0)
                    audio_stop_all();
                else
                    audio_stop(cues->stops[i]);
            }
            for (int i = 0; i < cues->n_plays; i++) {
                fla_sample_play *s = &cues->plays[i];
                const voc_t *v = bank_get(&bank, s->num);
                if (!v)
                    continue;
                float gl = 1.0f, gr = 1.0f;
                if (s->balance_l || s->balance_r) {
                    gl = s->balance_l / 63.0f;
                    gr = s->balance_r / 63.0f;
                    if (gl > 1.0f) gl = 1.0f;
                    if (gr > 1.0f) gr = 1.0f;
                }
                audio_play(s->num, v->pcm, v->frames, v->rate, s->repeat, gl, gr);
            }
        }

        /* palette -> RGBA (the decoder hands us a display-ready palette) */
        uint32_t lut[256];
        for (int c = 0; c < 256; c++) {
            uint8_t r = fr.palette[c * 3 + 0];
            uint8_t g = fr.palette[c * 3 + 1];
            uint8_t b = fr.palette[c * 3 + 2];
            lut[c] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }

        void *pixels;
        int pitch;
        if (SDL_LockTexture(tex, NULL, &pixels, &pitch)) {
            for (int y = 0; y < mv.height; y++) {
                uint32_t *row = (uint32_t *)((uint8_t *)pixels + (size_t)y * pitch);
                const uint8_t *src = fr.pixels + (size_t)y * mv.width;
                for (int x = 0; x < mv.width; x++)
                    row[x] = lut[src[x]];
            }
            SDL_UnlockTexture(tex);
        }

        /* present, letterboxed to the movie's aspect */
        int ow, oh;
        SDL_GetRenderOutputSize(ren, &ow, &oh);
        float aspect = (float)mv.width / (float)mv.height;
        SDL_FRect dst;
        if ((float)ow / (float)oh > aspect) {
            dst.h = (float)oh;
            dst.w = (float)oh * aspect;
        } else {
            dst.w = (float)ow;
            dst.h = (float)ow / aspect;
        }
        dst.x = ((float)ow - dst.w) * 0.5f;
        dst.y = ((float)oh - dst.h) * 0.5f;
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, &dst);
        SDL_RenderPresent(ren);

        /* input */
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT)
                quit = 1;
            else if (e.type == SDL_EVENT_KEY_DOWN) {
                SDL_Keycode k = e.key.key;
                if (k == SDLK_ESCAPE || k == SDLK_Q)
                    quit = 1;
                else if (k == SDLK_SPACE || k == SDLK_RETURN)
                    quit = 1; /* skip to end */
                else if (k == SDLK_F) {
                    bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
                    SDL_SetWindowFullscreen(win, !fs);
                }
            }
        }

        /* pace by the frame's own duration */
        next += (Uint64)(fr.duration * 1e9);
        Uint64 now = SDL_GetTicksNS();
        if (now < next)
            SDL_DelayNS(next - now);
        else
            next = now;
    }

    /* ----- teardown ------------------------------------------------------- */
    if (have_audio) {
        audio_stop_all();
        audio_shutdown();
        for (int i = 0; i < bank.count; i++)
            if (bank.slots[i].ok)
                voc_free(&bank.slots[i].voc);
        free(bank.slots);
        free(bank.hqr);
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    mv.close(&mv);
    free(movie_buf);
    if (iso)
        iso_close(iso);
    return 0;
}
