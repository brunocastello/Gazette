/*
 * Gazette — Google News country map and URL building
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: no system headers. See gazette_googlenews.h. The topic table is
 * generated into gazette_gnews_topics.c.
 */

#include "feeds/gazette_googlenews.h"

#include "portable/gazette_portable.h"

#include <string.h>

/*
 * NewsProxy's COUNTRY map, with display names added. The set is what
 * Newsstand 1.1 offered rather than everything Google News supports — the
 * point is parity, and a list of two hundred countries in a Platinum popup
 * would be worse than the twenty-nine that were actually there.
 */
const GazetteCountry kGazetteCountries[] = {
    { "US", "United States",  "en-US", "US", "US:en" },
    { "UK", "United Kingdom", "en-GB", "GB", "GB:en" },
    { "AU", "Australia",      "en-AU", "AU", "AU:en" },
    { "CA", "Canada",         "en-CA", "CA", "CA:en" },
    { "IN", "India",          "en-IN", "IN", "IN:en" },
    { "IE", "Ireland",        "en-IE", "IE", "IE:en" },
    { "PK", "Pakistan",       "en-PK", "PK", "PK:en" },
    { "NZ", "New Zealand",    "en-NZ", "NZ", "NZ:en" },
    { "JP", "Japan",          "ja-JP", "JP", "JP:ja" },
    { "DE", "Germany",        "de-DE", "DE", "DE:de" },
    { "FR", "France",         "fr-FR", "FR", "FR:fr" },
    { "IT", "Italy",          "it-IT", "IT", "IT:it" },
    { "BR", "Brazil",         "pt-BR", "BR", "BR:pt" },
    { "PT", "Portugal",       "pt-PT", "PT", "PT:pt" },
    { "MX", "Mexico",         "es-MX", "MX", "MX:es" },
    { "AR", "Argentina",      "es-AR", "AR", "AR:es" },
    { "CL", "Chile",          "es-CL", "CL", "CL:es" },
    { "CO", "Colombia",       "es-CO", "CO", "CO:es" },
    { "AT", "Austria",        "de-AT", "AT", "AT:de" },
    { "CH", "Switzerland",    "de-CH", "CH", "CH:de" },
    { "BE", "Belgium",        "fr-BE", "BE", "BE:fr" },
    { "NL", "Netherlands",    "nl-NL", "NL", "NL:nl" },
    { "PL", "Poland",         "pl-PL", "PL", "PL:pl" },
    { "RU", "Russia",         "ru-RU", "RU", "RU:ru" },
    { "TR", "Turkey",         "tr-TR", "TR", "TR:tr" },
    { "TW", "Taiwan",         "zh-TW", "TW", "TW:zh" },
    { "KR", "South Korea",    "ko-KR", "KR", "KR:ko" },
    { "DK", "Denmark",        "da-DK", "DK", "DK:da" },
    { "GR", "Greece",         "el-GR", "GR", "GR:el" }
};

const int kGazetteCountryCount =
    (int)(sizeof kGazetteCountries / sizeof kGazetteCountries[0]);

const GazetteCountry *GazetteCountryFind(const char *code)
{
    int i;

    if (code != NULL) {
        for (i = 0; i < kGazetteCountryCount; i++) {
            if (gz_stricmp(kGazetteCountries[i].code, code) == 0) {
                return &kGazetteCountries[i];
            }
        }
    }
    /* NewsProxy's DEFAULT_COUNTRY: an unknown code still produces a usable
       feed rather than a failure the user cannot interpret. */
    return &kGazetteCountries[0];
}

/* ------------------------------------------------------------------ */
/* URL building                                                        */
/* ------------------------------------------------------------------ */

/* Append src, tracking the running length. Once the buffer is full the length
   keeps growing past cap so the caller can tell it overflowed. */
static void Append(char *out, size_t cap, size_t *len, const char *src)
{
    size_t n = strlen(src);

    if (*len + n < cap) {
        memcpy(out + *len, src, n);
    }
    *len += n;
}

