/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - the windowed front end. Kept free of Nuklear types so the rest of the
 * player never sees the GUI toolkit. */
#ifndef FLADE_GUI_H
#define FLADE_GUI_H

/* What the start screen produced. `movie` is the argument to play (a loose path,
 * or a movie name / in-image path within `cd_path`); NULL means the user closed
 * the window without choosing. `cd_path` is the CD image to play from, or NULL
 * for a loose file. Both strings (when set) are malloc'd and owned by the
 * caller (they live until exit). */
typedef struct {
    char *cd_path;
    char *movie;
} gui_choice;

/* Open the start screen: pick a movie file, or open a disc and pick from its
 * list of cinematics. */
gui_choice gui_run(void);

#endif /* FLADE_GUI_H */
