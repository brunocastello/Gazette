/*
 * Gazette — a feed through the article path, run by hand on CI
 * Copyright (c) 2026 brunocastello
 *
 * Nothing here is a test. It is what a refresh does with each item's body —
 * the parser keeps it whole, the extractor lays it out as a fragment — driven
 * from the command line, so a feed whose articles read wrong on the Mac can
 * be put through the same code on CI (.github/workflows/probe.yml,
 * tools/probe.sh) and the reader pane's text read off the log. Fed 4 KB at a
 * time, as the Mac's fetch feeds it.
 *
 *   probe feed <file>     every item: its headline, link, pictures and text
 */

#include "extract/gazette_extract.h"
#include "feeds/gazette_feed_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    kChunk   = 4096,            /* what the Mac's fetch hands a sink at a time */
    kBodyMax = 48 * 1024        /* kBodyHTMLMax in gazette_feeds.c */
};

static GazetteFeedParser gParser;
static GazetteExtract    gExtract;
static char              gBody[kBodyMax];
static int               gItems;

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

/* WriteArticleText in gazette_feeds.c, printing where it writes. */
static int Item(const GazetteArticle *a, void *context)
{
    size_t      len;
    const char *html = GazetteFeedParserBody(&gParser, &len);
    int         i;

    (void)context;
    gItems++;
    printf("==== %d. %s\n%s\n", gItems, a->title, a->link);
    if (len == 0) {
        printf("(no body: the pane shows the summary)\n%s\n\n", a->body);
        return 1;
    }
    if (memchr(html, '<', len) == NULL) {
        len = GazetteDecodeEntities(gBody, len);
    }
    GazetteExtractInitFragment(&gExtract);
    (void)GazetteExtractFeed(&gExtract, gBody, len);
    GazetteExtractFinish(&gExtract);
    printf("html %lu bytes; text %lu; photos %d\n", (unsigned long)len,
           (unsigned long)strlen(GazetteExtractText(&gExtract)),
           GazetteExtractPhotoCount(&gExtract));
    for (i = 0; i < GazetteExtractPhotoCount(&gExtract); i++) {
        printf("photo %d: %s\n", i + 1, GazetteExtractPhoto(&gExtract, i)->url);
    }
    printf("----\n");
    PrintText(GazetteExtractText(&gExtract));
    putchar('\n');
    return 1;
}

int main(int argc, char **argv)
{
    char   buf[kChunk];
    size_t n;
    FILE  *f;

    if (argc != 3 || strcmp(argv[1], "feed") != 0) {
        fprintf(stderr, "usage: probe feed <file>\n");
        return 2;
    }
    f = fopen(argv[2], "rb");
    if (f == NULL) {
        fprintf(stderr, "probe: cannot open %s\n", argv[2]);
        return 2;
    }
    GazetteFeedParserInit(&gParser, Item, NULL);
    GazetteFeedParserSetBodyBuffer(&gParser, gBody, sizeof gBody);
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        if (!GazetteFeedParserFeed(&gParser, buf, n)) {
            break;
        }
    }
    fclose(f);
    GazetteFeedParserFinish(&gParser);
    printf("%s: %d items\n", GazetteFeedParserTitle(&gParser), gItems);
    return gItems > 0 ? 0 : 1;
}
