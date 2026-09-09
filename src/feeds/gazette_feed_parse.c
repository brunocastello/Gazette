/*
 * Gazette — incremental RSS 2.0 / Atom parser and feed auto-discovery
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: no system headers. See gazette_feed_parse.h.
 */

#include "feeds/gazette_feed_parse.h"

#include "portable/gazette_portable.h"

#include <string.h>

/* Scanner states. */
enum {
    kStateText = 0,
    kStateTag,
    kStateComment,
    kStateCData
};

/* Parser modes. */
enum {
    kModeFeed = 0,
    kModeDiscovery
};

/* Which field the captured text belongs to. 0 means "not capturing". */
enum {
    kFieldNone = 0,
    kFieldFeedTitle,
    kFieldTitle,
    kFieldLink,
    kFieldDate,
    kFieldSource,
    kFieldBody,         /* description / summary — taken only if none yet */
    kFieldBodyRich      /* content / content:encoded — always wins        */
};

/* ------------------------------------------------------------------ */
/* Entities                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    const char   *name;
    unsigned long cp;
} GzEntity;

/*
 * The five XML entities, plus the HTML ones that turn up in feeds because
 * publishers escape prose written for a web page. Anything else is left as
 * written: an unknown "&foo;" is far more likely to be literal text than a
 * reference, and dropping it would silently eat content.
 */
static const GzEntity kEntities[] = {
    { "amp",    38    }, { "lt",     60    }, { "gt",     62    },
    { "quot",   34    }, { "apos",   39    },
    { "nbsp",   0x00A0}, { "ndash",  0x2013}, { "mdash",  0x2014},
    { "lsquo",  0x2018}, { "rsquo",  0x2019},
    { "ldquo",  0x201C}, { "rdquo",  0x201D},
    { "hellip", 0x2026}, { "bull",   0x2022}, { "middot", 0x00B7},
    { "copy",   0x00A9}, { "reg",    0x00AE}, { "trade",  0x2122},
    { "deg",    0x00B0}, { "eacute", 0x00E9}, { "egrave", 0x00E8},
    { "agrave", 0x00E0}, { "ccedil", 0x00E7}, { "ntilde", 0x00F1},
    { "uuml",   0x00FC}, { "ouml",   0x00F6}, { "auml",   0x00E4},
    { "szlig",  0x00DF}, { "aacute", 0x00E1}, { "iacute", 0x00ED},
    { "oacute", 0x00F3}, { "uacute", 0x00FA}
};

/* Write cp as UTF-8 into out (at least 4 bytes). Returns bytes written.
   The transliterator downstream expects UTF-8, so a numeric reference has to
   become one rather than a raw byte. */
static size_t EncodeUTF8(unsigned long cp, char *out)
{
    if (cp < 0x80UL) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800UL) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000UL) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

size_t GazetteDecodeEntities(char *s, size_t len)
{
    size_t in  = 0;
    size_t out = 0;

    if (s == NULL) {
        return 0;
    }

    while (in < len) {
        size_t semi;
        size_t nameLen;

        if (s[in] != '&') {
            s[out++] = s[in++];
            continue;
        }

        /* Find the ';'. A reference is short; anything longer is prose that
           happens to contain an ampersand. */
        semi = in + 1;
        while (semi < len && semi - in <= 12 && s[semi] != ';') {
            semi++;
        }
        if (semi >= len || s[semi] != ';') {
            s[out++] = s[in++];
            continue;
        }
        nameLen = semi - in - 1;
        if (nameLen == 0) {
            s[out++] = s[in++];
            continue;
        }

        if (s[in + 1] == '#') {
            unsigned long cp = 0;
            size_t        i;
            int           hex = (nameLen > 1 &&
                                 (s[in + 2] == 'x' || s[in + 2] == 'X'));
            size_t        start = in + 2 + (hex ? 1 : 0);
            int           any = 0;

            for (i = start; i < semi; i++) {
                int d;
                char c = s[i];

                if (c >= '0' && c <= '9')      d = c - '0';
                else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else { any = 0; break; }

                cp = cp * (hex ? 16UL : 10UL) + (unsigned long)d;
                any = 1;
                if (cp > 0x10FFFFUL) { any = 0; break; }
            }

            if (any && cp != 0) {
                out += EncodeUTF8(cp, s + out);
                in = semi + 1;
                continue;
            }
            s[out++] = s[in++];
            continue;
        }

        {
            size_t i;
            int    matched = 0;

            for (i = 0; i < sizeof kEntities / sizeof kEntities[0]; i++) {
                size_t klen = strlen(kEntities[i].name);

                if (klen == nameLen &&
                    strncmp(s + in + 1, kEntities[i].name, klen) == 0) {
                    out += EncodeUTF8(kEntities[i].cp, s + out);
                    in = semi + 1;
                    matched = 1;
                    break;
                }
            }
            if (!matched) {
                s[out++] = s[in++];
            }
        }
    }

    s[out] = '\0';
    return out;
}

