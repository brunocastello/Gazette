/*
 * Gazette — Core C seam
 * Copyright (c) 2026 brunocastello
 *
 * The one interface main.cpp and the UI use to reach the engine. It stays
 * thin and plain-typed on purpose (constraint 5): MacTypes.h is the only
 * system header it may include, and nothing behind it leaks Carbon back out.
 */

#ifndef GAZETTE_CORE_H
#define GAZETTE_CORE_H

/* MacTypes.h is the seam's one permitted system header — it is what makes
   Boolean/true/false mean the same thing on both sides. */
#include <MacTypes.h>

#include "prefs/gazette_prefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load preferences, falling back to defaults when there is no prefs file
   (a first run). Always succeeds in the sense that the engine is usable
   afterwards; the return value says whether a file was actually read, which
   is only worth knowing for status text. Call once, before the event loop. */
Boolean GazetteCoreInit(void);

/* Write preferences back out. Called from GazetteCoreShutdown, and exposed
   for Phase 4's feed-management UI, which should not have to quit to save. */
Boolean GazetteCoreSavePrefs(void);

/* Save preferences and release anything the engine holds. */
void GazetteCoreShutdown(void);

/* The live preference block. Read-only to callers; changes go through the
   mutators below so the engine always knows when it has become dirty. */
const GazettePrefs *GazetteCoreGetPrefs(void);

/* Feed list, as the sidebar will want it in Phase 3. */
int         GazetteCoreFeedCount(void);
const char *GazetteCoreFeedTitle(int index);    /* "" when index is out of range */
const char *GazetteCoreFeedURL(int index);

/* Feed list mutators. Both mark the prefs dirty so the next save writes. */
Boolean GazetteCoreAddFeed(const char *url, const char *title);
Boolean GazetteCoreRemoveFeed(const char *url);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_CORE_H */
