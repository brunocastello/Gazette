/*
 * Gazette — Core C seam
 * Copyright (c) 2026 brunocastello
 *
 * No Toolbox calls here. Parsing and serialising are the portable prefs
 * module's job; reading and writing the file is the store's. This file just
 * owns the one live GazettePrefs and hands it out.
 */

#include "gazette_core.h"

#include "store/gazette_store.h"

#include <string.h>

/*
 * File-scope, not heap. A GazettePrefs is a fixed ~40 KB and the application
 * has exactly one, so making it static costs nothing at run time, keeps it
 * out of the 8 MB heap that streaming feed data will want, and means prefs
 * loading has no allocation that could fail. Constraint 6 forbids non-trivial
 * static constructors, not static PODs; this is zero-initialised by the
 * loader like any other C global.
 */
static GazettePrefs gPrefs;
static Boolean      gInited      = false;
static Boolean      gPrefsDirty  = false;

Boolean GazetteCoreInit(void)
{
    static char text[kGazettePrefsTextMax];
    long        len   = 0;
    Boolean     found;

    memset(&gPrefs, 0, sizeof gPrefs);

    found = GazetteStoreReadPrefs(text, (long)sizeof text, &len) ? true : false;

    /* GazettePrefsParse applies the defaults first either way, so a missing
       or unreadable file leaves a perfectly usable configuration behind. */
    GazettePrefsParse(found ? text : NULL, found ? (size_t)len : 0, &gPrefs);

    gInited     = true;
    /* A first run has nothing on disk yet; writing the defaults out at once
       gives the user a file to edit instead of one they have to invent. */
    gPrefsDirty = !found;

    return found;
}

Boolean GazetteCoreSavePrefs(void)
{
    static char text[kGazettePrefsTextMax];
    size_t      len;

    if (!gInited) {
        return false;
    }

    len = GazettePrefsSerialize(&gPrefs, text, sizeof text);
    if (len == 0) {
        return false;
    }

    if (!GazetteStoreWritePrefs(text, (long)len)) {
        return false;
    }

    gPrefsDirty = false;
    return true;
}

void GazetteCoreShutdown(void)
{
    if (gInited && gPrefsDirty) {
        (void)GazetteCoreSavePrefs();
    }
    gInited = false;
}

const GazettePrefs *GazetteCoreGetPrefs(void)
{
    return gInited ? &gPrefs : NULL;
}

int GazetteCoreFeedCount(void)
{
    return gInited ? gPrefs.feedCount : 0;
}

const char *GazetteCoreFeedTitle(int index)
{
    if (!gInited || index < 0 || index >= gPrefs.feedCount) {
        return "";
    }
    return gPrefs.feeds[index].title;
}

const char *GazetteCoreFeedURL(int index)
{
    if (!gInited || index < 0 || index >= gPrefs.feedCount) {
        return "";
    }
    return gPrefs.feeds[index].url;
}

int GazetteCoreFeedGroup(int index)
{
    if (!gInited || index < 0 || index >= gPrefs.feedCount) {
        return -1;
    }
    return gPrefs.feeds[index].group;
}

Boolean GazetteCoreFeedEnabled(int index)
{
    if (!gInited || index < 0 || index >= gPrefs.feedCount) {
        return false;
    }
    return gPrefs.feeds[index].enabled ? true : false;
}

int GazetteCoreGroupCount(void)
{
    return gInited ? gPrefs.groupCount : 0;
}

const char *GazetteCoreGroupName(int index)
{
    if (!gInited || index < 0 || index >= gPrefs.groupCount) {
        return "";
    }
    return gPrefs.groups[index].name;
}

Boolean GazetteCoreGroupCollapsed(int index)
{
    if (!gInited || index < 0 || index >= gPrefs.groupCount) {
        return false;
    }
    return gPrefs.groups[index].collapsed ? true : false;
}

int GazetteCoreGroupFeedCount(int index)
{
    return gInited ? GazettePrefsGroupFeedCount(&gPrefs, index) : 0;
}

