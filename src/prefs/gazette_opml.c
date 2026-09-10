/*
 * Gazette — OPML import and export
 * Copyright (c) 2026 brunocastello
 *
 * See gazette_opml.h.
 */

#include "prefs/gazette_opml.h"

#include "extract/gazette_extract.h"    /* GazetteHtmlAttr */
#include "portable/gazette_portable.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Writing                                                             */
/* ------------------------------------------------------------------ */

static void Append(char *out, size_t cap, size_t *len, const char *text)
{
    size_t n = strlen(text);

    if (*len + n >= cap) {
        *len = cap;                 /* overflowed; the caller checks */
        return;
    }
    memcpy(out + *len, text, n);
    *len += n;
}

/*
 * An attribute value, with the five characters XML reserves turned into
 * entities. A feed title routinely carries an ampersand and a URL routinely
 * carries one too — writing either raw produces a file that other readers
 * refuse, which is the one way an export can fail silently.
 */
static void AppendEscaped(char *out, size_t cap, size_t *len, const char *text)
{
    for (; *text != '\0'; text++) {
        switch (*text) {
            case '&':  Append(out, cap, len, "&amp;");  break;
            case '<':  Append(out, cap, len, "&lt;");   break;
            case '>':  Append(out, cap, len, "&gt;");   break;
            case '"':  Append(out, cap, len, "&quot;"); break;
            case '\'': Append(out, cap, len, "&apos;"); break;
            default:
                if (*len + 1 >= cap) {
                    *len = cap;
                    return;
                }
                out[(*len)++] = *text;
                break;
        }
    }
}

static void AppendFeed(char *out, size_t cap, size_t *len,
                       const GazetteFeedPref *feed, int indented)
{
    Append(out, cap, len, indented ? "      <outline type=\"rss\" text=\""
                                   : "    <outline type=\"rss\" text=\"");
    AppendEscaped(out, cap, len, feed->title);
    Append(out, cap, len, "\" title=\"");
    AppendEscaped(out, cap, len, feed->title);
    Append(out, cap, len, "\" xmlUrl=\"");
    AppendEscaped(out, cap, len, feed->url);

    /*
     * A feed switched off is still a subscription, and OPML has nowhere
     * standard to say otherwise, so it is exported like any other. The
     * alternative — leaving it out — would quietly lose it on a round trip
     * through another reader.
     */
    Append(out, cap, len, "\"/>\r");
}

