/*
 * Gazette — the Windows file store, proved on CI
 * Copyright (c) 2026 brunocastello
 *
 * Nothing here is shipped. src/store/gazette_store_win32.c driven from a
 * console program under Wine, the way GazetteNetTest drives the network:
 * the preferences written and read back, a feed's cache streamed out and in
 * line by line, the CR the engine writes turned into CRLF on disk, the Mac's
 * cache-file name for the same URL, the named data files, and a feed's cache
 * deleted -- and File > Import Feeds and Export Feeds: the real Open and
 * Save dialogs, driven from a timer the way a user would, a name typed in
 * and OK pressed, then the file read back. Exits non-zero on the first
 * thing that is not as it should be.
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

/* ------------------------------------------------------------------ */
/* Driving the Open and Save dialogs                                   */
/* ------------------------------------------------------------------ */

enum { kDriveName, kDriveCancel };

static int      gDriveAction;
static char     gDrivePath[MAX_PATH];
static int      gDriven;
static UINT_PTR gDriveTimer;

static BOOL CALLBACK FindDialog(HWND hwnd, LPARAM lParam)
{
    char cls[16];

    if (IsWindowVisible(hwnd) && GetClassNameA(hwnd, cls, sizeof cls) &&
        lstrcmpA(cls, "#32770") == 0) {
        *(HWND *)lParam = hwnd;
        return FALSE;
    }
    return TRUE;
}

/* A thread timer, which the dialog's own message loop runs: once the
   dialog is up, the file name goes into its name field -- edt1, or the
   combo box cmb13 that later Save dialogs use -- and OK is pressed. */
static VOID CALLBACK DriveDialog(HWND unused, UINT message, UINT_PTR id,
                                 DWORD time)
{
    HWND dialog = NULL;
    HWND name;

    (void)unused;
    (void)message;
    (void)id;
    (void)time;
    if (gDriven) {
        return;
    }
    EnumThreadWindows(GetCurrentThreadId(), FindDialog, (LPARAM)&dialog);
    if (dialog == NULL) {
        return;
    }
    gDriven = 1;
    if (gDriveAction == kDriveCancel) {
        PostMessageA(dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED),
                     (LPARAM)GetDlgItem(dialog, IDCANCEL));
        return;
    }
    name = GetDlgItem(dialog, 0x480);               /* edt1 */
    if (name == NULL) {
        name = GetDlgItem(dialog, 0x47C);           /* cmb13 */
    }
    SetWindowTextA(name, gDrivePath);
    PostMessageA(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED),
                 (LPARAM)GetDlgItem(dialog, IDOK));
}

static void Drive(int action, const char *leaf)
{
    char *slash;

    gDriveAction = action;
    gDriven      = 0;
    GetModuleFileNameA(NULL, gDrivePath, sizeof(gDrivePath));
    slash = strrchr(gDrivePath, '\\');
    if (slash != NULL && leaf != NULL) {
        lstrcpyA(slash + 1, leaf);
    }
    gDriveTimer = SetTimer(NULL, 0, 250, DriveDialog);
}

static void StopDriving(void)
{
    if (gDriveTimer != 0) {
        KillTimer(NULL, gDriveTimer);
        gDriveTimer = 0;
    }
}

static void WriteRaw(const char *leaf, const char *text)
{
    char   path[MAX_PATH];
    char  *slash;
    HANDLE h;
    DWORD  wrote;

    GetModuleFileNameA(NULL, path, sizeof(path));
    slash = strrchr(path, '\\');
    if (slash == NULL) {
        return;
    }
    lstrcpyA(slash + 1, leaf);
    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, text, (DWORD)lstrlenA(text), &wrote, NULL);
        CloseHandle(h);
    }
}

static const char *Outcome(int outcome)
{
    switch (outcome) {
    case kGazetteFileDone:      return "done";
    case kGazetteFileCancelled: return "cancelled";
    default:                    return "failed";
    }
}

static void TestDialogs(void)
{
    /* A Mac's export, as it comes off a floppy: CR line endings. */
    static const char kOPML[] =
        "<?xml version=\"1.0\"?>\r<opml version=\"1.1\"><body>\r"
        "<outline text=\"A\" xmlUrl=\"https://a.example/rss\"/>\r"
        "</body></opml>\r";
    static char buf[4096];
    long        len = 0;
    int         outcome;

    printf("== File > Import Feeds: the Open dialog\n");
    WriteRaw("import.opml", kOPML);
    Drive(kDriveName, "import.opml");
    outcome = GazetteStoreAskAndReadFile("Choose an OPML feed list to import:",
                                         buf, sizeof buf, &len);
    StopDriving();
    printf("   outcome: %s %s\n", Outcome(outcome), GazetteStoreErrorText());
    Check("the dialog opened and was answered", gDriven);
    Check("the chosen file was read", outcome == kGazetteFileDone &&
                                      len == (long)strlen(kOPML) &&
                                      memcmp(buf, kOPML, (size_t)len) == 0);

    printf("== File > Export Feeds: the Save dialog\n");
    Drive(kDriveName, "export.opml");
    outcome = GazetteStoreAskAndWriteFile("Save the feed list as:",
                                          "Gazette Feeds.opml", kOPML,
                                          (long)strlen(kOPML));
    StopDriving();
    printf("   outcome: %s %s\n", Outcome(outcome), GazetteStoreErrorText());
    Check("the dialog opened and was answered", gDriven);
    Check("the file was written, CRLF for Notepad",
          outcome == kGazetteFileDone &&
          RawFile("export.opml", buf, sizeof buf) > 0 &&
          strstr(buf, "<?xml version=\"1.0\"?>\r\n") == buf);

    printf("== Cancel\n");
    Drive(kDriveCancel, NULL);
    outcome = GazetteStoreAskAndReadFile("Choose an OPML feed list to import:",
                                         buf, sizeof buf, &len);
    StopDriving();
    Check("Cancel is cancelled, not a failure",
          outcome == kGazetteFileCancelled);
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
          RawFile("Gazette.ini", buf, sizeof buf) > 0 &&
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

            wsprintfA(leaf, "Cache\\Feed %08lX", hash & 0xFFFFFFFFUL);
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

    TestDialogs();

    printf("%d failure%s\n", gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
