/*
 * Gazette — local file storage
 * Copyright (c) 2026 brunocastello
 *
 * Every File Manager call in Gazette lives behind this header. The interface
 * is plain C with no Toolbox types in it, so gazette_core.c can call into it
 * without pulling Carbon across the seam (constraint 5) — the FSSpecs, OSErrs
 * and refNums all stay inside gazette_store.c.
 *
 * Phase 0 needs exactly one file, the preferences. Phase 3 adds the feed and
 * article cache alongside it, which is why this is a storage layer rather
 * than a pair of prefs functions.
 */
#ifndef GAZETTE_STORE_H
#define GAZETTE_STORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read "Gazette Preferences" from the Preferences folder into buf.
   Returns 1 and stores the length in *outLen when the file was read; 0 when
   it does not exist yet (a first run) or could not be read, in which case
   *outLen is 0 and buf holds an empty string. Anything past cap - 1 bytes is
   dropped: a prefs file larger than that is not a prefs file. */
int GazetteStoreReadPrefs(char *buf, long cap, long *outLen);

/* Write text as the whole contents of "Gazette Preferences", creating it if
   need be. Returns 1 on success. The file is created as 'TEXT'/'Gzt9' so it
   opens in SimpleText and shows Gazette's icon in the Finder. */
int GazetteStoreWritePrefs(const char *text, long len);

/* ------------------------------------------------------------------ */
/* The article cache                                                   */
/*                                                                     */
/* One file per feed, in a "Gazette Cache" folder beside the           */
/* preferences. Named by a hash of the feed's URL rather than by its   */
/* position in the list, so reordering or renaming feeds does not      */
/* silently hand a feed someone else's articles.                       */
/*                                                                     */
/* Streamed in both directions. A feed's cache is a few hundred        */
/* kilobytes and building it in memory to write in one go would be the */
/* largest allocation in the application, which is the same reason the */
/* fetch streams (constraint 3).                                       */
/* ------------------------------------------------------------------ */

typedef struct GazetteStoreFile GazetteStoreFile;

/* Create or truncate the cache file for a feed URL and open it for writing.
   Returns NULL if the folder or the file could not be made. */
GazetteStoreFile *GazetteStoreCacheCreate(const char *feedURL);

/* Open a feed's cache file for reading, or NULL when there is none. */
GazetteStoreFile *GazetteStoreCacheOpen(const char *feedURL);

/* Append text. Returns 1 on success. */
int GazetteStoreWrite(GazetteStoreFile *f, const char *text, long len);

/* Append "<text>
". Returns 1 on success. */
int GazetteStoreWriteLine(GazetteStoreFile *f, const char *text);

/*
 * Read one line into buf, without its terminator. Returns the length, or -1
 * at end of file. A line longer than cap is truncated and the rest of it
 * discarded, so a corrupt file cannot desynchronise the reader.
 */
long GazetteStoreReadLine(GazetteStoreFile *f, char *buf, long cap);

/* Close and free. Safe with NULL. */
void GazetteStoreClose(GazetteStoreFile *f);

/* Delete a feed's cache file. Used when a feed is removed. */
void GazetteStoreCacheDelete(const char *feedURL);

/* ------------------------------------------------------------------ */
/* Named files in the same folder                                      */
/*                                                                     */
/* For what Gazette remembers that belongs to no single feed — which   */
/* articles have been read, and how many of each feed are unread. Same */
/* folder, same streaming, a name of its own rather than a hashed one  */
/* so it is recognisable in the Finder.                                */
/* ------------------------------------------------------------------ */

GazetteStoreFile *GazetteStoreDataCreate(const char *name);
GazetteStoreFile *GazetteStoreDataOpen(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_STORE_H */
