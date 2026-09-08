/*
 * Gazette — Core C seam implementation (stub for Phase 0)
 * Copyright (c) 2026 brunocastello
 */

#include "gazette_core.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Feed list — simple linked-list stub (Phase 0)                       */
/* ------------------------------------------------------------------ */

struct GazetteFeedList {
    int            count;
    int            capacity;
    GazetteFeedEntry *entries;
};

GazetteFeedListRef GazetteFeedListCreate(void)
{
    GazetteFeedListRef list = (GazetteFeedListRef)calloc(1, sizeof(struct GazetteFeedList));
    if (list) {
        list->capacity = 16;
        list->entries  = (GazetteFeedEntry *)calloc(list->capacity, sizeof(GazetteFeedEntry));
        list->count    = 0;
    }
    return list;
}

void GazetteFeedListDispose(GazetteFeedListRef list)
{
    if (list) {
        free(list->entries);
        free(list);
    }
}

Boolean GazetteFeedListAddEntry(GazetteFeedListRef list, const char *url)
{
    if (!list || !url) return false;

    /* Grow array if needed */
    if (list->count >= list->capacity) {
        int newCap = list->capacity * 2;
        GazetteFeedEntry *newEntries = (GazetteFeedEntry *)realloc(
            list->entries, newCap * sizeof(GazetteFeedEntry));
        if (!newEntries) return false;
        list->entries  = newEntries;
        list->capacity = newCap;
    }

    memset(&list->entries[list->count], 0, sizeof(GazetteFeedEntry));
    strncpy(list->entries[list->count].url, url, 511);
    list->entries[list->count].enabled = true;
    list->count++;

    return true;
}

Boolean GazetteFeedListRemoveEntry(GazetteFeedListRef list, const char *url)
{
    if (!list || !url) return false;

    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->entries[i].url, url) == 0) {
            /* Shift remaining entries down */
            for (int j = i; j < list->count - 1; j++) {
                list->entries[j] = list->entries[j + 1];
            }
            memset(&list->entries[list->count - 1], 0, sizeof(GazetteFeedEntry));
            list->count--;
            return true;
        }
    }
    return false;
}

int GazetteFeedListCount(const GazetteFeedListRef list)
{
    return list ? list->count : 0;
}

/* ------------------------------------------------------------------ */
/* Article — simple stub                                               */
/* ------------------------------------------------------------------ */

struct GazetteArticle {
    char  title[512];
    char  link[512];
    char  source[256];
    long  date;
    Boolean read;
    Boolean starred;
};

GazetteArticleRef GazetteArticleCreate(void)
{
    GazetteArticleRef a = (GazetteArticleRef)calloc(1, sizeof(struct GazetteArticle));
    return a;
}

void GazetteArticleDispose(GazetteArticleRef article)
{
    free(article);
}

void GazetteArticleSetTitle(GazetteArticleRef a, const char *title)
{
    if (a && title) {
        strncpy(a->title, title, 511);
    }
}

void GazetteArticleSetLink(GazetteArticleRef a, const char *link)
{
    if (a && link) {
        strncpy(a->link, link, 511);
    }
}

/* ------------------------------------------------------------------ */
/* Fetch — stub (Phase 1: Gateway net + Certainly)                     */
/* ------------------------------------------------------------------ */

void GazetteFeedListFetchAll(GazetteFeedListRef list, GazetteFetchCallback cb, void *context)
{
    /* Phase 1: integrate Gateway gw_net + Certainly here */
    (void)list;
    (void)cb;
    (void)context;
}

/* ------------------------------------------------------------------ */
/* Preferences — stub (Phase 0: read/write feed list from file)        */
/* ------------------------------------------------------------------ */

Boolean GazettePrefsLoad(const char *path, GazetteFeedListRef list)
{
    /* Phase 0 stub: always returns true; real implementation later */
    (void)path;
    (void)list;
    return true;
}

Boolean GazettePrefsSave(const char *path, const GazetteFeedListRef list)
{
    /* Phase 0 stub: always returns true; real implementation later */
    (void)path;
    (void)list;
    return true;
}
