/*
 * Gazette — the article path, run by hand on CI
 * Copyright (c) 2026 brunocastello
 *
 * Nothing here is a test. It is the full-text path's portable half — the
 * Google News resolver and the extractor — driven from the command line,
 * so a page that reads wrong on the Mac can be put through the same code
 * on CI (.github/workflows/probe.yml, tools/probe.sh) and the reader
 * pane's text read off the log. The fetching is curl's; everything that
 * decides what the text is, is Gazette's own, fed the way the Mac feeds
 * it: 4 KB at a time, stopping when a sink says so.
 *
 *   probe token   <url>                 the story token, or nothing
 *   probe scan    <file>                data-n-a-ts and data-n-a-sg
 *   probe body    <token> <ts> <sig>    batchexecute's form body
 *   probe answer  <file>                the story's address
 *   probe extract <file>                the reader pane's text
 */

#include "extract/gazette_extract.h"
#include "feeds/gazette_googlenews.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kChunk = 4096 };     /* what the Mac's fetch hands a sink at a time */

static FILE *OpenOrDie(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        fprintf(stderr, "probe: cannot open %s\n", path);
        exit(2);
    }
    return f;
}

static int Scan(const char *path)
{
    static GazetteGNewsScan s;
    char   buf[kChunk];
    size_t n;
    long   read = 0;
    FILE  *f = OpenOrDie(path);

    GazetteGNewsScanInit(&s);
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        read += (long)n;
        if (!GazetteGNewsScanFeed(&s, buf, n)) {
            break;
        }
    }
    fclose(f);
    printf("read %ld bytes; done=%d\n", read, GazetteGNewsScanDone(&s));
    printf("ts=%s\nsig=%s\n", s.ts, s.sig);
    return GazetteGNewsScanDone(&s) ? 0 : 1;
}

static int Answer(const char *path)
{
    /* The Mac keeps the first 4095 bytes of the answer and no more. */
    static char answer[4096];
    char   url[1024];
    size_t n;
    FILE  *f = OpenOrDie(path);

    n = fread(answer, 1, sizeof answer - 1, f);
    fclose(f);
    if (!GazetteGNewsParseAnswer(answer, n, url, sizeof url)) {
        printf("no address in the answer\n");
        return 1;
    }
    printf("%s\n", url);
    return 0;
}

/* The marks the extractor leaves for the reader pane, made visible. */
static void PrintText(const char *s)
{
    for (; *s != '\0'; s++) {
        switch ((unsigned char)*s) {
            case kGazettePhotoMarker:   fputs("[PHOTO]", stdout); break;
            case kGazetteMarkHeading:   fputs("[H] ", stdout);    break;
            case kGazetteMarkListItem:  fputs("* ", stdout);      break;
            case kGazetteMarkQuote:     fputs("> ", stdout);      break;
            case kGazetteMarkBoldOn:
            case kGazetteMarkBoldOff:   fputs("**", stdout);      break;
            case kGazetteMarkItalicOn:
            case kGazetteMarkItalicOff: fputs("_", stdout);       break;
            case kGazetteMarkLinkOn:
            case kGazetteMarkLinkOff:                             break;
            case '\n':                  fputs("\n\n", stdout);    break;
            default:                    putchar(*s);              break;
        }
    }
    putchar('\n');
}

static int Extract(const char *path)
{
    GazetteExtract *e = (GazetteExtract *)calloc(1, sizeof *e);
    char   buf[kChunk];
    size_t n, len;
    long   read = 0;
    int    usable, i;
    FILE  *f = OpenOrDie(path);

    GazetteExtractInit(e);
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        read += (long)n;
        if (!GazetteExtractFeed(e, buf, n)) {
            break;                  /* the Mac stops the fetch here */
        }
    }
    fclose(f);

    len    = GazetteExtractFinish(e);
    usable = GazetteExtractUsable(e);
    printf("read %ld bytes; text %lu; usable=%d; fell back to description=%d;"
           " photos %d\n", read, (unsigned long)len, usable, e->fellBack,
           GazetteExtractPhotoCount(e));
    for (i = 0; i < GazetteExtractPhotoCount(e); i++) {
        printf("photo %d: %s\n", i + 1, GazetteExtractPhoto(e, i)->url);
    }
    printf("----\n");
    PrintText(GazetteExtractText(e));
    free(e);
    return usable ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "token") == 0) {
        char token[kGazetteGNewsTokenMax];

        if (!GazetteGNewsArticleToken(argv[2], token, sizeof token)) {
            return 1;
        }
        printf("%s\n", token);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "scan") == 0) {
        return Scan(argv[2]);
    }
    if (argc == 5 && strcmp(argv[1], "body") == 0) {
        char body[kGazetteGNewsBodyMax];

        if (GazetteGNewsBuildBody(argv[2], argv[3], argv[4], body,
                                  sizeof body) == 0) {
            fprintf(stderr, "probe: body does not fit\n");
            return 1;
        }
        printf("%s\n", body);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "answer") == 0) {
        return Answer(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "extract") == 0) {
        return Extract(argv[2]);
    }
    fprintf(stderr, "usage: probe token|scan|body|answer|extract ...\n");
    return 2;
}
