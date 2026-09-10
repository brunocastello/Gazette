/*
 * Gazette — local file storage (Carbon File Manager)
 * Copyright (c) 2026 brunocastello
 *
 * The FSSpec calls used here — FindFolder, FSMakeFSSpec, FSpCreate,
 * FSpOpenDF, GetEOF/SetEOF, FSRead/FSWrite, FSClose — are all documented as
 * "in CarbonLib 1.0 and later", so they are safe under constraint 1. The
 * newer FSRef API is not: it arrived with Carbon 1.1, and Gazette targets 1.0.
 */

#include "gazette_store.h"

#include <MacTypes.h>
#include <MacErrors.h>   /* fnfErr, dupFNErr, eofErr — not reached via MacTypes.h */
#include <Files.h>
#include <Folders.h>
#include <MacMemory.h>   /* NewPtrClear, DisposePtr, BlockMoveData */
#include <Script.h>      /* smSystemScript */
#include <Navigation.h>  /* NavGetFile / NavPutFile — the only Carbon way */
#include <AppleEvents.h> /* AEGetNthPtr, to read Nav's reply */

#include <stdio.h>       /* snprintf, for the error text */
#include <string.h>      /* strlen */

/* Pascal strings, so they can go straight to FSMakeFSSpec. */
static const unsigned char kPrefsFileName[]   = "\pGazette Preferences";
static const unsigned char kCacheFolderName[] = "\pGazette Cache";

enum {
    kGazetteCreator = 'Gzt9',       /* must match CREATOR in CMakeLists.txt */
    kTextFileType   = 'TEXT',

    /*
     * SimpleText, for files Gazette hands to the outside world.
     *
     * Nav looks a file's "kind" string up from the type and creator together,
     * and nothing on the system registers a kind for a 'Gzt9' document --
     * Gazette's BNDL claims the application, not a document type -- so
     * NavPutFile refuses with kNavMissingKindStringErr before it draws
     * anything. 'ttxt' is registered on every Mac OS 9 system there is.
     *
     * It is also the right answer on its own merits. An exported feed list is
     * for other programs to read, and one that opens in SimpleText when it is
     * double-clicked is more use than one wearing Gazette's creator and
     * opening nothing at all.
     */
    kSimpleTextCreator = 'ttxt'
};

/* ------------------------------------------------------------------ */
/* Locating the preferences file                                       */
/* ------------------------------------------------------------------ */

/*
 * Build the FSSpec for the prefs file. kDontCreateFolder is deliberate: the
 * Preferences folder always exists on a working System Folder, and if it
 * somehow does not, failing to load prefs is the right outcome — Gazette has
 * defaults to fall back on and no business repairing the System Folder.
 *
 * FSMakeFSSpec returns fnfErr when the folder is there but the file is not.
 * That is the ordinary first-run answer and it still fills the spec in, so
 * the caller can create the file from it; only a harder error is fatal.
 */
static OSErr MakePrefsSpec(FSSpec *spec, Boolean *existed)
{
    OSErr err;
    short vRefNum;
    long  dirID;

    *existed = false;

    err = FindFolder(kOnSystemDisk, kPreferencesFolderType,
                     kDontCreateFolder, &vRefNum, &dirID);
    if (err != noErr) {
        return err;
    }

    err = FSMakeFSSpec(vRefNum, dirID, kPrefsFileName, spec);
    if (err == noErr) {
        *existed = true;
        return noErr;
    }
    if (err == fnfErr) {
        return noErr;
    }
    return err;
}

/* ------------------------------------------------------------------ */
/* Reading                                                             */
/* ------------------------------------------------------------------ */

