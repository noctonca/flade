/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 the flade authors */
/* flade - the windowed front end. Kept free of Nuklear types so the rest of the
 * player never sees the GUI toolkit. */
#ifndef FLADE_GUI_H
#define FLADE_GUI_H

/* Run the start screen: open a window and let the user pick a movie file (or a
 * CD image / HQR) to play. Returns a malloc'd path the caller should play and
 * free, or NULL if the user closed the window without choosing. */
char *gui_pick_movie(void);

#endif /* FLADE_GUI_H */
