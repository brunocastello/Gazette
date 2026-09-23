/*
 * Gazette — the Windows file store, proved on CI
 * Copyright (c) 2026 brunocastello
 *
 * Nothing here is shipped. src/store/gazette_store_win32.c driven from a
 * console program under Wine, the way GazetteNetTest drives the network:
 * the preferences written and read back, a feed's cache streamed out and in
 * line by line, the CR the engine writes turned into CRLF on disk, the Mac's
 * cache-file name for the same URL, the named data files, and a feed's cache
 * deleted. Exits non-zero on the first thing that is not as it should be.
 */

#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "store/gazette_store.h"

static int gFailures;

static void Check(const char *what, int ok)
{
    printf("   %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        gFailures++;
    }
}

/* The raw bytes of a file beside the executable, for looking at the line
   endings the store actually wrote. */
static long RawFile(const char *leaf, char *buf, long cap)
{
    char   path[MAX_PATH];
    char  *slash;
    HANDLE h;
    DWORD  got = 0;

    GetModuleFileNameA(NULL, path, sizeof(path));
    slash = strrchr(path, '\\');
    if (slash == NULL) {
        return -1;
    }
    lstrcpyA(slash + 1, leaf);
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    ReadFile(h, buf, (DWORD)(cap - 1), &got, NULL);
    CloseHandle(h);
    buf[got] = '\0';
    return (long)got;
}

int main(void)
{
    static const char kPrefs[] = "# Gazette Preferences\rfeed = https://e/1\r";
    static const char kURL[]   = "https://news.google.com/rss";
    char              buf[1024];
    long              len = 0;
    GazetteStoreFile *f;

    printf("== preferences\n");
    Check("written", GazetteStoreWritePrefs(kPrefs, (long)strlen(kPrefs)));
    Check("read back", GazetteStoreReadPrefs(buf, sizeof buf, &len));
    Check("as CRLF on disk, for Notepad",
          RawFile("Gazette Preferences.txt", buf, sizeof buf) > 0 &&
          strcmp(buf, "# Gazette Preferences\r\nfeed = https://e/1\r\n") == 0);

    printf("== a feed's cache, streamed\n");
    f = GazetteStoreCacheCreate(kURL);
    Check("created", f != NULL);
    Check("a line written", GazetteStoreWriteLine(f, "GAZETTE-CACHE 6"));
    Check("text written", GazetteStoreWrite(f, "T One\rT Two\r", 12));
    GazetteStoreClose(f);

    f = GazetteStoreCacheOpen(kURL);
    Check("opened", f != NULL);
    Check("line 1", GazetteStoreReadLine(f, buf, sizeof buf) == 15 &&
                    strcmp(buf, "GAZETTE-CACHE 6") == 0);
    Check("line 2", GazetteStoreReadLine(f, buf, sizeof buf) >= 0 &&
                    strcmp(buf, "T One") == 0);
    Check("line 3", GazetteStoreReadLine(f, buf, sizeof buf) >= 0 &&
                    strcmp(buf, "T Two") == 0);
    Check("then the end", GazetteStoreReadLine(f, buf, sizeof buf) == -1);
    GazetteStoreClose(f);

    /* FNV-1a of the URL, computed as the Mac's CacheFileName does, names
       the file the store made. */
    {
        unsigned long hash = 2166136261UL;
        const char   *p;

        for (p = kURL; *p != '\0'; p++) {
            hash ^= (unsigned long)(unsigned char)*p;
            hash *= 16777619UL;
        }
        {
            char leaf[64];

            wsprintfA(leaf, "Gazette Cache\\Feed %08lX", hash & 0xFFFFFFFFUL);
            printf("   cache file: %s\n", leaf);
            Check("the cache file is where the Mac's name says",
                  RawFile(leaf, buf, sizeof buf) > 0);
        }
    }

    printf("== named data files\n");
    Check("written whole", GazetteStoreWriteDataFile("Gazette Index", "I 1\r", 4));
    f = GazetteStoreDataOpen("Gazette Index");
    Check("read back", f != NULL &&
                       GazetteStoreReadLine(f, buf, sizeof buf) == 3 &&
                       strcmp(buf, "I 1") == 0);
    GazetteStoreClose(f);

    printf("== a feed removed\n");
    GazetteStoreCacheDelete(kURL);
    Check("its cache is gone", GazetteStoreCacheOpen(kURL) == NULL);

    printf("%d failure%s\n", gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