int GazetteStoreReadPrefs(char *buf, long cap, long *outLen)
{
    FSSpec  spec;
    Boolean existed;
    OSErr   err;
    short   refNum;
    long    eof;
    long    count;

    if (outLen != NULL) {
        *outLen = 0;
    }
    if (buf == NULL || cap <= 0) {
        return 0;
    }
    buf[0] = '\0';

    err = MakePrefsSpec(&spec, &existed);
    if (err != noErr || !existed) {
        return 0;
    }

    err = FSpOpenDF(&spec, fsRdPerm, &refNum);
    if (err != noErr) {
        return 0;
    }

    err = GetEOF(refNum, &eof);
    if (err != noErr) {
        FSClose(refNum);
        return 0;
    }

    count = eof;
    if (count > cap - 1) {
        count = cap - 1;
    }

    if (count > 0) {
        err = FSRead(refNum, &count, buf);
        /* eofErr means FSRead delivered fewer bytes than it was asked for and
           then hit the end; count still says how many arrived, so a short
           read is a success rather than a failure. */
        if (err != noErr && err != eofErr) {
            FSClose(refNum);
            buf[0] = '\0';
            return 0;
        }
    }

    FSClose(refNum);

    buf[count] = '\0';
    if (outLen != NULL) {
        *outLen = count;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Writing                                                             */
/* ------------------------------------------------------------------ */

int GazetteStoreWritePrefs(const char *text, long len)
{
    FSSpec  spec;
    Boolean existed;
    OSErr   err;
    short   refNum;
    long    count;

    if (text == NULL || len < 0) {
        return 0;
    }

    err = MakePrefsSpec(&spec, &existed);
    if (err != noErr) {
        return 0;
    }

    if (!existed) {
        err = FSpCreate(&spec, kGazetteCreator, kTextFileType, smSystemScript);
        if (err != noErr && err != dupFNErr) {
            return 0;
        }
    }

    err = FSpOpenDF(&spec, fsWrPerm, &refNum);
    if (err != noErr) {
        return 0;
    }

    /* Truncate first: the new text is usually shorter than the old, and
       without this the tail of the previous version would survive past it. */
    err = SetEOF(refNum, 0);
    if (err != noErr) {
        FSClose(refNum);
        return 0;
    }

    count = len;
    if (count > 0) {
        err = FSWrite(refNum, &count, text);
        if (err != noErr || count != len) {
            FSClose(refNum);
            return 0;
        }
    }

    err = FSClose(refNum);
    return (err == noErr) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* The article cache                                                   */
/* ------------------------------------------------------------------ */

enum {
    kIOBufSize = 2048       /* one File Manager call per this many bytes */
};

struct GazetteStoreFile {
    short refNum;
    int   writing;

    char  buf[kIOBufSize];
    long  len;              /* bytes held in buf                        */
    long  pos;              /* read position within buf                 */
    int   atEOF;
};

/*
 * FNV-1a over the feed URL, rendered as eight hex digits. A file name has to
 * be derived from something stable about the feed, and the URL is the only
 * thing that is: its position in the list changes whenever the user reorders
 * the file, and its title changes whenever they rename it -- either of which
 * would hand a feed another one's articles.
 *
 * A collision would do the same thing, but at 32 bits and a few dozen feeds
 * it is not a risk worth carrying machinery for; the cache is also
 * regenerated by any refresh, so the worst case is one wrong list until then.
 */
static void CacheFileName(const char *feedURL, Str255 out)
{
    static const char kHex[] = "0123456789ABCDEF";
    unsigned long     hash   = 2166136261UL;
    const char       *p;
    int               i;

    for (p = feedURL; p != NULL && *p != '\0'; p++) {
        hash ^= (unsigned long)(unsigned char)*p;
        hash *= 16777619UL;
    }

    /* "Feed XXXXXXXX" — recognisable in the Finder, and short enough that
       the 31-character HFS limit is nowhere near. */
    out[0] = 13;
    out[1] = 'F'; out[2] = 'e'; out[3] = 'e'; out[4] = 'd'; out[5] = ' ';
    for (i = 0; i < 8; i++) {
        out[6 + i] = (unsigned char)kHex[(hash >> (28 - 4 * i)) & 0x0FUL];
    }
}

/*
 * A folder's own directory ID, given an FSSpec naming it.
 *
 * FSpGetDirectoryID would say this in one call, but its Availability: block
 * has no CarbonLib line at all -- it is glue that never made the transition --
 * so this goes through PBGetCatInfoSync, which is in CarbonLib 1.0. With
 * ioFDirIndex 0 that looks the name up in its parent and returns the
 * directory's ID in the same field it was asked in.
 */
static OSErr ResolveFolderDirID(const FSSpec *folder, long *dirID)
{
    CInfoPBRec pb;
    Str255     name;
    OSErr      err;

    BlockMoveData(folder->name, name, (Size)folder->name[0] + 1);
    memset(&pb, 0, sizeof pb);

    pb.dirInfo.ioNamePtr   = name;
    pb.dirInfo.ioVRefNum   = folder->vRefNum;
    pb.dirInfo.ioDrDirID   = folder->parID;
    pb.dirInfo.ioFDirIndex = 0;             /* look up by name */

    err = PBGetCatInfoSync(&pb);
    if (err != noErr) {
        return err;
    }
    /* A plain file sitting where the folder should be. Refusing is right:
       creating the cache would mean deleting whatever that is. */
    if ((pb.dirInfo.ioFlAttrib & ioDirMask) == 0) {
        return dupFNErr;
    }

    *dirID = pb.dirInfo.ioDrDirID;
    return noErr;
}

/*
 * The FSSpec for a feed's cache file, creating the folder if asked. Unlike
 * the preferences, the cache folder is ours to make: it does not exist until
 * the first successful refresh, and there is nothing for the user to have
 * created in advance.
 */
static OSErr MakeCacheSpecNamed(ConstStr255Param name, Boolean createFolder,
                                FSSpec *spec, Boolean *existed)
{
    OSErr  err;
    short  vRefNum;
    long   prefsDir;
    long   cacheDir;
    FSSpec folder;

    *existed = false;

    err = FindFolder(kOnSystemDisk, kPreferencesFolderType,
                     kDontCreateFolder, &vRefNum, &prefsDir);
    if (err != noErr) {
        return err;
    }

    err = FSMakeFSSpec(vRefNum, prefsDir, kCacheFolderName, &folder);
    if (err == fnfErr) {
        if (!createFolder) {
            return fnfErr;
        }
        err = FSpDirCreate(&folder, smSystemScript, &cacheDir);
        if (err != noErr && err != dupFNErr) {
            return err;
        }
        if (err == dupFNErr) {
            /* Created between the two calls, or the name is a file. Ask
               again rather than guessing which. */
            err = FSMakeFSSpec(vRefNum, prefsDir, kCacheFolderName, &folder);
            if (err != noErr) {
                return err;
            }
            err = ResolveFolderDirID(&folder, &cacheDir);
            if (err != noErr) {
                return err;
            }
        }
    } else if (err == noErr) {
        err = ResolveFolderDirID(&folder, &cacheDir);
        if (err != noErr) {
            return err;
        }
    } else {
        return err;
    }

    err = FSMakeFSSpec(vRefNum, cacheDir, name, spec);
    if (err == noErr) {
        *existed = true;
        return noErr;
    }
    return (err == fnfErr) ? noErr : err;
}

/* The same, for the file a feed's articles live in. */
static OSErr MakeCacheSpec(const char *feedURL, Boolean createFolder,
                           FSSpec *spec, Boolean *existed)
{
    Str255 name;

    CacheFileName(feedURL, name);
    return MakeCacheSpecNamed(name, createFolder, spec, existed);
}

/*
 * And for a file in that folder with a name of its own rather than a hashed
 * one — what Gazette remembers that does not belong to any single feed. The
 * name is given as a C string and converted here, so no caller outside this
 * file has to know a Pascal string from a C one.
 */
static OSErr MakeDataSpec(const char *name, Boolean createFolder,
                          FSSpec *spec, Boolean *existed)
{
    Str255 pascalName;
    size_t len;

    if (name == NULL || name[0] == '\0') {
        return paramErr;
    }
    len = strlen(name);
    if (len > 31) {
        return paramErr;            /* the HFS limit; no caller is near it */
    }
    pascalName[0] = (unsigned char)len;
    memcpy(pascalName + 1, name, len);

    return MakeCacheSpecNamed(pascalName, createFolder, spec, existed);
}

static GazetteStoreFile *NewStoreFile(short refNum, int writing)
{
    GazetteStoreFile *f =
        (GazetteStoreFile *)NewPtrClear((Size)sizeof(GazetteStoreFile));

    if (f == NULL) {
        FSClose(refNum);
        return NULL;
    }
    f->refNum  = refNum;
    f->writing = writing;
    return f;
}

GazetteStoreFile *GazetteStoreCacheCreate(const char *feedURL)
{
    FSSpec  spec;
    Boolean existed;
    OSErr   err;
    short   refNum;

    if (feedURL == NULL || feedURL[0] == '\0') {
        return NULL;
    }

    err = MakeCacheSpec(feedURL, true, &spec, &existed);
    if (err != noErr) {
        return NULL;
    }

    if (!existed) {
        err = FSpCreate(&spec, kGazetteCreator, kTextFileType, smSystemScript);
        if (err != noErr && err != dupFNErr) {
            return NULL;
        }
    }

    err = FSpOpenDF(&spec, fsWrPerm, &refNum);
    if (err != noErr) {
        return NULL;
    }
    if (SetEOF(refNum, 0) != noErr) {
        FSClose(refNum);
        return NULL;
    }

    return NewStoreFile(refNum, 1);
}

GazetteStoreFile *GazetteStoreCacheOpen(const char *feedURL)
{
    FSSpec  spec;
    Boolean existed;
    short   refNum;

    if (feedURL == NULL || feedURL[0] == '\0') {
        return NULL;
    }
    if (MakeCacheSpec(feedURL, false, &spec, &existed) != noErr || !existed) {
        return NULL;
    }
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) {
        return NULL;
    }
    return NewStoreFile(refNum, 0);
}

GazetteStoreFile *GazetteStoreDataCreate(const char *name)
{
    FSSpec  spec;
    Boolean existed;
    OSErr   err;
    short   refNum;

    err = MakeDataSpec(name, true, &spec, &existed);
    if (err != noErr) {
        return NULL;
    }
    if (!existed) {
        err = FSpCreate(&spec, kGazetteCreator, kTextFileType, smSystemScript);
        if (err != noErr && err != dupFNErr) {
            return NULL;
        }
    }
    err = FSpOpenDF(&spec, fsWrPerm, &refNum);
    if (err != noErr) {
        return NULL;
    }
    if (SetEOF(refNum, 0) != noErr) {
        FSClose(refNum);
        return NULL;
    }
    return NewStoreFile(refNum, 1);
}

GazetteStoreFile *GazetteStoreDataOpen(const char *name)
{
    FSSpec  spec;
    Boolean existed;
    short   refNum;

    if (MakeDataSpec(name, false, &spec, &existed) != noErr || !existed) {
        return NULL;
    }
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) {
        return NULL;
    }
    return NewStoreFile(refNum, 0);
}

void GazetteStoreCacheDelete(const char *feedURL)
{
    FSSpec  spec;
    Boolean existed;

    if (feedURL == NULL || feedURL[0] == '\0') {
        return;
    }
    if (MakeCacheSpec(feedURL, false, &spec, &existed) == noErr && existed) {
        (void)FSpDelete(&spec);
    }
}

/* ------------------------------------------------------------------ */
/* Writing                                                             */
/* ------------------------------------------------------------------ */

static int FlushWrite(GazetteStoreFile *f)
{
    long count = f->len;

    if (count == 0) {
        return 1;
    }
    if (FSWrite(f->refNum, &count, f->buf) != noErr || count != f->len) {
        return 0;
    }
    f->len = 0;
    return 1;
}

int GazetteStoreWrite(GazetteStoreFile *f, const char *text, long len)
{
    if (f == NULL || !f->writing || text == NULL || len < 0) {
        return 0;
    }

    while (len > 0) {
        long room = (long)sizeof f->buf - f->len;
        long n    = (len < room) ? len : room;

        BlockMoveData(text, f->buf + f->len, n);
        f->len += n;
        text   += n;
        len    -= n;

        if (f->len == (long)sizeof f->buf && !FlushWrite(f)) {
            return 0;
        }
    }
    return 1;
}

int GazetteStoreWriteLine(GazetteStoreFile *f, const char *text)
{
    /* CR, the way an OS 9 text file ends its lines, so the cache opens
       readably in SimpleText like the preferences do. */
    static const char kCR[1] = { '\r' };
    long len;

    if (f == NULL || text == NULL) {
        return 0;
    }
    len = (long)strlen(text);
    if (!GazetteStoreWrite(f, text, len)) {
        return 0;
    }
    return GazetteStoreWrite(f, kCR, 1);
}

/* ------------------------------------------------------------------ */
/* Reading                                                             */
/* ------------------------------------------------------------------ */

/* Next byte, or -1 at end of file. */
static int ReadByte(GazetteStoreFile *f)
{
    if (f->pos >= f->len) {
        long count = (long)sizeof f->buf;
        OSErr err;

        if (f->atEOF) {
            return -1;
        }
        err = FSRead(f->refNum, &count, f->buf);
        if ((err != noErr && err != eofErr) || count <= 0) {
            f->atEOF = 1;
            return -1;
        }
        if (err == eofErr) {
            f->atEOF = 1;
        }
        f->len = count;
        f->pos = 0;
    }
    return (unsigned char)f->buf[f->pos++];
}

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
            /* A file written elsewhere may use CRLF; the LF that follows a CR
               belongs to the same ending, not to the next line. */
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
        /* Past cap the rest of the line is dropped rather than spilling into
           the next one, so a corrupt file cannot desynchronise the reader. */
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
    FSClose(f->refNum);
    DisposePtr((Ptr)f);
}

