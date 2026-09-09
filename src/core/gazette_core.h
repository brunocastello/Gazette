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

/* ------------------------------------------------------------------ */
/* The sidebar's tree                                                  */
/*                                                                     */
/* Groups and feeds are the user's arrangement, not a built-in one.     */
/* Feeds are held in sidebar order — the top-level ones, then each      */
/* group's in turn — so the window, the file and a drag all read the    */
/* same sequence and none of them re-derives it.                        */
/* ------------------------------------------------------------------ */

int         GazetteCoreFeedCount(void);
const char *GazetteCoreFeedTitle(int index);    /* "" when index is out of range */
const char *GazetteCoreFeedURL(int index);
int         GazetteCoreFeedGroup(int index);    /* -1 for the top level */
Boolean     GazetteCoreFeedEnabled(int index);

int         GazetteCoreGroupCount(void);
const char *GazetteCoreGroupName(int index);
Boolean     GazetteCoreGroupCollapsed(int index);
int         GazetteCoreGroupFeedCount(int index);

/*
 * The tree flattened into the lines the sidebar draws — see
 * GazettePrefsRowCount. The window indexes rows, not feeds: a closed group
 * hides its own, and a scroll bar counts what is on screen.
 */
int     GazetteCoreSidebarRowCount(void);
Boolean GazetteCoreSidebarRowAt(int row, GazetteSidebarRow *out);
int     GazetteCoreSidebarRowForFeed(int feed);
int     GazetteCoreSidebarRowForGroup(int group);

/* Every mutator marks the preferences dirty, so the next save writes them and
   quitting is enough to make a change permanent. */

/* Returns the new feed's index, or -1. group is -1 for the top level. */
int     GazetteCoreAddFeed(const char *url, const char *title, int group);
Boolean GazetteCoreRemoveFeed(const char *url);
Boolean GazetteCoreRenameFeed(int index, const char *title);

/* Switch a feed off or on. An off feed keeps its place in the sidebar and its
   line in the preferences file; a refresh simply skips it. */
Boolean GazetteCoreSetFeedEnabled(int index, Boolean enabled);

/* Change a feed's address, keeping its place, its name and its group. False
   when another feed already has that address. */
Boolean GazetteCoreSetFeedURL(int index, const char *url);

/* Move a feed to a position and into a group. Returns its new index, or -1.
   This is what a drag in the sidebar lands on. */
int     GazetteCoreMoveFeed(int from, int to, int group);

/* Returns the new group's index, or -1. */
int     GazetteCoreAddGroup(const char *name);

/* Removing a group moves its feeds to the top level rather than deleting
   them: a folder should never silently take subscriptions with it. */
Boolean GazetteCoreRemoveGroup(int index);
Boolean GazetteCoreRenameGroup(int index, const char *name);
int     GazetteCoreMoveGroup(int from, int to);
void    GazetteCoreSetGroupCollapsed(int index, Boolean collapsed);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_CORE_H */
