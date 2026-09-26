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

/* Gazette.ini, as Gateway keeps Gateway.ini: the Windows name for a
   settings file beside the program (Bruno, 2026-09-26). The contents are
   the Mac's grammar, key = value, one to a line, which is what an INI
   file is; a section header or a ';' comment is skipped by the parser. */
static const char kPrefsFileName[]   = "Gazette.ini";
/* What builds before 2026-09-26 called it: read when there is no
   Gazette.ini yet, so a feed list made with one of them carries over. The
   next save writes Gazette.ini, and the old file is left alone. */
static const char kOldPrefsFileName[] = "Gazette Preferences.txt";
/* Beside the program, where "Gazette" in the name would say nothing a
   folder next to Gazette.exe does not (Bruno, 2026-09-26). The Mac's is
   "Gazette Cache" because it lives in the shared Preferences folder. */
static const char kCacheFolderName[] = "Cache";
/* What builds before 2026-09-26 called it: renamed on first use, so the
   cached articles and the read marks in it carry over. */
static const char kOldCacheFolderName[] = "Gazette Cache";

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

/* An earlier build's "Gazette Cache", renamed to "Cache" when there is no
   "Cache" yet. Once a run: after that the answer cannot change. */
static void AdoptOldCache(void)
{
    static int done;
    char       oldFolder[MAX_PATH];
    char       folder[MAX_PATH];

    if (done) {
        return;
    }
    done = 1;
    if (PathIn(kOldCacheFolderName, oldFolder) &&
        PathIn(kCacheFolderName, folder) &&
        GetFileAttributesA(folder) == (DWORD)-1 &&
        GetFileAttributesA(oldFolder) != (DWORD)-1) {
        MoveFileA(oldFolder, folder);
    }
}

/* base\Cache\leaf, making the folder when asked. */
static int CachePath(const char *leaf, int createFolder, char *out)
{
    char folder[MAX_PATH];

    AdoptOldCache();
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

/*
 * A file being written goes to "<name>.new" beside the real one and takes
 * its place only once every byte is down (Close). Opening the real file
 * with CREATE_ALWAYS would empty it first, so a write that failed half way
 * -- a full disk, above all a floppy (Bruno's 86Box run, 2026-09-26: the
 * cache filled the disk and the feed list came back empty) -- lost the old
 * contents as well as the new. Now a failed write loses only itself.
 */
struct GazetteStoreFile {
    HANDLE handle;
    int    writing;
    int    failed;              /* writing: a write did not go down */
    char   path[MAX_PATH];      /* writing: the real file */
    char   temp[MAX_PATH];      /* writing: where the bytes go meanwhile */
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
    char              temp[MAX_PATH];

    if (writing) {
        if (lstrlenA(path) + 5 > MAX_PATH) {
            return NULL;
        }
        wsprintfA(temp, "%s.new", path);
        path = temp;
    }
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
    if (writing) {
        lstrcpynA(f->temp, temp, sizeof f->temp);
        lstrcpynA(f->path, temp, sizeof f->path);
        f->path[lstrlenA(f->path) - 4] = '\0';     /* without ".new" */
    }
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
        f->failed = 1;
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

/*
 * Close, and for a file written, put it in place: the finished ".new" over
 * the real file when every write went down, or thrown away when one did
 * not, leaving the real file as it was. MoveFileEx replaces in one step on
 * NT; 95, 98 and Me answer it with "not implemented", and there the old
 * file is deleted and the new one renamed -- the only moment a crash could
 * cost the file, and a moment rather than the length of a whole write.
 * Returns whether the file is now what was written.
 */
typedef BOOL (WINAPI *MoveFileExProc)(LPCSTR, LPCSTR, DWORD);

/* MoveFileExA by name: 95 has at most a stub of it, and a static import
   is a load-time failure on any Windows that lacks the export. */
static BOOL ReplaceFile95(const char *from, const char *to)
{
    static MoveFileExProc moveEx;
    static int            looked;

    if (!looked) {
        HMODULE kernel = GetModuleHandleA("KERNEL32.DLL");

        looked = 1;
        if (kernel != NULL) {
            moveEx = (MoveFileExProc)GetProcAddress(kernel, "MoveFileExA");
        }
    }
    return (moveEx != NULL && moveEx(from, to, MOVEFILE_REPLACE_EXISTING));
}

static int Finish(GazetteStoreFile *f)
{
    int ok = 1;

    if (f->writing) {
        if (!FlushWrite(f)) {
            f->failed = 1;
        }
    }
    CloseHandle(f->handle);

    if (f->writing) {
        if (f->failed) {
            DeleteFileA(f->temp);
            ok = 0;
        } else if (!ReplaceFile95(f->temp, f->path)) {
            DeleteFileA(f->path);
            if (!MoveFileA(f->temp, f->path)) {
                DeleteFileA(f->temp);
                ok = 0;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, f);
    return ok;
}

void GazetteStoreClose(GazetteStoreFile *f)
{
    if (f != NULL) {
        (void)Finish(f);
    }
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
    if (!GazetteStoreWrite(f, text, len)) {
        f->failed = 1;
    }
    ok = Finish(f);
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
    if (ReadWhole(path, buf, cap, outLen)) {
        return 1;
    }
    return PathIn(kOldPrefsFileName, path) &&
           ReadWhole(path, buf, cap, outLen);
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
   imports and exports, then anything. *.opm as well: a feed list the Mac
   exported and copied to a PC floppy arrives with an 8.3 name, since
   Mac OS 9 writes DOS disks without long names, and "Gazette Feeds.opml"
   becomes something.OPM that *.opml alone would hide. */
static const char kFilter[] =
    "OPML files (*.opml; *.opm; *.xml)\0*.opml;*.opm;*.xml\0"
    "All files (*.*)\0*.*\0";

/*
 * The size the Open and Save dialogs are told their structure is: the
 * Windows 95 one. MinGW-w64's commdlg.h always declares OPENFILENAMEA with
 * the three fields Windows 2000 added, whatever _WIN32_WINNT says, and 95,
 * 98 and NT 4.0 answer a structure of that size with CDERR_STRUCTSIZE and
 * no dialog at all. 2000, XP and Wine take either, which is how 0.2.0
 * shipped with Import Feeds and Export Feeds that opened nothing on
 * Windows 95 (Bruno's 86Box, 2026-09-26) and everything passed on CI.
 * None of the later fields is used here, so the old size loses nothing.
 */
#define kOpenFileNameSize OPENFILENAME_SIZE_VERSION_400A

/* 76 bytes on Win32, and the build stops if it is ever anything else. */
typedef char gazette_open_file_name_is_the_95_size[
    (kOpenFileNameSize == 76) ? 1 : -1];

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
    ofn.lStructSize = kOpenFileNameSize;
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
    ofn.lStructSize = kOpenFileNameSize;
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