/* ------------------------------------------------------------------ */
/* Files the user chooses                                              */
/* ------------------------------------------------------------------ */

int GazetteStoreWriteDataFile(const char *name, const char *text, long len)
{
    GazetteStoreFile *f;

    if (name == NULL || text == NULL || len < 0) {
        return 0;
    }
    f = GazetteStoreDataCreate(name);
    if (f == NULL) {
        return 0;
    }
    if (!GazetteStoreWrite(f, text, len)) {
        GazetteStoreClose(f);
        return 0;
    }
    GazetteStoreClose(f);
    return 1;
}

static GazetteStoreIdle gIdle;

/*
 * Why the last chooser failed. Every one of these paths used to return the
 * same 0 the user's Cancel returns, so a real failure looked exactly like
 * changing your mind -- which is how a broken export presented as a menu item
 * that "doesn't seem to do anything".
 */
static char gError[128];

const char *GazetteStoreErrorText(void)
{
    return gError;
}

static int Failed(const char *what, OSErr err)
{
    if (err != noErr) {
        snprintf(gError, sizeof gError, "%s (error %d)", what, (int)err);
    } else {
        snprintf(gError, sizeof gError, "%s", what);
    }
    return kGazetteFileFailed;
}

/*
 * Whether Navigation Services can be called.
 *
 * Not NavServicesAvailable(), which is the wrong test for this application.
 * That macro branches on TARGET_RT_MAC_CFM -- the *binary format* -- and
 * Gazette is a Carbon application in CFM form, which is exactly what a
 * CarbonLib application on Mac OS 9 is. So it takes the classic branch and
 * weak-links NavLibraryVersion out of NavigationLib, a library a Carbon
 * application does not link against; the symbol is unresolved and the answer
 * is a confident "no". Which is how import and export came to do nothing at
 * all, silently, on a machine where Navigation Services was right there.
 *
 * The Availability blocks on NavGetFile and NavPutFile are the authority
 * here, and they both say CarbonLib 1.0 and later. Under Carbon there is
 * nothing to test.
 */
