/*
 * Gazette — OPML import and export
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: no system headers, host-tested (constraint 5). Choosing the file
 * is the Toolbox's job and lives in store/gazette_store.h; this is only the
 * text.
 *
 * OPML is how a feed list moves between readers, and the half of it everyone
 * actually agrees on is small: <outline> elements with an xmlUrl attribute
 * are feeds, and an <outline> without one that contains others is a folder.
 * Gazette writes that and reads that, and ignores the rest of the
 * specification rather than pretending to implement it.
 *
 * Gazette's own model is one level of folders deep. A file nested deeper than
 * that is not rejected — its feeds land in the outermost folder they are
 * under, which keeps a subscription rather than losing it to a shape the
 * sidebar cannot draw.
 */
#ifndef GAZETTE_OPML_H
#define GAZETTE_OPML_H

#include <stddef.h>

#include "prefs/gazette_prefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Enough for a full feed list as OPML. An outline line is the URL and the
 * title plus about eighty bytes of attributes and indentation, so this is
 * kGazetteMaxFeeds of those with room to spare.
 */
enum { kGazetteOPMLMax = 96 * 1024 };

/* Write p's groups and feeds as an OPML document. Returns the length, or 0
   if it would not fit in cap. Always NUL-terminates when cap > 0. */
size_t GazetteOPMLWrite(const GazettePrefs *p, char *out, size_t cap);

/*
 * Read an OPML document into p, adding to what is already there rather than
 * replacing it — an import is a subscription list arriving, not a demand that
 * the existing one be thrown away. A feed already present is skipped, and a
 * group is matched by name so importing the same file twice changes nothing.
 *
 * Returns the number of feeds actually added.
 */
int GazetteOPMLParse(const char *text, size_t len, GazettePrefs *p);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_OPML_H */
