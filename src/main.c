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
#include "player.h"
#include "fla.h"
#include "midi.h"
#include "smk.h"
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

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
    return *a == *b;
}

static int is_movie_path(const char *path) {
    size_t n = strlen(path);
    return n >= 4 && (ieq(path + n - 4, ".FLA") || ieq(path + n - 4, ".ACF"));
}

static void list_walk_cb(void *ud, const char *path, uint32_t size) {
    (void)ud;
    if (is_movie_path(path))
        printf("  %-34s %9u bytes\n", path, size);
}

/* Resolve a bare movie name to its full in-image path. */
typedef struct {
    const char *want;
    char found[1024];
} find_ctx;

static void find_walk_cb(void *ud, const char *path, uint32_t size) {
    (void)size;
    find_ctx *f = (find_ctx *)ud;
    if (f->found[0] || !is_movie_path(path))
        return;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char stem[256];
    size_t bn = strlen(base);
    size_t sl = bn >= 4 ? bn - 4 : bn; /* basename without the .FLA/.ACF */
    if (sl >= sizeof(stem))
        sl = sizeof(stem) - 1;
    memcpy(stem, base, sl);
    stem[sl] = 0;
    if (ieq(base, f->want) || ieq(stem, f->want))
        snprintf(f->found, sizeof(f->found), "%s", path);
}

/* Find a file by exact basename anywhere in the image (case-insensitive). */
typedef struct {
    const char *want;
    char found[1024];
} base_ctx;

static void base_walk_cb(void *ud, const char *path, uint32_t size) {
    (void)size;
    base_ctx *b = (base_ctx *)ud;
    if (b->found[0])
        return;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (ieq(base, b->want))
        snprintf(b->found, sizeof(b->found), "%s", path);
}

/* path (sans leading '/') of the first file with this basename, or "" */
static void iso_find_basename(iso9660_t *iso, const char *basename, char *out, size_t cap) {
    base_ctx b;
    b.want = basename;
    b.found[0] = 0;
    iso_walk(iso, base_walk_cb, &b);
    snprintf(out, cap, "%s", b.found[0] == '/' ? b.found + 1 : b.found);
}

/* Read the LBA2 cutscene catalogue (RESS.HQR entry 48 = the ACF name list):
 * names in VIDEO.HQR entry order. Returns the count. */
#define MAX_VIDEO_NAMES 128
#define RESS_ACFLIST 48
static int lba2_video_names(iso9660_t *iso, char names[][32]) {
    char ress[1024];
    iso_find_basename(iso, "RESS.HQR", ress, sizeof(ress));
    if (!ress[0])
        return 0;
    uint8_t *rh = NULL;
    size_t rsz = 0;
    if (iso_read(iso, ress, &rh, &rsz) != 0)
        return 0;
    int count = 0;
    uint8_t *lst = NULL;
    size_t lsz = 0;
    if (hqr_entry(rh, rsz, RESS_ACFLIST, &lst, &lsz) == 0) {
        size_t i = 0;
        while (i < lsz && count < MAX_VIDEO_NAMES) {
            while (i < lsz && lst[i] <= 32)
                i++; /* skip whitespace */
            if (i >= lsz || lst[i] == 26)
                break; /* 0x1A terminates */
            int k = 0;
            while (i < lsz && lst[i] > 32 && k < 31)
                names[count][k++] = (char)lst[i++];
            names[count][k] = 0;
            count++;
        }
        free(lst);
    }
    free(rh);
    return count;
}