static Boolean GazetteNavAvailable(void)
{
#if TARGET_API_MAC_CARBON
    return true;
#else
    return NavServicesAvailable();
#endif
}

void GazetteStoreSetIdle(GazetteStoreIdle idle)
{
    gIdle = idle;
}

/*
 * Nav runs its own event loop and hands the application every event it does
 * not want itself. Null events are the ones that matter: they are the idle
 * time, and pumping the fetch from here is what keeps a refresh alive while a
 * file is being chosen. See GazetteStoreSetIdle.
 */
static pascal void GazetteNavEvent(NavEventCallbackMessage selector,
                                   NavCBRecPtr parms, void *context)
{
    (void)context;

    if (selector != kNavCBEvent || parms == NULL) {
        return;
    }
    if (parms->eventData.eventDataParms.event == NULL) {
        return;
    }
    if (parms->eventData.eventDataParms.event->what == nullEvent &&
        gIdle != NULL) {
        gIdle();
    }
}

/* The first item of a Nav reply, as an FSSpec. Nav answers with an Apple
   Event descriptor list because it can return several; every dialog here asks
   for one file, so the first is the answer. */
static OSErr FirstReplySpec(const NavReplyRecord *reply, FSSpec *spec)
{
    AEKeyword keyword;
    DescType  type;
    Size      actual;

    return AEGetNthPtr(&reply->selection, 1, typeFSS, &keyword, &type,
                       spec, (Size)sizeof(FSSpec), &actual);
}