size_t GazetteOPMLWrite(const GazettePrefs *p, char *out, size_t cap)
{
    size_t len = 0;
    int    g;
    int    i;

    if (p == NULL || out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';

    Append(out, cap, &len, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r");
    Append(out, cap, &len, "<opml version=\"1.1\">\r");
    Append(out, cap, &len, "  <head>\r");
    Append(out, cap, &len, "    <title>Gazette Feeds</title>\r");
    Append(out, cap, &len, "  </head>\r");
    Append(out, cap, &len, "  <body>\r");

    /* Top-level feeds first, then each group's — the sidebar's order, which
       is the order the feeds array is already held in. */
    for (i = 0; i < p->feedCount; i++) {
        if (p->feeds[i].group < 0) {
            AppendFeed(out, cap, &len, &p->feeds[i], 0);
        }
    }

    for (g = 0; g < p->groupCount; g++) {
        Append(out, cap, &len, "    <outline text=\"");
        AppendEscaped(out, cap, &len, p->groups[g].name);
        Append(out, cap, &len, "\" title=\"");
        AppendEscaped(out, cap, &len, p->groups[g].name);
        Append(out, cap, &len, "\">\r");

        for (i = 0; i < p->feedCount; i++) {
            if (p->feeds[i].group == g) {
                AppendFeed(out, cap, &len, &p->feeds[i], 1);
            }
        }
        Append(out, cap, &len, "    </outline>\r");
    }

    Append(out, cap, &len, "  </body>\r");
    Append(out, cap, &len, "</opml>\r");

    if (len >= cap) {
        out[0] = '\0';
        return 0;
    }
    out[len] = '\0';
    return len;
}

/* ------------------------------------------------------------------ */
/* Reading                                                             */
/* ------------------------------------------------------------------ */

/* Copy an attribute value out, undoing the five entities the writer makes.
   Numeric references are left alone: nothing writes them into an xmlUrl, and
   a URL is the one field where guessing wrong would break a subscription. */
static void CopyUnescaped(char *dst, size_t cap, const char *src, size_t len)
{
    size_t out = 0;
    size_t i;

    if (cap == 0) {
        return;
    }
    for (i = 0; i < len && out + 1 < cap; i++) {
        if (src[i] == '&') {
            size_t rest = len - i;

            if (rest >= 5 && gz_starts_ci(src + i, rest, "&amp;")) {
                dst[out++] = '&'; i += 4; continue;
            }
            if (rest >= 4 && gz_starts_ci(src + i, rest, "&lt;")) {
                dst[out++] = '<'; i += 3; continue;
            }
            if (rest >= 4 && gz_starts_ci(src + i, rest, "&gt;")) {
                dst[out++] = '>'; i += 3; continue;
            }
            if (rest >= 6 && gz_starts_ci(src + i, rest, "&quot;")) {
                dst[out++] = '"'; i += 5; continue;
            }
            if (rest >= 6 && gz_starts_ci(src + i, rest, "&apos;")) {
                dst[out++] = '\''; i += 5; continue;
            }
        }
        dst[out++] = src[i];
    }
    dst[out] = '\0';
}

/* The group of this name, adding it when there is none. -1 when the group
   list is full, which puts the feed at the top level rather than losing it. */
static int GroupNamed(GazettePrefs *p, const char *name)
{
    int g;

    if (name == NULL || name[0] == '\0') {
        return -1;
    }
    for (g = 0; g < p->groupCount; g++) {
        if (gz_stricmp(p->groups[g].name, name) == 0) {
            return g;
        }
    }
    return GazettePrefsAddGroup(p, name);
}

int GazetteOPMLParse(const char *text, size_t len, GazettePrefs *p)
{
    size_t at    = 0;
    int    added = 0;
    int    depth = 0;            /* how deep in <outline> elements we are */
    int    group = -1;           /* the group opened at depth 1, or -1    */

    if (text == NULL || p == NULL) {
        return 0;
    }

    while (at < len) {
        const char *tag;
        size_t      tagLen;
        size_t      end;
        int         closing;
        int         selfClosing;
        char        name[16];
        const char *value;
        size_t      valueLen;

        if (text[at] != '<') {
            at++;
            continue;
        }

        end = at + 1;
        while (end < len && text[end] != '>') {
            end++;
        }
        if (end >= len) {
            break;                  /* an unterminated tag ends the file */
        }

        tag    = text + at + 1;
        tagLen = end - at - 1;
        at     = end + 1;

        closing     = (tagLen > 0 && tag[0] == '/');
        selfClosing = (tagLen > 0 && tag[tagLen - 1] == '/');

        GazetteHtmlTagName(tag, tagLen, name, sizeof name);
        if (strcmp(name, "outline") != 0) {
            continue;
        }

        if (closing) {
            if (depth > 0) {
                depth--;
            }
            if (depth == 0) {
                group = -1;         /* out of the folder it opened */
            }
            continue;
        }

        /*
         * An outline with an xmlUrl is a feed; one without is a folder. That
         * is the whole of the distinction, and every reader that writes OPML
         * agrees on it.
         */
        if (GazetteHtmlAttr(tag, tagLen, "xmlUrl", &value, &valueLen)) {
            char url[kGazetteURLLen];
            char title[kGazetteTitleLen];

            CopyUnescaped(url, sizeof url, value, valueLen);

            title[0] = '\0';
            if (GazetteHtmlAttr(tag, tagLen, "title", &value, &valueLen) ||
                GazetteHtmlAttr(tag, tagLen, "text", &value, &valueLen)) {
                CopyUnescaped(title, sizeof title, value, valueLen);
            }

            if (url[0] != '\0' && GazettePrefsFindFeed(p, url) < 0 &&
                GazettePrefsAddFeed(p, url, title, group) >= 0) {
                added++;
            }
        } else if (depth == 0 && !selfClosing) {
            char title[kGazetteGroupLen];

            title[0] = '\0';
            if (GazetteHtmlAttr(tag, tagLen, "title", &value, &valueLen) ||
                GazetteHtmlAttr(tag, tagLen, "text", &value, &valueLen)) {
                CopyUnescaped(title, sizeof title, value, valueLen);
            }
            group = GroupNamed(p, title);
        }

        if (!selfClosing) {
            depth++;
        }
    }

    return added;
}
