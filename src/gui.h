/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - the windowed front end. Kept free of Nuklear types so the rest of the
 * player never sees the GUI toolkit. The browser and the player share one
 * window: main creates it, calls gui_init once, then alternates gui_browse()
 * with the player on the same window. */
#ifndef FLADE_GUI_H
#define FLADE_GUI_H

#include <SDL3/SDL.h>

/* What the start screen produced. `movie` is the argument to play (a loose path,
 * or a movie name / in-image path within `cd_path`); NULL means the user closed
 * the window. `video_index` is the entry when `movie` is a loose movie-HQR,
 * else -1. Strings (when set) are malloc'd and owned by the caller. */
typedef struct {
    char *cd_path;
    char *movie;
    int video_index;
} gui_choice;

/* Set up Nuklear on a main-owned window/renderer (call once). */
void gui_init(SDL_Window *win, SDL_Renderer *ren);
void gui_shutdown(void);

/* Run the browse screen until the user picks a movie or closes the window.
 * `initial` (or NULL) is a disc/HQR to open immediately - pass the last disc to
 * return to its list after a movie. */
gui_choice gui_browse(const char *initial);

#endif /* FLADE_GUI_H */
