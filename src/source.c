/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
#include "source.h"
#include "hqr.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir((p), 0755)
#endif

/* ----- file / string helpers ---------------------------------------------- */

uint8_t *read_file(const char *path, size_t *size) {
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

static int write_file(const char *path, const uint8_t *data, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int ok = fwrite(data, 1, n, f) == n;
    fclose(f);
    return ok ? 0 : -1;
}

/* Create a directory and any missing parents. */
static void ensure_dir(const char *path) {
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *s = tmp + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            MKDIR(tmp);
            *s = '/';
        }
    MKDIR(tmp);
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

/* ----- in-image lookup ----------------------------------------------------- */

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

void source_list_movies(iso9660_t *iso, const char *label) {
    printf("Movies in %s:\n", label);
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
}

int source_resolve(iso9660_t *iso, const char *name, char *inpath, size_t cap, int *video_index) {
    find_ctx fc;
    fc.want = name;
    fc.found[0] = 0;
    iso_walk(iso, find_walk_cb, &fc);
    if (fc.found[0]) {
        snprintf(inpath, cap, "%s", fc.found + 1); /* loose .fla/.acf */
        return 0;
    }
    /* Try the LBA2 cinematic catalogue: name -> VIDEO.HQR entry. */
    char names[MAX_VIDEO_NAMES][32];
    int nv = lba2_video_names(iso, names);
    for (int i = 0; i < nv; i++) {
        char stem[32]; /* the catalogue name without its extension */
        size_t k = 0;
        for (; k < sizeof(stem) - 1 && names[i][k] && names[i][k] != '.'; k++)
            stem[k] = names[i][k];
        stem[k] = 0;
        if (ieq(names[i], name) || ieq(stem, name)) {
            char vpath[1024];
            iso_find_basename(iso, "VIDEO.HQR", vpath, sizeof(vpath));
            if (vpath[0]) {
                snprintf(inpath, cap, "%s", vpath);
                *video_index = i;
                return 0;
            }
        }
    }
    return -1;
}

/* ----- extract: unpack the original files out of a CD image or HQR --------- */

/* Pick a file extension from the content's magic. */
static const char *sniff_ext(const uint8_t *d, size_t n) {
    if (n >= 4 && (memcmp(d, "SMK2", 4) == 0 || memcmp(d, "SMK4", 4) == 0))
        return "smk";
    if (n >= 4 && memcmp(d, "V1.3", 4) == 0)
        return "fla";
    if (n >= 8 && memcmp(d, "FrameLen", 8) == 0)
        return "acf";
    if (n >= 19 && memcmp(d, "Creative Voice File", 19) == 0)
        return "voc";
    if (hqr_count(d, n) > 0)
        return "hqr";
    return "bin";
}

/* Write every entry of an HQR buffer to outdir. With `names` (the RESS catalog)
 * each entry keeps its real name; otherwise it's entry_NNNN.<sniffed-type>. */
static int extract_hqr_buffer(const uint8_t *hqr, size_t size, const char *outdir,
                              char names[][32], int nnames) {
    int n = hqr_count(hqr, size), written = 0;
    if (n < 0)
        return 0;
    for (int i = 0; i < n; i++) {
        uint8_t *e = NULL;
        size_t es = 0;
        if (hqr_entry(hqr, size, i, &e, &es) != 0 || es == 0) {
            free(e);
            continue;
        }
        char path[1100];
        if (names && i < nnames && names[i][0])
            snprintf(path, sizeof(path), "%s/%s", outdir, names[i]);
        else
            snprintf(path, sizeof(path), "%s/entry_%04d.%s", outdir, i, sniff_ext(e, es));
        if (write_file(path, e, es) == 0) {
            printf("  %s  (%zu bytes)\n", path, es);
            written++;
        }
        free(e);
    }
    return written;
}

/* Walk a CD image, copying out every loose movie file (.fla / .acf). */
typedef struct {
    iso9660_t *iso;
    const char *outdir;
    int count;
} extract_ctx;

static void extract_walk_cb(void *ud, const char *path, uint32_t size) {
    (void)size;
    extract_ctx *e = (extract_ctx *)ud;
    if (!is_movie_path(path))
        return;
    const char *rel = path[0] == '/' ? path + 1 : path;
    uint8_t *buf = NULL;
    size_t bs = 0;
    if (iso_read(e->iso, rel, &buf, &bs) != 0)
        return;
    /* mirror the disc layout so same-named files (Time Commando's per-stage
     * SCENE.ACF) don't collide; create the parent directory chain first. */
    char out[1100];
    snprintf(out, sizeof(out), "%s/%s", e->outdir, rel);
    char *slash = strrchr(out, '/');
    if (slash) {
        *slash = 0;
        ensure_dir(out);
        *slash = '/';
    }
    if (write_file(out, buf, bs) == 0) {
        printf("  %s  (%zu bytes)\n", out, bs);
        e->count++;
    }
    free(buf);
}

/* Extract the movies from a CD image: loose .fla/.acf plus the Smacker
 * cinematics inside VIDEO.HQR (named from the RESS.HQR catalogue). */
static int extract_image(iso9660_t *iso, const char *outdir) {
    extract_ctx ctx = {iso, outdir, 0};
    iso_walk(iso, extract_walk_cb, &ctx);

    char vpath[1024];
    iso_find_basename(iso, "VIDEO.HQR", vpath, sizeof(vpath));
    if (vpath[0]) {
        uint8_t *vh = NULL;
        size_t vs = 0;
        if (iso_read(iso, vpath, &vh, &vs) == 0) {
            char names[MAX_VIDEO_NAMES][32];
            int nv = lba2_video_names(iso, names);
            ctx.count += extract_hqr_buffer(vh, vs, outdir, nv > 0 ? names : NULL, nv);
            free(vh);
        }
    }
    return ctx.count;
}

int run_extract(const char *src, const char *cd_path, const char *outdir) {
    ensure_dir(outdir);
    int n = 0;
    if (cd_path) {
        iso9660_t *img = iso_open(cd_path);
        if (!img) {
            fprintf(stderr, "flade: '%s' is not a CD image I can read\n", cd_path);
            return 1;
        }
        uint8_t *buf = NULL;
        size_t bs = 0;
        if (iso_read(img, src[0] == '/' ? src + 1 : src, &buf, &bs) != 0) {
            fprintf(stderr, "flade: '%s' not found in image\n", src);
            iso_close(img);
            return 1;
        }
        if (hqr_count(buf, bs) > 0) {
            const char *eb = strrchr(src, '/');
            eb = eb ? eb + 1 : src;
            char names[MAX_VIDEO_NAMES][32];
            int nv = ieq(eb, "VIDEO.HQR") ? lba2_video_names(img, names) : 0;
            n = extract_hqr_buffer(buf, bs, outdir, nv > 0 ? names : NULL, nv);
        } else {
            const char *base = strrchr(src, '/');
            base = base ? base + 1 : src;
            char out[1100];
            snprintf(out, sizeof(out), "%s/%s", outdir, base);
            if (write_file(out, buf, bs) == 0) {
                printf("  %s  (%zu bytes)\n", out, bs);
                n = 1;
            }
        }
        free(buf);
        iso_close(img);
    } else {
        iso9660_t *img = iso_open(src);
        if (img) {
            n = extract_image(img, outdir);
            iso_close(img);
        } else {
            size_t bs = 0;
            uint8_t *buf = read_file(src, &bs);
            if (buf && hqr_count(buf, bs) > 0) {
                n = extract_hqr_buffer(buf, bs, outdir, NULL, 0);
            } else {
                fprintf(stderr, "flade: '%s' is not a CD image or an HQR archive\n", src);
                free(buf);
                return 1;
            }
            free(buf);
        }
    }
    printf("flade: extracted %d file(s) to %s/\n", n, outdir);
    return 0;
}