/*
 * Turn off the file-kind popup.
 *
 * Nav builds that popup out of the human-readable "kind" strings for the file
 * types on offer, and there is no kind registered for 'TEXT' documents owned
 * by 'Gzt9' -- Gazette's FREF claims the application, not a document type. So
 * NavPutFile fails outright with kNavMissingKindStringErr (-5699) before the
 * dialog ever appears, which is what "the Save dialog could not be shown"
 * was reporting.
 *
 * kNavDefaultNavDlogOptions is 0xE4 and already turns translation items off;
 * the type popup is the one it leaves on. Gazette reads and writes exactly
 * one kind of file, so a popup offering a choice of one was never worth
 * anything here anyway.
 */
static void NoKindStrings(NavDialogOptions *options)
{
    options->dialogOptionFlags |= kNavNoTypePopup;
    options->dialogOptionFlags |= kNavDontAddTranslateItems;
    options->dialogOptionFlags |= kNavDontAutoTranslate;
}

static void SetPrompt(NavDialogOptions *options, const char *prompt)
{
    size_t len;

    if (prompt == NULL) {
        return;
    }
    len = strlen(prompt);
    if (len > 255) {
        len = 255;
    }
    options->message[0] = (unsigned char)len;
    memcpy(options->message + 1, prompt, len);
}