/* ------------------------------------------------------------------ */
/* Mutators                                                            */
/* ------------------------------------------------------------------ */

int GazetteCoreAddFeed(const char *url, const char *title, int group)
{
    int index;

    if (!gInited) {
        return -1;
    }
    index = GazettePrefsAddFeed(&gPrefs, url, title, group);
    if (index >= 0) {
        gPrefsDirty = true;
    }
    return index;
}

Boolean GazetteCoreRemoveFeed(const char *url)
{
    if (!gInited || !GazettePrefsRemoveFeed(&gPrefs, url)) {
        return false;
    }
    gPrefsDirty = true;
    return true;
}

Boolean GazetteCoreRenameFeed(int index, const char *title)
{
    if (!gInited || !GazettePrefsRenameFeed(&gPrefs, index, title)) {
        return false;
    }
    gPrefsDirty = true;
    return true;
}

Boolean GazetteCoreSetFeedEnabled(int index, Boolean enabled)
{
    if (!gInited ||
        !GazettePrefsSetFeedEnabled(&gPrefs, index, enabled ? 1 : 0)) {
        return false;
    }
    gPrefsDirty = true;
    return true;
}

Boolean GazetteCoreSetFeedURL(int index, const char *url)
{
    if (!gInited || !GazettePrefsSetFeedURL(&gPrefs, index, url)) {
        return false;
    }
    gPrefsDirty = true;
    return true;
}

int GazetteCoreMoveFeed(int from, int to, int group)
{
    int index;

    if (!gInited) {
        return -1;
    }
    index = GazettePrefsMoveFeed(&gPrefs, from, to, group);
    if (index >= 0) {
        gPrefsDirty = true;
    }
    return index;
}

int GazetteCoreAddGroup(const char *name)
{
    int index;

    if (!gInited) {
        return -1;
    }
    index = GazettePrefsAddGroup(&gPrefs, name);
    if (index >= 0) {
        gPrefsDirty = true;
    }
    return index;
}

Boolean GazetteCoreRemoveGroup(int index)
{
    if (!gInited || !GazettePrefsRemoveGroup(&gPrefs, index)) {
        return false;
    }
    gPrefsDirty = true;
    return true;
}

Boolean GazetteCoreRenameGroup(int index, const char *name)
{
    if (!gInited || !GazettePrefsRenameGroup(&gPrefs, index, name)) {
        return false;
    }
    gPrefsDirty = true;
    return true;
}

int GazetteCoreMoveGroup(int from, int to)
{
    int index;

    if (!gInited) {
        return -1;
    }
    index = GazettePrefsMoveGroup(&gPrefs, from, to);
    if (index >= 0) {
        gPrefsDirty = true;
    }
    return index;
}

void GazetteCoreSetGroupCollapsed(int index, Boolean collapsed)
{
    if (!gInited || index < 0 || index >= gPrefs.groupCount) {
        return;
    }
    if (gPrefs.groups[index].collapsed == (collapsed ? 1 : 0)) {
        return;
    }
    gPrefs.groups[index].collapsed = collapsed ? 1 : 0;
    gPrefsDirty = true;
}

/* ------------------------------------------------------------------ */
/* The sidebar's rows                                                  */
/* ------------------------------------------------------------------ */

int GazetteCoreSidebarRowCount(void)
{
    return gInited ? GazettePrefsRowCount(&gPrefs) : 0;
}

Boolean GazetteCoreSidebarRowAt(int row, GazetteSidebarRow *out)
{
    if (!gInited) {
        return false;
    }
    return GazettePrefsRowAt(&gPrefs, row, out) ? true : false;
}

int GazetteCoreSidebarRowForFeed(int feed)
{
    return gInited ? GazettePrefsRowForFeed(&gPrefs, feed) : -1;
}

int GazetteCoreSidebarRowForGroup(int group)
{
    return gInited ? GazettePrefsRowForGroup(&gPrefs, group) : -1;
}
