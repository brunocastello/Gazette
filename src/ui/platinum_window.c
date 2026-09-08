/*
 * Gazette — Platinum window implementation (stub for Phase 0)
 * Copyright (c) 2026 brunocastello
 */

#include "platinum_window.h"
#include <MacTypes.h>
#include <Windows.h>

WindowPtr GazetteCreatePlatinumWindow(short id, WindowPtr behind)
{
    /* Phase 0 stub: returns nil; real implementation in Phase 3 */
    (void)id;
    (void)behind;
    return nil;
}

void GazetteUpdatePlatinumWindow(WindowPtr win, const char *feedData, int len)
{
    /* Phase 0 stub */
    (void)win;
    (void)feedData;
    (void)len;
}