static void usage(void) {
    printf("flade - Adeline movie player (FLA + ACF)\n\n"
           "Usage:\n"
           "  flade <movie.fla|.acf> [options]\n"
           "  flade --cd <image> <name | /path/in/image> [options]\n"
           "  flade --cd <image> --list\n\n"
           "Reads loose movie files or a raw MODE1/2352 CD image (LBA1 LBA.DOT,\n"
           "Time Commando GAME.GOG, ...). With --cd, give a full in-image path such\n"
           "as /SEQUENCE/BIGINTRO.ACF, or a bare name like INTROD to search the disc.\n\n"
           "Options:\n"
           "  --cd <image>     read the movie (and FLA samples) from a CD image\n"
           "  --flasamp <file> FLASAMP.HQR for a loose FLA (default: alongside it)\n"
           "  --midi <file>    MIDI_MI.HQR for FLA cutscene music (default: from --cd)\n"
           "  --soundfont <f>  .sf2 for MIDI (default: a system soundfont)\n"
           "  --index <n>      play entry n when the input is an HQR (LBA2 VIDEO.HQR)\n"
           "  --voice <n>      Smacker voice track to mix (1..3 = FR/DE/EN; default first)\n"
           "  --list           with --cd, list the movies (.fla/.acf) in the image\n"
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
    const char *midi_path = NULL;
    const char *soundfont_path = NULL;
    int do_list = 0, no_audio = 0, scale = 3;
    int video_index = 0; /* entry to play when the input is an HQR of movies */
    float volume = 0.7f;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--cd") && i + 1 < argc)
            cd_path = argv[++i];
        else if (!strcmp(a, "--flasamp") && i + 1 < argc)
            flasamp_path = argv[++i];
        else if (!strcmp(a, "--midi") && i + 1 < argc)
            midi_path = argv[++i];
        else if (!strcmp(a, "--soundfont") && i + 1 < argc)
            soundfont_path = argv[++i];
        else if (!strcmp(a, "--index") && i + 1 < argc)
            video_index = atoi(argv[++i]);
        else if (!strcmp(a, "--voice") && i + 1 < argc)
            smk_set_voice(atoi(argv[++i])); /* SMK voice track (1..3 = FR/DE/EN) */
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
            printf("Movies in %s:\n", cd_path);
            iso_walk(iso, list_walk_cb, NULL); /* loose .fla / .acf */
            /* LBA2 Smacker cinematics (VIDEO.HQR + the RESS.HQR name list) */
            char names[MAX_VIDEO_NAMES][32];
            int nv = lba2_video_names(iso, names);
            char vpath[1024];
            iso_find_basename(iso, "VIDEO.HQR", vpath, sizeof(vpath));
            if (nv > 0 && vpath[0]) {
                printf("  Smacker cinematics in /%s (play by name, or --index):\n", vpath);
                for (int i = 0; i < nv; i++)
                    printf("    [%2d]  %s\n", i, names[i]);
            }
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
    char flasamp_in_image[1024] = {0}; /* FLASAMP.HQR beside an in-image FLA */
    char midi_in_image[1024] = {0};    /* MIDI_MI.HQR one level up from it */
    if (iso) {
        char inpath[1024];
        if (movie[0] == '/') {
            snprintf(inpath, sizeof(inpath), "%s", movie + 1); /* strip leading '/' */
        } else {
            find_ctx fc;
            fc.want = movie;
            fc.found[0] = 0;
            iso_walk(iso, find_walk_cb, &fc);
            if (fc.found[0]) {
                snprintf(inpath, sizeof(inpath), "%s", fc.found + 1); /* loose .fla/.acf */
            } else {
                /* Try the LBA2 cinematic catalogue: name -> VIDEO.HQR entry. */
                char names[MAX_VIDEO_NAMES][32];
                int nv = lba2_video_names(iso, names);
                int vidx = -1;
                for (int i = 0; i < nv; i++) {
                    char stem[32];
                    snprintf(stem, sizeof(stem), "%s", names[i]);
                    char *dot = strrchr(stem, '.');
                    if (dot)
                        *dot = 0;
                    if (ieq(names[i], movie) || ieq(stem, movie)) {
                        vidx = i;
                        break;
                    }
                }
                char vpath[1024];
                iso_find_basename(iso, "VIDEO.HQR", vpath, sizeof(vpath));
                if (vidx >= 0 && vpath[0]) {
                    snprintf(inpath, sizeof(inpath), "%s", vpath);
                    video_index = vidx; /* the HQR-container path plays this entry */
                } else {
                    fprintf(stderr, "flade: no movie matching '%s' in image (try --list)\n", movie);
                    iso_close(iso);
                    return 1;
                }
            }
        }
        if (iso_read(iso, inpath, &movie_buf, &movie_size) != 0) {
            fprintf(stderr, "flade: '/%s' not found in image\n", inpath);
            iso_close(iso);
            return 1;
        }
        /* FLASAMP.HQR (FLA samples) lives in the same directory as the movie */
        const char *sl = strrchr(inpath, '/');
        size_t dl = sl ? (size_t)(sl - inpath) + 1 : 0;
        snprintf(flasamp_in_image, sizeof(flasamp_in_image), "%.*sFLASAMP.HQR", (int)dl, inpath);
        /* MIDI_MI.HQR sits one directory up (e.g. LBA/MIDI_MI.HQR for LBA/FLA/X.FLA) */
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", inpath);
        char *s1 = strrchr(tmp, '/');
        if (s1) {
            *s1 = 0;
            char *s2 = strrchr(tmp, '/');
            if (s2)
                s2[1] = 0;
            else
                tmp[0] = 0;
        } else {
            tmp[0] = 0;
        }
        snprintf(midi_in_image, sizeof(midi_in_image), "%sMIDI_MI.HQR", tmp);
    } else {
        movie_buf = read_file(movie, &movie_size);
        if (!movie_buf) {
            fprintf(stderr, "flade: cannot open '%s'\n", movie);
            return 1;
        }
    }

    movie_t mv;
    if (movie_open(&mv, movie_buf, movie_size, movie) != 0) {
        /* Not a bare movie - maybe an HQR container of movies (LBA2's
         * VIDEO.HQR holds one .smk per entry). Play entry `video_index`. */
        uint8_t *entry = NULL;
        size_t esize = 0;
        if (hqr_count(movie_buf, movie_size) > 0 &&
            hqr_entry(movie_buf, movie_size, video_index, &entry, &esize) == 0 &&
            movie_open(&mv, entry, esize, movie) == 0) {
            free(movie_buf); /* the movie now references the decoded entry */
            movie_buf = entry;
            movie_size = esize;
        } else {
            free(entry);
            fprintf(stderr, "flade: '%s' is not a movie I can play\n", movie);
            free(movie_buf);
            if (iso)
                iso_close(iso);
            return 1;
        }
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

    /* ----- audio (FLA cue mixer or ACF streaming track; optional) --------- */
    sample_bank bank;
    int have_audio = 0;  /* FLA cue mixer ready */
    int have_stream = 0; /* ACF streaming track available */
    memset(&bank, 0, sizeof(bank));
    if (!no_audio && !cues && mv.audio_pcm && mv.audio_frames > 0)
        have_stream = 1; /* SDL audio is up; the stream opens on demand */
    if (!no_audio && cues) {
        uint8_t *hqr = NULL;
        size_t hqr_size = 0;
        int explicit_req = 0;
        if (iso) {
            iso_read(iso, flasamp_in_image, &hqr, &hqr_size);
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
    if (!no_audio && !have_audio && !have_stream)
        printf("flade: no audio track - playing video only\n");

    /* ----- FLA cutscene MIDI (the flute, XMI track 26) -------------------- *
     * Only some FLAs use it (FLUTE2/GLASS2). Needs MIDI_MI.HQR (in the CD
     * image, or --midi) and a soundfont (--soundfont or a system .sf2). The
     * rendered track is pre-mixed once and played on the streaming channel. */
    int16_t *midi_pcm = NULL;
    size_t midi_frames = 0;
    int midi_rate = 44100;
    int have_midi = 0;
    if (!no_audio && cues) {
        uint8_t *mh = NULL;
        size_t mh_sz = 0;
        if (midi_path)
            mh = read_file(midi_path, &mh_sz);
        else if (iso && midi_in_image[0])
            iso_read(iso, midi_in_image, &mh, &mh_sz);
        /* (loose FLAs without --midi: skipped - e.g. Steam ships MP3s, no HQR) */
        if (mh && hqr_count(mh, mh_sz) > 26 && midi_init(soundfont_path) == 0) {
            uint8_t *xmi = NULL;
            size_t xmi_sz = 0;
            if (hqr_entry(mh, mh_sz, 26, &xmi, &xmi_sz) == 0) {
                if (midi_render_xmi(xmi, xmi_sz, midi_rate, &midi_pcm, &midi_frames) == 0)
                    have_midi = 1;
                free(xmi);
            }
        } else if (mh && !midi_available()) {
            fprintf(stderr, "flade: found MIDI_MI.HQR but no soundfont; "
                            "pass --soundfont <.sf2> for cutscene music\n");
        }
        free(mh);
    }

    /* ----- playback loop (cached, so rewind / scrub are instant) ---------- */
    player_t *pl = player_open(&mv);
    if (!pl)
        fprintf(stderr, "flade: out of memory\n");

    double pos = 0.0;   /* current frame, fractional */
    double speed = 1.0; /* playback rate multiplier */
    int dir = 1;        /* +1 forward, -1 reverse */
    int paused = 0;
    int quit = 0;
    int have_frame = 0;
    int was_audio_active = 0; /* ACF stream was playing last iteration */
    int seeked = 0;           /* a key jumped the playhead this frame */
    int midi_playing = 0;     /* FLA cutscene MIDI is sounding */
    int midi_fading = 0;
    float midi_gain = 1.0f;
    movie_frame fr;
    Uint64 prev = SDL_GetTicksNS();

    while (!quit && pl) {
        Uint64 now = SDL_GetTicksNS();
        double dt = (double)(now - prev) / 1e9;
        prev = now;
        if (dt > 0.25)
            dt = 0.25; /* don't lurch after a stall */

        if (!paused)
            pos += (double)dir * speed * dt * mv.fps;
        if (pos < 0.0) {
            pos = 0.0;
            paused = 1;
        }

        int idx = (int)pos;
        int before = player_count(pl);
        if (player_get(pl, idx, &fr)) {
            have_frame = 1;
            /* fire FLA sound-effect cues only for a frame freshly decoded
             * during normal forward play (rewind / scrub / FF stay silent) */
            int fresh = player_count(pl) == before + 1 && idx == player_count(pl) - 1;
            int normal_fwd = dir > 0 && !paused && speed > 0.99 && speed < 1.01;
            if (have_audio && cues && fresh && normal_fwd) {
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
            /* FLA cutscene MIDI cues (FLUTE2/GLASS2): play / fade the flute */
            if (have_midi && fresh && normal_fwd) {
                if (cues->midi_play) {
                    audio_stream_start(midi_pcm, midi_frames, midi_rate, 2, 0, volume);
                    midi_playing = 1;
                    midi_fading = 0;
                    midi_gain = 1.0f;
                }
                if (cues->midi_fade && midi_playing)
                    midi_fading = 1;
            }
        } else if (player_complete(pl) && player_count(pl) > 0) {
            /* reached the end: hold on the last frame (rewind still works) */
            idx = player_count(pl) - 1;
            pos = (double)idx;
            paused = 1;
            have_frame = player_get(pl, idx, &fr);
        } else if (!have_frame) {
            fprintf(stderr, "flade: no frames decoded\n");
            break;
        }

        /* ACF streaming audio rides alongside the wall-clock video (the track
         * matches the video's duration). Play only in normal forward; pause
         * when paused; mute (and re-sync on return) for reverse / FF / scrub. */
        if (have_stream) {
            int audio_active = dir > 0 && speed > 0.99 && speed < 1.01;
            if (audio_active) {
                if (!was_audio_active || seeked) {
                    size_t sf = (size_t)((double)idx / mv.fps * (double)mv.audio_rate);
                    if (audio_stream_start(mv.audio_pcm, mv.audio_frames, mv.audio_rate,
                                           mv.audio_channels, sf, volume) != 0)
                        have_stream = 0; /* no device - drop to silent */
                }
                audio_stream_set_paused(paused);
            } else if (was_audio_active) {
                audio_stream_stop();
            }
            was_audio_active = audio_active;
        }

        /* FLA MIDI transport: mute on any trick mode, ramp the fade, follow pause */
        if (midi_playing) {
            int trick = dir <= 0 || speed < 0.99 || speed > 1.01 || seeked;
            if (trick) {
                audio_stream_stop();
                midi_playing = 0;
                midi_fading = 0;
            } else if (midi_fading) {
                midi_gain -= (float)dt; /* ~1s linear fade */
                if (midi_gain <= 0.0f) {
                    audio_stream_stop();
                    midi_playing = 0;
                    midi_fading = 0;
                } else {
                    audio_stream_set_gain(midi_gain);
                }
            }
            if (midi_playing)
                audio_stream_set_paused(paused);
        }
        seeked = 0;

        if (have_frame) {
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

        /* input / transport */
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                quit = 1;
            } else if (e.type == SDL_EVENT_KEY_DOWN) {
                switch (e.key.key) {
                case SDLK_ESCAPE:
                case SDLK_Q:
                case SDLK_RETURN:
                    quit = 1;
                    break;
                case SDLK_SPACE: /* pause / play */
                    paused = !paused;
                    if (paused)
                        audio_stop_all();
                    break;
                case SDLK_R: /* reverse direction */
                    dir = -dir;
                    audio_stop_all();
                    break;
                case SDLK_LEFT: /* seek back ~5s */
                    pos -= 5.0 * mv.fps;
                    if (pos < 0.0)
                        pos = 0.0;
                    audio_stop_all();
                    seeked = 1;
                    break;
                case SDLK_RIGHT: /* seek forward ~5s */
                    pos += 5.0 * mv.fps;
                    audio_stop_all();
                    seeked = 1;
                    break;
                case SDLK_COMMA: /* step one frame back */
                    paused = 1;
                    pos -= 1.0;
                    if (pos < 0.0)
                        pos = 0.0;
                    seeked = 1;
                    break;
                case SDLK_PERIOD: /* step one frame forward */
                    paused = 1;
                    pos += 1.0;
                    seeked = 1;
                    break;
                case SDLK_MINUS: /* slower */
                    speed *= 0.5;
                    if (speed < 0.125)
                        speed = 0.125;
                    break;
                case SDLK_EQUALS:
                case SDLK_PLUS: /* faster */
                    speed *= 2.0;
                    if (speed > 8.0)
                        speed = 8.0;
                    break;
                case SDLK_HOME:
                case SDLK_BACKSPACE: /* restart */
                    pos = 0.0;
                    dir = 1;
                    audio_stop_all();
                    seeked = 1;
                    break;
                case SDLK_F: {
                    bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
                    SDL_SetWindowFullscreen(win, !fs);
                    break;
                }
                default:
                    break;
                }
            }
        }

        SDL_DelayNS(6 * 1000000); /* ~165 Hz: responsive input, low CPU */
    }
    player_close(pl);

    /* ----- teardown ------------------------------------------------------- */
    audio_stream_stop();
    midi_shutdown();
    free(midi_pcm);
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