int GazetteStoreAskAndReadFile(const char *prompt, char *buf, long cap,
                               long *outLen)
{
    NavDialogOptions options;
    NavReplyRecord   reply;
    NavEventUPP      eventUPP;
    FSSpec           spec;
    OSErr            err;
    short            refNum;
    long             count;

    if (outLen != NULL) {
        *outLen = 0;
    }
    if (buf == NULL || cap <= 0) {
        return 0;
    }
    buf[0] = '\0';

    gError[0] = '\0';

    if (!GazetteNavAvailable()) {
        return Failed("This system has no Navigation Services.", noErr);
    }
    err = NavGetDefaultDialogOptions(&options);
    if (err != noErr) {
        return Failed("The Open dialog could not be set up.", err);
    }
    NoKindStrings(&options);
    SetPrompt(&options, prompt);

    eventUPP = NewNavEventUPP(GazetteNavEvent);

    /* No type list: an OPML file is 'TEXT' from one editor and something else
       from another, and refusing to show a file the user is pointing at is
       worse than opening one that turns out not to parse. */
    err = NavGetFile(NULL, &reply, &options, eventUPP, NULL, NULL, NULL, NULL);

    if (eventUPP != NULL) {
        DisposeNavEventUPP(eventUPP);
    }
    if (err != noErr) {
        return Failed("The Open dialog could not be shown.", err);
    }
    if (!reply.validRecord) {
        NavDisposeReply(&reply);
        return kGazetteFileCancelled;
    }

    err = FirstReplySpec(&reply, &spec);
    NavDisposeReply(&reply);
    if (err != noErr) {
        return Failed("That file could not be identified.", err);
    }

    err = FSpOpenDF(&spec, fsRdPerm, &refNum);
    if (err != noErr) {
        return Failed("That file could not be opened.", err);
    }
    err = GetEOF(refNum, &count);
    if (err != noErr) {
        FSClose(refNum);
        return Failed("That file could not be measured.", err);
    }
    if (count > cap - 1) {
        count = cap - 1;
    }
    err = FSRead(refNum, &count, buf);
    FSClose(refNum);

    if (err != noErr && err != eofErr) {
        return Failed("That file could not be read.", err);
    }
    buf[count] = '\0';
    if (outLen != NULL) {
        *outLen = count;
    }
    return kGazetteFileDone;
}

