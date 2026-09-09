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

Boolean GazetteCoreAddFeed(const char *url, const char *title)
{
    if (!gInited || !GazettePrefsAddFeed(&gPrefs, url, title)) {
        return false;
    }
    gPrefsDirty = true;
    return true;
}

Boolean GazetteCoreRemoveFeed(const char *url)
{
    if (!gInited || !GazettePrefsRemoveFeed(&gPrefs, url)) {
        return false;
    }
    gPrefsDirty = true;
    return true;
}
