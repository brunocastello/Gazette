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