int GazetteStoreAskAndWriteFile(const char *prompt, const char *defaultName,
                                const char *text, long len)
{
    NavDialogOptions options;
    NavReplyRecord   reply;
    NavEventUPP      eventUPP;
    FSSpec           spec;
    OSErr            err;
    short            refNum;
    long             count;

    gError[0] = '\0';

    if (text == NULL || len < 0) {
        return Failed("There was nothing to write.", noErr);
    }
    if (!GazetteNavAvailable()) {
        return Failed("This system has no Navigation Services.", noErr);
    }
    err = NavGetDefaultDialogOptions(&options);
    if (err != noErr) {
        return Failed("The Save dialog could not be set up.", err);
    }
    NoKindStrings(&options);
    SetPrompt(&options, prompt);

    if (defaultName != NULL) {
        size_t nameLen = strlen(defaultName);

        if (nameLen > 63) {
            nameLen = 63;
        }
        options.savedFileName[0] = (unsigned char)nameLen;
        memcpy(options.savedFileName + 1, defaultName, nameLen);
    }

    eventUPP = NewNavEventUPP(GazetteNavEvent);

    err = NavPutFile(NULL, &reply, &options, eventUPP, kTextFileType,
                     kSimpleTextCreator, NULL);

    if (eventUPP != NULL) {
        DisposeNavEventUPP(eventUPP);
    }
    if (err != noErr) {
        return Failed("The Save dialog could not be shown.", err);
    }
    if (!reply.validRecord) {
        NavDisposeReply(&reply);
        return kGazetteFileCancelled;
    }

    err = FirstReplySpec(&reply, &spec);
    if (err != noErr) {
        NavDisposeReply(&reply);
        return Failed("That location could not be identified.", err);
    }

    /* replacing is Nav's answer to "the user picked an existing file and
       agreed to replace it"; without the delete, the old contents past the
       new length would survive the write. */
    if (reply.replacing) {
        (void)FSpDelete(&spec);
    }
    /* The same creator the dialog was told about, so what is written is what
       was offered. */
    err = FSpCreate(&spec, kSimpleTextCreator, kTextFileType, smSystemScript);
    if (err != noErr && err != dupFNErr) {
        NavDisposeReply(&reply);
        return Failed("The file could not be created.", err);
    }

    err = FSpOpenDF(&spec, fsWrPerm, &refNum);
    if (err != noErr) {
        NavDisposeReply(&reply);
        return Failed("The file could not be opened for writing.", err);
    }
    (void)SetEOF(refNum, 0);

    count = len;
    err   = FSWrite(refNum, &count, text);
    FSClose(refNum);

    /* Tells Nav the save is finished, which is what lets it hide a file name
       extension and clean up any translation it set up. */
    NavCompleteSave(&reply, kNavTranslateInPlace);
    NavDisposeReply(&reply);

    if (err != noErr) {
        return Failed("The file could not be written.", err);
    }
    return kGazetteFileDone;
}
