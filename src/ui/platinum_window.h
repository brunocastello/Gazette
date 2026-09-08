/*
 * Gazette — Platinum window helpers (stub for Phase 0)
 * Copyright (c) 2026 brunocastello
 */

#ifndef GAZETTE_PLATINUM_WINDOW_H
#define GAZETTE_PLATINUM_WINDOW_H

#include <MacTypes.h>
#include <Windows.h>

/* Create a standard Platinum-style window with sidebar, article list, reader pane */
WindowPtr GazetteCreatePlatinumWindow(short id, WindowPtr behind);

/* Update window with feed data */
void GazetteUpdatePlatinumWindow(WindowPtr win, const char *feedData, int len);

#endif /* GAZETTE_PLATINUM_WINDOW_H */
