/*
 * Gazette — Core C seam (thin, no system headers)
 * Copyright (c) 2026 brunocastello
 *
 * Pure C interface between UI (Multiversal) and engine (Universal / portable).
 */

#ifndef GAZETTE_CORE_H
#define GAZETTE_CORE_H

/* The seam trades in plain types only. MacTypes.h is the one exception:
   it is present in both Multiversal and Apple's Universal Interfaces, so
   including it here keeps Boolean/true/false meaningful on either side of
   the seam (and lets gazette_core.c compile without pulling in Carbon). */
#include <MacTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle — UI code allocates, engine consumes */
typedef struct GazetteFeedList  *GazetteFeedListRef;
typedef struct GazetteArticle   *GazetteArticleRef;

/* Feed entry */
typedef struct {
    char  url[512];       /* feed URL (max 511 chars + NUL) */
    char  title[256];     /* display name */
    short feedType;       /* 0 = RSS 2.0, 1 = Atom */
    Boolean enabled;
} GazetteFeedEntry;

/* Article entry */
typedef struct {
    char  title[512];
    char  link[512];
    char  source[256];
    long  date;           /* seconds since epoch (approx) */
    Boolean read;
    Boolean starred;
} GazetteArticleEntry;

/* Feed list operations */
GazetteFeedListRef GazetteFeedListCreate(void);
void             GazetteFeedListDispose(GazetteFeedListRef list);
Boolean          GazetteFeedListAddEntry(GazetteFeedListRef list, const char *url);
Boolean          GazetteFeedListRemoveEntry(GazetteFeedListRef list, const char *url);
int              GazetteFeedListCount(const GazetteFeedListRef list);

/* Article operations */
GazetteArticleRef  GazetteArticleCreate(void);
void               GazetteArticleDispose(GazetteArticleRef article);
void               GazetteArticleSetTitle(GazetteArticleRef a, const char *title);
void               GazetteArticleSetLink(GazetteArticleRef a, const char *link);

/* Feed fetching (stub — Phase 1) */
typedef void (*GazetteFetchCallback)(const char *data, int len, void *context);
void GazetteFeedListFetchAll(GazetteFeedListRef list, GazetteFetchCallback cb, void *context);

/* Preferences (stub — Phase 0) */
Boolean GazettePrefsLoad(const char *path, GazetteFeedListRef list);
Boolean GazettePrefsSave(const char *path, const GazetteFeedListRef list);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_CORE_H */
