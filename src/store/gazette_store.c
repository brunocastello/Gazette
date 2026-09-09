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
#include <Script.h>      /* smSystemScript */

/* Pascal string, so it can go straight to FSMakeFSSpec. */
static const unsigned char kPrefsFileName[] = "\pGazette Preferences";

enum {
    kGazetteCreator = 'Gzt9',       /* must match CREATOR in CMakeLists.txt */
    kTextFileType   = 'TEXT'
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
