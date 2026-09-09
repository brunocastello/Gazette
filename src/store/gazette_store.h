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

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_STORE_H */