static int IsUnreserved(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

size_t GazetteURLEncode(const char *src, char *out, size_t cap)
{
    static const char kHex[] = "0123456789ABCDEF";
    size_t len = 0;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (src == NULL) {
        return 0;
    }

    for (; *src != '\0'; src++) {
        unsigned char c = (unsigned char)*src;

        if (IsUnreserved(c)) {
            if (len + 1 < cap) {
                out[len] = (char)c;
            }
            len++;
        } else {
            if (len + 3 < cap) {
                out[len]     = '%';
                out[len + 1] = kHex[(c >> 4) & 0x0F];
                out[len + 2] = kHex[c & 0x0F];
            }
            len += 3;
        }
    }

    if (len >= cap) {
        out[0] = '\0';
        return 0;
    }
    out[len] = '\0';
    return len;
}

size_t GazetteGoogleNewsURL(const GazetteCountry *country,
                            GazetteTopicKind kind, const char *value,
                            char *out, size_t cap)
{
    size_t len = 0;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (country == NULL) {
        country = GazetteCountryFind(NULL);
    }

    Append(out, cap, &len, "https://news.google.com/rss");

    switch (kind) {
        case kGazetteTopicSection:
            if (value == NULL || value[0] == '\0') {
                return 0;
            }
            Append(out, cap, &len, "/headlines/section/topic/");
            Append(out, cap, &len, value);
            break;

        case kGazetteTopicNation:
            /* The country's own national news is keyed by gl, not by a
               section name — "/geo/US" rather than "/topic/NATION". */
            Append(out, cap, &len, "/headlines/section/geo/");
            Append(out, cap, &len, country->gl);
            break;

        case kGazetteTopicSearch: {
            char encoded[512];

            if (value == NULL || value[0] == '\0') {
                return 0;
            }
            if (GazetteURLEncode(value, encoded, sizeof encoded) == 0) {
                return 0;
            }
            Append(out, cap, &len, "/search?q=");
            Append(out, cap, &len, encoded);
            break;
        }

        case kGazetteTopicTop:
        default:
            break;                      /* /rss on its own is top stories */
    }

    /* Search already opened the query string; everything else opens it here. */
    Append(out, cap, &len, (kind == kGazetteTopicSearch) ? "&hl=" : "?hl=");
    Append(out, cap, &len, country->hl);
    Append(out, cap, &len, "&gl=");
    Append(out, cap, &len, country->gl);
    Append(out, cap, &len, "&ceid=");
    Append(out, cap, &len, country->ceid);

    if (len >= cap) {
        out[0] = '\0';
        return 0;
    }
    out[len] = '\0';
    return len;
}

/* ------------------------------------------------------------------ */
/* Article links                                                       */
/* ------------------------------------------------------------------ */

int GazetteGNewsArticleToken(const char *url, char *token, size_t cap)
{
    const char *p;
    const char *start;
    size_t      n = 0;

    if (url == NULL || token == NULL || cap == 0) {
        return 0;
    }
    token[0] = '\0';

    if (!gz_starts_ci(url, strlen(url), "https://news.google.com/") &&
        !gz_starts_ci(url, strlen(url), "http://news.google.com/")) {
        return 0;
    }
    p = strstr(url, "/articles/");
    if (p == NULL) {
        p = strstr(url, "/read/");
        if (p == NULL) {
            return 0;
        }
        p += 6;
    } else {
        p += 10;
    }

    /* The token runs to the query string or the end. */
    start = p;
    while (*p != '\0' && *p != '?' && *p != '#' && *p != '/') {
        p++;
    }
    n = (size_t)(p - start);
    if (n == 0 || n + 1 > cap) {
        return 0;
    }
    memcpy(token, start, n);
    token[n] = '\0';
    return 1;
}

static const char kSigAttr[] = "data-n-a-sg=\"";
static const char kTsAttr[]  = "data-n-a-ts=\"";

void GazetteGNewsScanInit(GazetteGNewsScan *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof *s);
    }
}

int GazetteGNewsScanDone(const GazetteGNewsScan *s)
{
    return (s != NULL && s->haveSig && s->haveTs) ? 1 : 0;
}

/*
 * One attribute's matcher: the name, then the value up to the closing
 * quote. A failed match starts over from the first character, which for
 * these two names is all the cleverness the page needs.
 */
static void ScanOne(char c, const char *name, size_t *match, int *in,
                    char *value, size_t cap, size_t *valueLen, int *have)
{
    if (*have) {
        return;
    }
    if (*in) {
        if (c == '"') {
            value[*valueLen] = '\0';
            *have = 1;
            *in   = 0;
        } else if (*valueLen + 1 < cap) {
            value[(*valueLen)++] = c;
        } else {
            /* Longer than any real value: not the attribute after all. */
            *in       = 0;
            *valueLen = 0;
        }
        return;
    }
    if (c == name[*match]) {
        (*match)++;
        if (name[*match] == '\0') {
            *in       = 1;
            *match    = 0;
            *valueLen = 0;
        }
    } else {
        *match = (c == name[0]) ? 1 : 0;
    }
}

int GazetteGNewsScanFeed(GazetteGNewsScan *s, const char *data, size_t len)
{
    size_t i;

    if (s == NULL || data == NULL) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        ScanOne(data[i], kSigAttr, &s->matchSig, &s->inSig, s->sig,
                sizeof s->sig, &s->sigLen, &s->haveSig);
        ScanOne(data[i], kTsAttr, &s->matchTs, &s->inTs, s->ts,
                sizeof s->ts, &s->tsLen, &s->haveTs);
        if (s->haveSig && s->haveTs) {
            return 0;
        }
    }
    return 1;
}

