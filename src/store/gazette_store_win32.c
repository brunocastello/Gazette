/*
 * Gazette — local file storage on Windows 95 through XP
 * Copyright (c) 2026 brunocastello
 *
 * gazette_store.h in Win32 files: the Mac's gazette_store.c with the File
 * Manager and Navigation Services swapped for CreateFile and the common
 * dialogs. The names, the line rules and the cache-file hash are the Mac's,
 * so a feed list, a cache folder or an OPML file moves between the two.
 *
 * Where it all lives is docs/windows.md: beside Gazette.exe, as Gateway keeps
 * its settings, or -- where that folder cannot be written -- in
 * %APPDATA%\Gazette (2000, XP), then %USERPROFILE%\Gazette (NT 4.0).
 *
 * Files are written with CRLF. The engine ends lines with a bare CR, the Mac
 * way, and Notepad on 95 to XP shows such a file as one long line; so every
 * CR on its way to disk becomes CRLF. Reading accepts CR, LF and CRLF, as
 * the Mac's does.
 *
 * ANSI calls throughout (the A forms): Windows 95 has no Unicode file API.
 */

#include <windows.h>
#include <commdlg.h>

#include <stdio.h>
#include <string.h>

#include "store/gazette_store.h"

static const char kPrefsFileName[]   = "Gazette Preferences.txt";
static const char kCacheFolderName[] = "Gazette Cache";

/* ------------------------------------------------------------------ */
/* The folder                                                          */
/* ------------------------------------------------------------------ */

static char gBase[MAX_PATH];        /* "" until found, then without a slash */
static int  gBaseTried;

/* Whether a file can be made in dir: made, then removed. Asking the file
   system is the one test that means the same on 95 as on NT. */