size_t GazetteStripMarkup(char *s, size_t len)
{
    size_t in  = 0;
    size_t out = 0;
    int    depth = 0;

    if (s == NULL) {
        return 0;
    }

    while (in < len) {
        char c = s[in++];

        if (c == '<') {
            depth++;
        } else if (c == '>') {
            if (depth > 0) {
                depth--;
                /* A tag becomes a space, not nothing: "a<br>b" is two words. */
                if (depth == 0 && out > 0 && s[out - 1] != ' ') {
                    s[out++] = ' ';
                }
            } else {
                s[out++] = c;
            }
        } else if (depth == 0) {
            s[out++] = c;
        }
    }

    s[out] = '\0';
    return out;
}

/* ------------------------------------------------------------------ */
/* Dates                                                              */
/* ------------------------------------------------------------------ */

static const char *const kMonths[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/*
 * Days since 1970-01-01 from a civil date, by Howard Hinnant's algorithm.
 * Correct for any proleptic Gregorian date and needs no table, which matters
 * here only because it is easier to be sure of than a table would be.
 */
static long DaysFromCivil(long y, long m, long d)
{
    long era, yoe, doy, doe;

    y -= (m <= 2);
    era = ((y >= 0) ? y : y - 399) / 400;
    yoe = y - era * 400;                                /* [0, 399] */
    doy = (153 * (m + ((m > 2) ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;        /* [0, 146096] */
    return era * 146097 + doe - 719468;
}

static int MonthFromName(const char *s)
{
    int i;

    for (i = 0; i < 12; i++) {
        if (gz_strnicmp(s, kMonths[i], 3) == 0) {
            return i + 1;
        }
    }
    return 0;
}

static int IsDigit(int c) { return c >= '0' && c <= '9'; }

static long TwoDigits(const char *s) { return (s[0] - '0') * 10 + (s[1] - '0'); }

/*
 * ISO 8601 as Atom writes it: 2026-09-07T21:30:00Z, or with a numeric offset,
 * or with fractional seconds. Only the shapes feeds actually produce.
 */
static long ParseISO8601(const char *s, size_t len)
{
    long year, mon, day, hh = 0, mm = 0, ss = 0;
    long offset = 0;
    size_t i;

    if (len < 10) {
        return 0;
    }
    if (!IsDigit(s[0]) || !IsDigit(s[1]) || !IsDigit(s[2]) || !IsDigit(s[3]) ||
        s[4] != '-' || !IsDigit(s[5]) || !IsDigit(s[6]) ||
        s[7] != '-' || !IsDigit(s[8]) || !IsDigit(s[9])) {
        return 0;
    }

    year = (s[0] - '0') * 1000L + (s[1] - '0') * 100L +
           (s[2] - '0') * 10L + (s[3] - '0');
    mon  = TwoDigits(s + 5);
    day  = TwoDigits(s + 8);

    if (len >= 19 && (s[10] == 'T' || s[10] == 't' || s[10] == ' ') &&
        IsDigit(s[11]) && IsDigit(s[12]) && s[13] == ':' &&
        IsDigit(s[14]) && IsDigit(s[15]) && s[16] == ':' &&
        IsDigit(s[17]) && IsDigit(s[18])) {
        hh = TwoDigits(s + 11);
        mm = TwoDigits(s + 14);
        ss = TwoDigits(s + 17);

        /* Skip fractional seconds, then read the zone. */
        i = 19;
        if (i < len && s[i] == '.') {
            i++;
            while (i < len && IsDigit(s[i])) i++;
        }
        if (i < len && (s[i] == '+' || s[i] == '-') && i + 2 < len &&
            IsDigit(s[i + 1]) && IsDigit(s[i + 2])) {
            long sign = (s[i] == '-') ? -1 : 1;
            long oh   = TwoDigits(s + i + 1);
            long om   = 0;

            if (i + 5 < len && s[i + 3] == ':' &&
                IsDigit(s[i + 4]) && IsDigit(s[i + 5])) {
                om = TwoDigits(s + i + 4);
            } else if (i + 4 < len && IsDigit(s[i + 3]) && IsDigit(s[i + 4])) {
                om = TwoDigits(s + i + 3);
            }
            offset = sign * (oh * 3600L + om * 60L);
        }
    }

    if (mon < 1 || mon > 12 || day < 1 || day > 31) {
        return 0;
    }
    return DaysFromCivil(year, mon, day) * 86400L
           + hh * 3600L + mm * 60L + ss - offset;
}

/*
 * RFC 822 as RSS writes it: "Sun, 07 Sep 2026 21:30:00 GMT". The day name is
 * optional, the day number may be one digit, the year may be two, and the
 * zone may be named or numeric.
 */
static long ParseRFC822(const char *s, size_t len)
{
    size_t i = 0;
    long   day, mon, year, hh = 0, mm = 0, ss = 0, offset = 0;

    /* An optional day name, which carries no information. */
    while (i < len && s[i] == ' ') i++;
    if (i + 3 < len && !IsDigit(s[i])) {
        while (i < len && s[i] != ',' && s[i] != ' ') i++;
        if (i < len && s[i] == ',') i++;
    }
    while (i < len && s[i] == ' ') i++;

    if (i >= len || !IsDigit(s[i])) {
        return 0;
    }
    day = s[i++] - '0';
    if (i < len && IsDigit(s[i])) {
        day = day * 10 + (s[i++] - '0');
    }

    while (i < len && s[i] == ' ') i++;
    if (i + 2 >= len) {
        return 0;
    }
    mon = MonthFromName(s + i);
    if (mon == 0) {
        return 0;
    }
    while (i < len && s[i] != ' ') i++;
    while (i < len && s[i] == ' ') i++;

    if (i >= len || !IsDigit(s[i])) {
        return 0;
    }
    year = 0;
    {
        size_t start = i;
        while (i < len && IsDigit(s[i])) {
            year = year * 10 + (s[i++] - '0');
        }
        /* RFC 822 allowed two-digit years and old feeds still emit them. */
        if (i - start == 2) {
            year += (year < 70) ? 2000 : 1900;
        }
    }

    while (i < len && s[i] == ' ') i++;
    if (i + 4 < len && IsDigit(s[i]) && IsDigit(s[i + 1]) && s[i + 2] == ':') {
        hh = TwoDigits(s + i);
        mm = TwoDigits(s + i + 3);
        i += 5;
        if (i + 2 < len && s[i] == ':' && IsDigit(s[i + 1]) && IsDigit(s[i + 2])) {
            ss = TwoDigits(s + i + 1);
            i += 3;
        }
    }

    while (i < len && s[i] == ' ') i++;
    if (i < len) {
        if ((s[i] == '+' || s[i] == '-') && i + 4 < len) {
            long sign = (s[i] == '-') ? -1 : 1;
            offset = sign * (TwoDigits(s + i + 1) * 3600L +
                             TwoDigits(s + i + 3) * 60L);
        } else if (gz_strnicmp(s + i, "EST", 3) == 0) offset = -5  * 3600L;
        else if (gz_strnicmp(s + i, "EDT", 3) == 0)   offset = -4  * 3600L;
        else if (gz_strnicmp(s + i, "CST", 3) == 0)   offset = -6  * 3600L;
        else if (gz_strnicmp(s + i, "CDT", 3) == 0)   offset = -5  * 3600L;
        else if (gz_strnicmp(s + i, "MST", 3) == 0)   offset = -7  * 3600L;
        else if (gz_strnicmp(s + i, "MDT", 3) == 0)   offset = -6  * 3600L;
        else if (gz_strnicmp(s + i, "PST", 3) == 0)   offset = -8  * 3600L;
        else if (gz_strnicmp(s + i, "PDT", 3) == 0)   offset = -7  * 3600L;
        /* GMT, UT and UTC are all zero, which is already the default. */
    }

    if (day < 1 || day > 31) {
        return 0;
    }
    return DaysFromCivil(year, mon, day) * 86400L
           + hh * 3600L + mm * 60L + ss - offset;
}

long GazetteParseDate(const char *s, size_t len)
{
    size_t trimmed;

    if (s == NULL) {
        return 0;
    }
    s = gz_trim(s, len, &trimmed);
    if (trimmed == 0) {
        return 0;
    }

    /* An ISO date starts with four digits and a dash; nothing in RFC 822
       does, so one look is enough to choose. */
    if (trimmed >= 5 && IsDigit(s[0]) && IsDigit(s[1]) &&
        IsDigit(s[2]) && IsDigit(s[3]) && s[4] == '-') {
        return ParseISO8601(s, trimmed);
    }
    return ParseRFC822(s, trimmed);
}

/* The inverse of DaysFromCivil, by the same algorithm. */
static void CivilFromDays(long z, long *y, long *m, long *d)
{
    long era, doe, yoe, doy, mp, year;

    z += 719468;
    era = ((z >= 0) ? z : z - 146096) / 146097;
    doe = z - era * 146097;                                     /* [0, 146096] */
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0, 399]   */
    year = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);              /* [0, 365]   */
    mp  = (5 * doy + 2) / 153;                                  /* [0, 11]    */

    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + ((mp < 10) ? 3 : -9);
    *y = year + (*m <= 2);
}

size_t GazetteFormatDate(long seconds, long nowSeconds, char *out, size_t cap)
{
    long days, secs, y, m, d;
    int  showYear;
    size_t len = 0;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (seconds == 0 || cap < 13) {
        return 0;
    }

    /* Floor division, so dates before 1970 do not round towards zero and land
       on the wrong day. Feeds do carry them, usually by accident. */
    days = seconds / 86400L;
    secs = seconds % 86400L;
    if (secs < 0) {
        secs += 86400L;
        days--;
    }

    CivilFromDays(days, &y, &m, &d);
    if (m < 1 || m > 12) {
        return 0;
    }

    showYear = (nowSeconds != 0) &&
               (nowSeconds - seconds > 180L * 86400L ||
                seconds - nowSeconds > 180L * 86400L);

    memcpy(out, kMonths[m - 1], 3);
    len = 3;
    out[len++] = ' ';
    out[len++] = (char)('0' + (d / 10) % 10);
    out[len++] = (char)('0' + d % 10);
    out[len++] = ' ';

    if (showYear) {
        out[len++] = (char)('0' + (y / 1000) % 10);
        out[len++] = (char)('0' + (y / 100) % 10);
        out[len++] = (char)('0' + (y / 10) % 10);
        out[len++] = (char)('0' + y % 10);
    } else {
        long hh = secs / 3600L;
        long mm = (secs / 60L) % 60L;

        out[len++] = (char)('0' + (hh / 10) % 10);
        out[len++] = (char)('0' + hh % 10);
        out[len++] = ':';
        out[len++] = (char)('0' + (mm / 10) % 10);
        out[len++] = (char)('0' + mm % 10);
    }

    out[len] = '\0';
    return len;
}

/* ------------------------------------------------------------------ */
/* Capture                                                            */
/* ------------------------------------------------------------------ */

static void CaptureBegin(GazetteFeedParser *p, int field)
{
    p->capturing        = field;
    p->captureLen       = 0;
    p->captureTruncated = 0;
}

static void CaptureByte(GazetteFeedParser *p, char c)
{
    if (p->captureLen + 1 < sizeof p->capture) {
        p->capture[p->captureLen++] = c;
    } else {
        p->captureTruncated = 1;
    }
}

/*
 * Turn what was captured into something Mac OS 9 can draw: decode entities to
 * UTF-8, strip whatever markup that revealed, transliterate to ASCII, flatten
 * whitespace. Order matters -- entities first, because "&lt;b&gt;" is not
 * markup until it is decoded, and transliteration last, because it is what
 * turns a decoded U+2019 into an apostrophe.
 */
/* Run the pipeline and leave the result in p->scratch, returning its length.
   Split out from CaptureFinish so a caller that has to inspect the text
   before deciding where it goes does not need a buffer of its own -- which on
   this stack, several frames inside the fetch pump, is worth avoiding. */
static size_t CaptureProcess(GazetteFeedParser *p)
{
    size_t len;

    p->capture[p->captureLen] = '\0';

    len = GazetteDecodeEntities(p->capture, p->captureLen);
    len = GazetteStripMarkup(p->capture, len);
    len = gz_utf8_to_ascii(p->capture, len, p->scratch, sizeof p->scratch);
    return gz_flatten_ws(p->scratch, len);
}

static void CaptureFinish(GazetteFeedParser *p, char *out, size_t cap)
{
    size_t len = CaptureProcess(p);

    gz_copy_n(out, cap, p->scratch, len);
}

/* ------------------------------------------------------------------ */
/* Tag helpers                                                        */
/* ------------------------------------------------------------------ */

/* The element name in tag[], with any namespace prefix dropped, lowercased
   into out. Namespaces are ignored on purpose: <atom:link> and <link> mean
   the same thing to this parser, and resolving prefixes properly would buy
   nothing a feed reader can use. */
static void TagName(const char *tag, size_t len, char *out, size_t cap)
{
    size_t start = 0;
    size_t end;
    size_t colon;
    size_t i;

    if (start < len && tag[start] == '/') {
        start++;
    }
    end = start;
    while (end < len && tag[end] != ' ' && tag[end] != '\t' &&
           tag[end] != '\r' && tag[end] != '\n' && tag[end] != '/') {
        end++;
    }

    colon = start;
    for (i = start; i < end; i++) {
        if (tag[i] == ':') {
            colon = i + 1;
        }
    }

    len = end - colon;
    if (len > cap - 1) {
        len = cap - 1;
    }
    for (i = 0; i < len; i++) {
        out[i] = (char)gz_lower((unsigned char)tag[colon + i]);
    }
    out[len] = '\0';
}

/*
 * Find an attribute's value. Returns 1 and fills out on success. Values may
 * be single- or double-quoted; unquoted values are not accepted, because the
 * only attributes read here (href, rel, type) are quoted everywhere it
 * matters and accepting bare ones invites mis-parsing a malformed tag.
 */
static int TagAttr(const char *tag, size_t len, const char *name,
                   char *out, size_t cap)
{
    size_t nlen = strlen(name);
    size_t i    = 0;

    if (cap > 0) {
        out[0] = '\0';
    }

    /* Skip the element name. */
    while (i < len && tag[i] != ' ' && tag[i] != '\t' &&
           tag[i] != '\r' && tag[i] != '\n') {
        i++;
    }

    while (i < len) {
        size_t nameStart;
        size_t nameEnd;

        while (i < len && (tag[i] == ' ' || tag[i] == '\t' ||
                           tag[i] == '\r' || tag[i] == '\n')) {
            i++;
        }
        if (i >= len) {
            break;
        }

        nameStart = i;
        while (i < len && tag[i] != '=' && tag[i] != ' ' && tag[i] != '\t' &&
               tag[i] != '\r' && tag[i] != '\n' && tag[i] != '/') {
            i++;
        }
        nameEnd = i;

        while (i < len && (tag[i] == ' ' || tag[i] == '\t')) i++;
        if (i >= len || tag[i] != '=') {
            continue;                   /* a valueless attribute */
        }
        i++;
        while (i < len && (tag[i] == ' ' || tag[i] == '\t')) i++;
        if (i >= len || (tag[i] != '"' && tag[i] != '\'')) {
            continue;
        }

        {
            char   quote = tag[i++];
            size_t valStart = i;

            while (i < len && tag[i] != quote) {
                i++;
            }
            if (nameEnd - nameStart == nlen &&
                gz_strnicmp(tag + nameStart, name, nlen) == 0) {
                gz_copy_n(out, cap, tag + valStart, i - valStart);
                return 1;
            }
            if (i < len) {
                i++;                    /* past the closing quote */
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Element handling                                                   */
/* ------------------------------------------------------------------ */

static void EmitArticle(GazetteFeedParser *p)
{
    /* An entry with neither a title nor a link is not an article; feeds do
       carry such things, usually as an artefact of a template. */
    if (p->article.title[0] == '\0' && p->article.link[0] == '\0') {
        return;
    }

    p->articleCount++;
    if (p->sink != NULL && !p->sink(&p->article, p->context)) {
        p->stopped = 1;
    }
}

static void StartElement(GazetteFeedParser *p, const char *tag, size_t len,
                         const char *name, int selfClosing)
{
    char attr[kGazetteArticleLinkLen];

    if (p->mode == kModeDiscovery) {
        if (strcmp(name, "link") == 0 && !p->sawDiscovery &&
            TagAttr(tag, len, "type", attr, sizeof attr)) {
            if (gz_stricmp(attr, "application/rss+xml") == 0 ||
                gz_stricmp(attr, "application/atom+xml") == 0 ||
                gz_stricmp(attr, "application/xml") == 0 ||
                gz_stricmp(attr, "text/xml") == 0) {
                char rel[32];

                /* rel="alternate" is what a feed link carries. Some pages
                   omit it; none of the other rel values name a feed. */
                if (!TagAttr(tag, len, "rel", rel, sizeof rel) ||
                    gz_stricmp(rel, "alternate") == 0) {
                    if (TagAttr(tag, len, "href", attr, sizeof attr) &&
                        attr[0] != '\0') {
                        gz_copy_n(p->discovered, sizeof p->discovered,
                                  attr, strlen(attr));
                        p->sawDiscovery = 1;
                    }
                }
            }
        }
        return;
    }

    if (!p->inItem) {
        if (strcmp(name, "item") == 0 || strcmp(name, "entry") == 0) {
            memset(&p->article, 0, sizeof p->article);
            p->inItem = 1;
            return;
        }
        if (strcmp(name, "image") == 0) {
            /* <image><title> inside a channel names the logo, not the feed. */
            p->inImage = 1;
            return;
        }
        if (strcmp(name, "title") == 0 && !p->sawFeedTitle && !p->inImage) {
            CaptureBegin(p, kFieldFeedTitle);
        }
        return;
    }

    if (strcmp(name, "title") == 0) {
        CaptureBegin(p, kFieldTitle);
        return;
    }

    if (strcmp(name, "link") == 0) {
        /*
         * Atom puts the URL in href; RSS puts it in the element's text. A feed
         * may carry several links, and only the alternate one is the article:
         * rel="self" is the feed itself and rel="enclosure" is a media file.
         */
        if (TagAttr(tag, len, "href", attr, sizeof attr)) {
            char rel[32];

            if (!TagAttr(tag, len, "rel", rel, sizeof rel) ||
                gz_stricmp(rel, "alternate") == 0) {
                if (p->article.link[0] == '\0' && attr[0] != '\0') {
                    gz_copy_n(p->article.link, sizeof p->article.link,
                              attr, strlen(attr));
                }
            }
        } else if (!selfClosing) {
            CaptureBegin(p, kFieldLink);
        }
        return;
    }

    if (strcmp(name, "guid") == 0) {
        /* Only useful as a link when the feed says it is one, and only if no
           real link turned up. RSS defaults isPermaLink to true. */
        if (p->article.link[0] == '\0') {
            char perma[8];

            if (!TagAttr(tag, len, "isPermaLink", perma, sizeof perma) ||
                gz_stricmp(perma, "true") == 0) {
                CaptureBegin(p, kFieldLink);
            }
        }
        return;
    }

    if (strcmp(name, "pubdate") == 0 || strcmp(name, "published") == 0 ||
        strcmp(name, "updated") == 0 || strcmp(name, "date") == 0) {
        /* pubDate wins over a later updated: the first date an item carries
           is the one the reader means by "when". */
        if (p->article.date == 0) {
            CaptureBegin(p, kFieldDate);
        }
        return;
    }

    if (strcmp(name, "source") == 0) {
        CaptureBegin(p, kFieldSource);
        return;
    }

    /*
     * The summary. A feed may carry several of these and they are not equal:
     * <content> and <content:encoded> hold the full text where a feed offers
     * it, while <description> and <summary> hold an extract. The prefix is
     * already stripped, so content:encoded arrives here as "content".
     */
    if (strcmp(name, "content") == 0) {
        CaptureBegin(p, kFieldBodyRich);
        return;
    }
    if (strcmp(name, "description") == 0 || strcmp(name, "summary") == 0) {
        if (p->article.body[0] == '\0') {
            CaptureBegin(p, kFieldBody);
        }
        return;
    }

    if (strcmp(name, "author") == 0 || strcmp(name, "creator") == 0) {
        p->inAuthor = 1;
        if (strcmp(name, "creator") == 0 && p->article.source[0] == '\0') {
            CaptureBegin(p, kFieldSource);   /* dc:creator carries text */
        }
        return;
    }

    if (strcmp(name, "name") == 0 && p->inAuthor &&
        p->article.source[0] == '\0') {
        CaptureBegin(p, kFieldSource);       /* Atom <author><name> */
    }
}

static void EndElement(GazetteFeedParser *p, const char *name)
{
    if (p->mode == kModeDiscovery) {
        return;
    }

    if (p->capturing != kFieldNone) {
        switch (p->capturing) {
            case kFieldFeedTitle:
                CaptureFinish(p, p->feedTitle, sizeof p->feedTitle);
                if (p->feedTitle[0] != '\0') {
                    p->sawFeedTitle = 1;
                }
                break;
            case kFieldTitle:
                CaptureFinish(p, p->article.title, sizeof p->article.title);
                break;
            case kFieldLink:
                CaptureFinish(p, p->article.link, sizeof p->article.link);
                break;
            case kFieldSource:
                CaptureFinish(p, p->article.source, sizeof p->article.source);
                break;
            case kFieldBody:
                if (p->article.body[0] == '\0') {
                    CaptureFinish(p, p->article.body, sizeof p->article.body);
                }
                break;
            case kFieldBodyRich: {
                /* Only if it actually says something: an empty <content/> is
                   common and must not wipe a description already captured. */
                size_t len = CaptureProcess(p);

                if (len > 0) {
                    gz_copy_n(p->article.body, sizeof p->article.body,
                              p->scratch, len);
                }
                break;
            }
            case kFieldDate: {
                char text[64];

                CaptureFinish(p, text, sizeof text);
                p->article.date = GazetteParseDate(text, strlen(text));
                break;
            }
            default:
                break;
        }
        p->capturing = kFieldNone;
    }

    if (strcmp(name, "item") == 0 || strcmp(name, "entry") == 0) {
        if (p->inItem) {
            EmitArticle(p);
            p->inItem = 0;
        }
        return;
    }
    if (strcmp(name, "image") == 0) {
        p->inImage = 0;
        return;
    }
    if (strcmp(name, "author") == 0 || strcmp(name, "creator") == 0) {
        p->inAuthor = 0;
    }
}

/* Dispatch one complete tag, whose text sits in p->tag[0..p->tagLen). */
static void ProcessTag(GazetteFeedParser *p)
{
    char   name[64];
    size_t len = p->tagLen;
    int    isEnd;
    int    selfClosing;

    if (len == 0) {
        return;
    }

    isEnd       = (p->tag[0] == '/');
    selfClosing = (p->tag[len - 1] == '/');
    if (selfClosing) {
        len--;
    }

    /* Processing instructions and declarations are not elements. */
    if (p->tag[0] == '?' || p->tag[0] == '!') {
        return;
    }

    TagName(p->tag, len, name, sizeof name);
    if (name[0] == '\0') {
        return;
    }

    if (isEnd) {
        EndElement(p, name);
    } else {
        StartElement(p, p->tag, len, name, selfClosing);
        if (selfClosing) {
            /* <title/> has no text, and a capture left open by it would
               swallow whatever element came next instead. */
            EndElement(p, name);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The scanner                                                        */
/* ------------------------------------------------------------------ */

void GazetteFeedParserInit(GazetteFeedParser *p,
                           GazetteArticleSink sink, void *context)
{
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof *p);
    p->state   = kStateText;
    p->mode    = kModeFeed;
    p->sink    = sink;
    p->context = context;
}

void GazetteFeedParserInitDiscovery(GazetteFeedParser *p)
{
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof *p);
    p->state = kStateText;
    p->mode  = kModeDiscovery;
}

int GazetteFeedParserFeed(GazetteFeedParser *p, const char *data, size_t len)
{
    size_t i;

    if (p == NULL || data == NULL) {
        return 0;
    }
    if (p->stopped) {
        return 0;
    }

    for (i = 0; i < len && !p->stopped; i++) {
        char c = data[i];

        switch (p->state) {
            case kStateText:
                if (c == '<') {
                    p->state  = kStateTag;
                    p->tagLen = 0;
                } else if (p->capturing != kFieldNone) {
                    CaptureByte(p, c);
                }
                break;

            case kStateTag:
                if (c == '>') {
                    ProcessTag(p);
                    p->state = kStateText;
                    break;
                }
                if (p->tagLen < sizeof p->tag - 1) {
                    p->tag[p->tagLen++] = c;
                }
                /* Comments and CDATA are recognised as soon as enough of the
                   opener has arrived, which may be several chunks in. */
                if (p->tagLen == 3 && strncmp(p->tag, "!--", 3) == 0) {
                    p->state        = kStateComment;
                    p->commentDashes = 0;
                } else if (p->tagLen == 8 &&
                           strncmp(p->tag, "![CDATA[", 8) == 0) {
                    p->state     = kStateCData;
                    p->cdHoldLen = 0;
                }
                break;

            case kStateComment:
                if (c == '-') {
                    if (p->commentDashes < 2) {
                        p->commentDashes++;
                    }
                } else if (c == '>' && p->commentDashes >= 2) {
                    p->state = kStateText;
                } else {
                    p->commentDashes = 0;
                }
                break;

            case kStateCData:
                /*
                 * "]]>" ends the section, and only that. Brackets are held
                 * back until it is clear they are not the terminator; a run of
                 * three or more emits the oldest and keeps looking.
                 */
                if (c == ']') {
                    if (p->cdHoldLen < 2) {
                        p->cdHoldLen++;
                    } else if (p->capturing != kFieldNone) {
                        CaptureByte(p, ']');
                    }
                } else if (c == '>' && p->cdHoldLen == 2) {
                    p->cdHoldLen = 0;
                    p->state     = kStateText;
                } else {
                    while (p->cdHoldLen > 0) {
                        if (p->capturing != kFieldNone) {
                            CaptureByte(p, ']');
                        }
                        p->cdHoldLen--;
                    }
                    if (p->capturing != kFieldNone) {
                        CaptureByte(p, c);
                    }
                }
                break;

            default:
                break;
        }
    }

    return p->stopped ? 0 : 1;
}

void GazetteFeedParserFinish(GazetteFeedParser *p)
{
    if (p == NULL || p->stopped) {
        return;
    }

    /*
     * A download cut short leaves an item open. Its title was read in full
     * long before its closing tag would have arrived, and a headline that was
     * fully read is worth showing even though the document was not.
     */
    if (p->mode == kModeFeed && p->inItem) {
        if (p->capturing != kFieldNone) {
            EndElement(p, "");
        }
        EmitArticle(p);
        p->inItem = 0;
    }
}

const char *GazetteFeedParserTitle(const GazetteFeedParser *p)
{
    return (p == NULL) ? "" : p->feedTitle;
}

long GazetteFeedParserArticleCount(const GazetteFeedParser *p)
{
    return (p == NULL) ? 0 : p->articleCount;
}

const char *GazetteFeedParserDiscovered(const GazetteFeedParser *p)
{
    return (p == NULL) ? "" : p->discovered;
}