/*
 * The body, percent-encoded the way a browser posts a form. What is inside
 * is the request 68k-news worked out: an outer array naming the RPC
 * ("Fbv4je") and, as one JSON string, the inner request with the token,
 * the timestamp and the signature. The inner string's quotes are escaped
 * for the outer JSON, and then the whole thing is escaped for the form.
 */
static size_t PutEncoded(char *out, size_t cap, size_t len, const char *s)
{
    static const char kHex[] = "0123456789ABCDEF";

    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~' || c == '/') {
            if (len + 1 < cap) {
                out[len] = (char)c;
            }
            len++;
        } else {
            if (len + 3 < cap) {
                out[len]     = '%';
                out[len + 1] = kHex[c >> 4];
                out[len + 2] = kHex[c & 15];
            }
            len += 3;
        }
    }
    return len;
}

size_t GazetteGNewsBuildBody(const char *token, const char *ts,
                             const char *sig, char *out, size_t cap)
{
    static const char kHead[] =
        "f.req=";
    static const char kOpen[] =
        "[[[\"Fbv4je\",\"[\\\"garturlreq\\\",[[\\\"X\\\",\\\"X\\\",[\\\"X\\\",\\\"X\\\"],"
        "null,null,1,1,\\\"US:en\\\",null,1,null,null,null,null,null,0,1],"
        "\\\"X\\\",\\\"X\\\",1,[1,1,1],1,1,null,0,0,null,0],\\\"";
    static const char kMid[]   = "\\\",";
    static const char kMid2[]  = ",\\\"";
    static const char kClose[] = "\\\"]\"]]]";
    size_t len = 0;

    if (out == NULL || cap == 0 || token == NULL || ts == NULL ||
        sig == NULL) {
        return 0;
    }
    memcpy(out, kHead, sizeof kHead - 1);
    len = sizeof kHead - 1;
    len = PutEncoded(out, cap, len, kOpen);
    len = PutEncoded(out, cap, len, token);
    len = PutEncoded(out, cap, len, kMid);
    len = PutEncoded(out, cap, len, ts);
    len = PutEncoded(out, cap, len, kMid2);
    len = PutEncoded(out, cap, len, sig);
    len = PutEncoded(out, cap, len, kClose);

    if (len >= cap) {
        out[0] = '\0';
        return 0;
    }
    out[len] = '\0';
    return len;
}

/*
 * The answer is JSON inside JSON: an outer array whose third element is a
 * string holding the inner array, whose second element is the address.
 * Read as bytes rather than parsed: after "garturlres" comes \",\" and
 * then the address up to the next \" — with the inner string's escapes
 * doubled by the outer one, so an '=' in it arrives as \\u003d.
 */
/* Four hex digits, or -1. */
static long Hex4(const char *s)
{
    long v = 0;
    int  i;

    for (i = 0; i < 4; i++) {
        char c = s[i];
        int  d;

        if (c >= '0' && c <= '9')      d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = v * 16 + d;
    }
    return v;
}

int GazetteGNewsParseAnswer(const char *resp, size_t len,
                            char *url, size_t cap)
{
    static const char kMark[] = "garturlres";
    size_t i, n = 0;

    if (resp == NULL || url == NULL || cap == 0) {
        return 0;
    }
    url[0] = '\0';

    for (i = 0; i + sizeof kMark - 1 <= len; i++) {
        if (memcmp(resp + i, kMark, sizeof kMark - 1) == 0) {
            break;
        }
    }
    if (i + sizeof kMark - 1 > len) {
        return 0;
    }
    i += sizeof kMark - 1;

    /* Past the closing \" of "garturlres" and the opening \" of the
       address: the next two quote characters, whatever escapes them. */
    {
        int quotes = 0;

        while (i < len && quotes < 2) {
            if (resp[i] == '"') {
                quotes++;
            }
            i++;
        }
        if (quotes < 2) {
            return 0;
        }
    }

    while (i < len) {
        char c = resp[i];

        if (c == '\\') {
            size_t slashes = 0;

            while (i < len && resp[i] == '\\') {
                slashes++;
                i++;
            }
            if (i >= len) {
                break;
            }
            c = resp[i];
            if (c == '"') {
                break;                  /* the inner string's own end */
            }
            if (c == 'u' && i + 4 < len) {
                long v = Hex4(resp + i + 1);

                if (v >= 0 && v < 128) {
                    c = (char)v;
                    i += 4;
                }
            }
            /* "\/" is "/", and anything else keeps its character. */
        } else if (c == '"') {
            break;
        }
        if (n + 1 >= cap) {
            url[0] = '\0';
            return 0;
        }
        url[n++] = c;
        i++;
    }
    url[n] = '\0';

    return (n > 8 && gz_starts_ci(url, n, "http")) ? 1 : 0;
}