static int CanWriteIn(const char *dir)
{
    char   probe[MAX_PATH];
    HANDLE h;

    if (lstrlenA(dir) + 16 >= MAX_PATH) {
        return 0;
    }
    wsprintfA(probe, "%s\\Gazette.$$$", dir);
    h = CreateFileA(probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    CloseHandle(h);
    DeleteFileA(probe);
    return 1;
}

/* An environment variable's folder with "\Gazette" on the end, created if
   need be. 0 when the variable is not set or the folder cannot be made. */
static int TryUnder(const char *variable, char *out)
{
    char  root[MAX_PATH];
    DWORD n = GetEnvironmentVariableA(variable, root, sizeof(root));

    if (n == 0 || n >= sizeof(root) || lstrlenA(root) + 9 >= MAX_PATH) {
        return 0;
    }
    wsprintfA(out, "%s\\Gazette", root);
    CreateDirectoryA(out, NULL);            /* already there is fine */
    return CanWriteIn(out);
}

/* Where Gazette's files go, found once. "" when nowhere can be written:
   Gazette still runs, and every read and write here fails quietly. */
static const char *Base(void)
{
    char *slash;

    if (gBaseTried) {
        return gBase;
    }
    gBaseTried = 1;

    if (GetModuleFileNameA(NULL, gBase, sizeof(gBase)) > 0) {
        slash = strrchr(gBase, '\\');
        if (slash != NULL) {
            *slash = '\0';
            if (CanWriteIn(gBase)) {
                return gBase;
            }
        }
    }
    if (TryUnder("APPDATA", gBase) || TryUnder("USERPROFILE", gBase)) {
        return gBase;
    }
    gBase[0] = '\0';
    return gBase;
}

/* base\leaf, or 0 when there is no base or it would not fit. */
static int PathIn(const char *leaf, char *out)
{
    const char *base = Base();

    if (base[0] == '\0' || lstrlenA(base) + lstrlenA(leaf) + 2 > MAX_PATH) {
        return 0;
    }
    wsprintfA(out, "%s\\%s", base, leaf);
    return 1;
}

/* base\Gazette Cache\leaf, making the folder when asked. */
static int CachePath(const char *leaf, int createFolder, char *out)
{
    char folder[MAX_PATH];

    if (!PathIn(kCacheFolderName, folder) ||
        lstrlenA(folder) + lstrlenA(leaf) + 2 > MAX_PATH) {
        return 0;
    }
    if (createFolder) {
        CreateDirectoryA(folder, NULL);
    }
    wsprintfA(out, "%s\\%s", folder, leaf);
    return 1;
}

/*
 * "Feed XXXXXXXX": the Mac's name, from the same FNV-1a hash of the feed's
 * URL. unsigned long is 32 bits on Win32 as it is on the PowerPC, so the same
 * URL gives the same name on both.
 */
static void CacheFileName(const char *feedURL, char *out)
{
    unsigned long hash = 2166136261UL;
    const char   *p;

    for (p = feedURL; p != NULL && *p != '\0'; p++) {
        hash ^= (unsigned long)(unsigned char)*p;
        hash *= 16777619UL;
    }
    wsprintfA(out, "Feed %08lX", hash & 0xFFFFFFFFUL);
}

/* ------------------------------------------------------------------ */
/* Streamed files                                                      */
/* ------------------------------------------------------------------ */

struct GazetteStoreFile {
    HANDLE handle;
    int    writing;
    int    atEOF;
    int    lastCR;              /* writing: the last byte out was a CR */
    long   len;                 /* bytes in buf */
    long   pos;                 /* reading: next byte of buf */
    char   buf[4096];
};

static GazetteStoreFile *OpenPath(const char *path, int writing)
{
    GazetteStoreFile *f;
    HANDLE            h;

    h = CreateFileA(path, writing ? GENERIC_WRITE : GENERIC_READ,
                    writing ? 0 : FILE_SHARE_READ, NULL,
                    writing ? CREATE_ALWAYS : OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    f = (GazetteStoreFile *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                      sizeof(GazetteStoreFile));
    if (f == NULL) {
        CloseHandle(h);
        return NULL;
    }
    f->handle  = h;
    f->writing = writing;
    return f;
}

GazetteStoreFile *GazetteStoreCacheCreate(const char *feedURL)
{
    char name[32];
    char path[MAX_PATH];

    if (feedURL == NULL || feedURL[0] == '\0') {
        return NULL;
    }
    CacheFileName(feedURL, name);
    return CachePath(name, 1, path) ? OpenPath(path, 1) : NULL;
}

GazetteStoreFile *GazetteStoreCacheOpen(const char *feedURL)
{
    char name[32];
    char path[MAX_PATH];

    if (feedURL == NULL || feedURL[0] == '\0') {
        return NULL;
    }
    CacheFileName(feedURL, name);
    return CachePath(name, 0, path) ? OpenPath(path, 0) : NULL;
}

GazetteStoreFile *GazetteStoreDataCreate(const char *name)
{
    char path[MAX_PATH];

    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    return CachePath(name, 1, path) ? OpenPath(path, 1) : NULL;
}

GazetteStoreFile *GazetteStoreDataOpen(const char *name)
{
    char path[MAX_PATH];

    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    return CachePath(name, 0, path) ? OpenPath(path, 0) : NULL;
}

void GazetteStoreCacheDelete(const char *feedURL)
{
    char name[32];
    char path[MAX_PATH];

    if (feedURL == NULL || feedURL[0] == '\0') {
        return;
    }
    CacheFileName(feedURL, name);
    if (CachePath(name, 0, path)) {
        DeleteFileA(path);
    }
}

static int FlushWrite(GazetteStoreFile *f)
{
    DWORD wrote = 0;

    if (f->len == 0) {
        return 1;
    }
    if (!WriteFile(f->handle, f->buf, (DWORD)f->len, &wrote, NULL) ||
        wrote != (DWORD)f->len) {
        return 0;
    }
    f->len = 0;
    return 1;
}

static int PutByte(GazetteStoreFile *f, char c)
{
    if (f->len == (long)sizeof f->buf && !FlushWrite(f)) {
        return 0;
    }
    f->buf[f->len++] = c;
    return 1;
}

/* Every CR becomes CRLF on the way out; an LF the text already had after
   its CR is not doubled. */
int GazetteStoreWrite(GazetteStoreFile *f, const char *text, long len)
{
    long i;

    if (f == NULL || !f->writing || text == NULL || len < 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        char c = text[i];

        if (c == '\n' && f->lastCR) {
            f->lastCR = 0;
            continue;
        }
        if (!PutByte(f, c)) {
            return 0;
        }
        if (c == '\r') {
            if (!PutByte(f, '\n')) {
                return 0;
            }
            f->lastCR = 1;
        } else {
            f->lastCR = 0;
        }
    }
    return 1;
}

int GazetteStoreWriteLine(GazetteStoreFile *f, const char *text)
{
    if (f == NULL || text == NULL) {
        return 0;
    }
    if (!GazetteStoreWrite(f, text, (long)strlen(text))) {
        return 0;
    }
    return GazetteStoreWrite(f, "\r", 1);
}

/* Next byte, or -1 at end of file. */
static int ReadByte(GazetteStoreFile *f)
{
    if (f->pos >= f->len) {
        DWORD got = 0;

        if (f->atEOF) {
            return -1;
        }
        if (!ReadFile(f->handle, f->buf, sizeof f->buf, &got, NULL) ||
            got == 0) {
            f->atEOF = 1;
            return -1;
        }
        f->len = (long)got;
        f->pos = 0;
    }
    return (unsigned char)f->buf[f->pos++];
}

/* The Mac's reader exactly: a line ends at CR, LF or CRLF; past cap the
   rest of the line is dropped; -1 only when nothing at all was read. */
long GazetteStoreReadLine(GazetteStoreFile *f, char *buf, long cap)
{
    long len = 0;
    int  c;
    int  any = 0;

    if (f == NULL || f->writing || buf == NULL || cap <= 0) {
        return -1;
    }
    buf[0] = '\0';

    for (;;) {
        c = ReadByte(f);
        if (c < 0) {
            break;
        }
        any = 1;
        if (c == '\r' || c == '\n') {
            if (c == '\r') {
                int next = ReadByte(f);
                if (next >= 0 && next != '\n') {
                    f->pos--;               /* put it back */
                }
            }
            break;
        }
        if (len < cap - 1) {
            buf[len++] = (char)c;
        }
    }

    buf[len] = '\0';
    return (any || len > 0) ? len : -1;
}

void GazetteStoreClose(GazetteStoreFile *f)
{
    if (f == NULL) {
        return;
    }
    if (f->writing) {
        (void)FlushWrite(f);
    }
    CloseHandle(f->handle);
    HeapFree(GetProcessHeap(), 0, f);
}

/* ------------------------------------------------------------------ */
/* Whole files                                                         */
/* ------------------------------------------------------------------ */

/* Read a whole file into buf, NUL-terminated, cap - 1 bytes at most. */
static int ReadWhole(const char *path, char *buf, long cap, long *outLen)
{
    HANDLE h;
    DWORD  got = 0;

    if (outLen != NULL) {
        *outLen = 0;
    }
    if (buf == NULL || cap <= 0) {
        return 0;
    }
    buf[0] = '\0';

    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (!ReadFile(h, buf, (DWORD)(cap - 1), &got, NULL)) {
        CloseHandle(h);
        buf[0] = '\0';
        return 0;
    }
    CloseHandle(h);
    buf[got] = '\0';
    if (outLen != NULL) {
        *outLen = (long)got;
    }
    return 1;
}

/* Write text as a whole file, through the same CRLF rule as streams. */
static int WriteWhole(const char *path, const char *text, long len)
{
    GazetteStoreFile *f = OpenPath(path, 1);
    int               ok;

    if (f == NULL) {
        return 0;
    }
    ok = GazetteStoreWrite(f, text, len) && FlushWrite(f);
    GazetteStoreClose(f);
    return ok;
}

int GazetteStoreReadPrefs(char *buf, long cap, long *outLen)
{
    char path[MAX_PATH];

    if (outLen != NULL) {
        *outLen = 0;
    }
    if (buf != NULL && cap > 0) {
        buf[0] = '\0';
    }
    if (!PathIn(kPrefsFileName, path)) {
        return 0;
    }
    return ReadWhole(path, buf, cap, outLen);
}

int GazetteStoreWritePrefs(const char *text, long len)
{
    char path[MAX_PATH];

    if (text == NULL || len < 0 || !PathIn(kPrefsFileName, path)) {
        return 0;
    }
    return WriteWhole(path, text, len);
}

int GazetteStoreWriteDataFile(const char *name, const char *text, long len)
{
    char path[MAX_PATH];

    if (name == NULL || text == NULL || len < 0 ||
        !CachePath(name, 1, path)) {
        return 0;
    }
    return WriteWhole(path, text, len);
}

/* ------------------------------------------------------------------ */
/* Files the user chooses                                              */
/*                                                                     */
/* The common Open and Save As dialogs, which are in comdlg32 on every  */
/* Windows 95. Like a Nav dialog, each runs its own message loop, so a  */
/* timer calls the idle procedure while it is up and a fetch keeps      */
/* moving -- the timer's messages are dispatched by the dialog's loop.  */
/* ------------------------------------------------------------------ */

static GazetteStoreIdle gIdle;
static char             gError[128];

void GazetteStoreSetIdle(GazetteStoreIdle idle)
{
    gIdle = idle;
}

const char *GazetteStoreErrorText(void)
{
    return gError;
}

static VOID CALLBACK IdleTimer(HWND hwnd, UINT message, UINT_PTR id,
                               DWORD time)
{
    (void)hwnd;
    (void)message;
    (void)id;
    (void)time;
    if (gIdle != NULL) {
        gIdle();
    }
}

static int Failed(const char *what)
{
    DWORD code = GetLastError();

    if (code != 0) {
        snprintf(gError, sizeof gError, "%s (error %lu)", what,
                 (unsigned long)code);
    } else {
        snprintf(gError, sizeof gError, "%s", what);
    }
    return kGazetteFileFailed;
}

/* The filter both dialogs offer: OPML first, since that is what Gazette
   imports and exports, then anything. */
static const char kFilter[] =
    "OPML files (*.opml; *.xml)\0*.opml;*.xml\0"
    "All files (*.*)\0*.*\0";

/*
 * A common dialog's own failure, as opposed to Cancel: both return FALSE,
 * and only CommDlgExtendedError tells them apart -- 0 is the user's Cancel.
 */
static int DialogOutcome(const char *what)
{
    DWORD code = CommDlgExtendedError();

    if (code == 0) {
        return kGazetteFileCancelled;
    }
    snprintf(gError, sizeof gError, "%s (dialog error %lu)", what,
             (unsigned long)code);
    return kGazetteFileFailed;
}

int GazetteStoreAskAndReadFile(const char *prompt, char *buf, long cap,
                               long *outLen)
{
    OPENFILENAMEA ofn;
    char          path[MAX_PATH];
    UINT_PTR      timer;
    BOOL          chosen;

    gError[0] = '\0';
    if (outLen != NULL) {
        *outLen = 0;
    }
    path[0] = '\0';

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = GetActiveWindow();
    ofn.lpstrFilter = kFilter;
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = sizeof(path);
    ofn.lpstrTitle  = prompt;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    timer  = SetTimer(NULL, 0, 100, IdleTimer);
    chosen = GetOpenFileNameA(&ofn);
    if (timer != 0) {
        KillTimer(NULL, timer);
    }
    if (!chosen) {
        return DialogOutcome("The Open dialog could not be shown");
    }
    if (!ReadWhole(path, buf, cap, outLen)) {
        return Failed("That file could not be read");
    }
    return kGazetteFileDone;
}

int GazetteStoreAskAndWriteFile(const char *prompt, const char *defaultName,
                                const char *text, long len)
{
    OPENFILENAMEA ofn;
    char          path[MAX_PATH];
    UINT_PTR      timer;
    BOOL          chosen;

    gError[0] = '\0';
    path[0] = '\0';
    if (defaultName != NULL) {
        lstrcpynA(path, defaultName, sizeof(path));
    }

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = GetActiveWindow();
    ofn.lpstrFilter = kFilter;
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = sizeof(path);
    ofn.lpstrTitle  = prompt;
    ofn.lpstrDefExt = "opml";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
                      OFN_HIDEREADONLY;

    timer  = SetTimer(NULL, 0, 100, IdleTimer);
    chosen = GetSaveFileNameA(&ofn);
    if (timer != 0) {
        KillTimer(NULL, timer);
    }
    if (!chosen) {
        return DialogOutcome("The Save dialog could not be shown");
    }
    if (!WriteWhole(path, text, len)) {
        return Failed("That file could not be written");
    }
    return kGazetteFileDone;
}
