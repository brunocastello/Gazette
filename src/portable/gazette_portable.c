/*
 * Gazette — portable string, preference-text and OS-9 text helpers
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: no Mac system headers. See gazette_portable.h.
 */

#include "gazette_portable.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Small string helpers                                                */
/* ------------------------------------------------------------------ */

int gz_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

int gz_stricmp(const char *a, const char *b)
{
    while (*a && *b) {
        int la = gz_lower((unsigned char)*a);
        int lb = gz_lower((unsigned char)*b);
        if (la != lb) {
            return la - lb;
        }
        a++;
        b++;
    }
    return gz_lower((unsigned char)*a) - gz_lower((unsigned char)*b);
}

int gz_strnicmp(const char *a, const char *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        int la = gz_lower((unsigned char)a[i]);
        int lb = gz_lower((unsigned char)b[i]);
        if (la != lb) {
            return la - lb;
        }
        if (la == 0) {
            break;
        }
    }
    return 0;
}

size_t gz_copy_n(char *dst, size_t cap, const char *src, size_t len)
{
    size_t n;

    if (dst == NULL || cap == 0) {
        return 0;
    }
    n = (len < cap - 1) ? len : cap - 1;
    if (n > 0 && src != NULL) {
        memcpy(dst, src, n);
    } else {
        n = 0;
    }
    dst[n] = '\0';
    return n;
}

static int gz_is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\v' || c == '\f';
}

const char *gz_trim(const char *s, size_t len, size_t *out_len)
{
    while (len > 0 && gz_is_space((unsigned char)*s)) {
        s++;
        len--;
    }
    while (len > 0 && gz_is_space((unsigned char)s[len - 1])) {
        len--;
    }
    if (out_len != NULL) {
        *out_len = len;
    }
    return s;
}

int gz_starts_ci(const char *s, size_t len, const char *prefix)
{
    size_t plen = strlen(prefix);

    if (len < plen) {
        return 0;
    }
    return gz_strnicmp(s, prefix, plen) == 0;
}

long gz_parse_dec(const char *s, size_t len, long def)
{
    long  value = 0;
    int   negative = 0;
    int   digits = 0;
    size_t i = 0;

    if (s == NULL) {
        return def;
    }
    /* Leading whitespace is skipped: the status line hands this " 200 OK"
       starting at the space after "HTTP/1.1". */
    while (i < len && gz_is_space((unsigned char)s[i])) {
        i++;
    }
    if (i < len && (s[i] == '-' || s[i] == '+')) {
        negative = (s[i] == '-');
        i++;
    }
    for (; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            break;
        }
        value = value * 10 + (s[i] - '0');
        digits++;
    }
    if (digits == 0) {
        return def;
    }
    return negative ? -value : value;
}

/* ------------------------------------------------------------------ */
/* RFC 822 header blocks                                               */
/* ------------------------------------------------------------------ */

/*
 * Offset of the first byte after the line starting at off.
 *
 * All three conventions count as a line ending. Gazette writes CR, the way
 * OS 9 text files do, but the prefs file is meant to be hand-edited and may
 * come back from a Windows editor as CRLF or from a Unix one as LF.
 */
static size_t gz_next_line(const char *text, size_t len, size_t off)
{
    while (off < len && text[off] != '\n' && text[off] != '\r') {
        off++;
    }
    if (off >= len) {
        return len;
    }
    if (text[off] == '\r' && off + 1 < len && text[off + 1] == '\n') {
        return off + 2;
    }
    return off + 1;
}

int gz_find_head_end(const char *buf, size_t len, size_t *head_len)
{
    size_t i;

    if (buf == NULL || head_len == NULL) {
        return 0;
    }

    for (i = 0; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            i + 3 < len && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            *head_len = i + 4;
            return 1;
        }
        /* Bare LF is tolerated. It is not legal HTTP, and it turns up
           anyway -- often enough that rejecting it costs more than it
           protects. */
        if (buf[i] == '\n' && buf[i + 1] == '\n') {
            *head_len = i + 2;
            return 1;
        }
    }
    return 0;
}

const char *gz_header_find(const char *head, size_t head_len,
                           const char *name, size_t *val_len)
{
    size_t nlen;
    size_t off;

    if (head == NULL || name == NULL || val_len == NULL) {
        return NULL;
    }

    nlen = strlen(name);
    off  = gz_next_line(head, head_len, 0);     /* skip the start line */

    while (off < head_len) {
        size_t eol = off;
        size_t line_end;

        while (eol < head_len && head[eol] != '\n') {
            eol++;
        }
        line_end = eol;
        if (line_end > off && head[line_end - 1] == '\r') {
            line_end--;
        }
        if (line_end == off) {
            break;                              /* blank line: end of block */
        }

        if (line_end - off > nlen &&
            gz_strnicmp(head + off, name, nlen) == 0 &&
            head[off + nlen] == ':') {
            size_t v = off + nlen + 1;

            while (v < line_end && (head[v] == ' ' || head[v] == '\t')) {
                v++;
            }
            while (line_end > v &&
                   (head[line_end - 1] == ' ' || head[line_end - 1] == '\t')) {
                line_end--;
            }
            *val_len = line_end - v;
            return head + v;
        }

        off = (eol < head_len) ? eol + 1 : head_len;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Preference text                                                     */
/* ------------------------------------------------------------------ */

int gz_prefs_next(const char *text, size_t len, size_t *off,
                  char *key, size_t keyCap, char *value, size_t valueCap)
{
    if (key != NULL && keyCap > 0) {
        key[0] = '\0';
    }
    if (value != NULL && valueCap > 0) {
        value[0] = '\0';
    }
    if (text == NULL || off == NULL) {
        return 0;
    }

    while (*off < len) {
        size_t      eol = gz_next_line(text, len, *off);
        size_t      line_len;
        const char *line;
        const char *sep;
        size_t      i;

        line = gz_trim(text + *off, eol - *off, &line_len);
        *off = eol;

        if (line_len == 0 || line[0] == '#' || line[0] == ';') {
            continue;
        }

        /* Find the separator; a line without one is not a setting. */
        sep = NULL;
        for (i = 0; i < line_len; i++) {
            if (line[i] == '=' || line[i] == ':') {
                sep = line + i;
                break;
            }
        }
        if (sep == NULL) {
            continue;
        }

        {
            size_t      name_len;
            const char *name = gz_trim(line, (size_t)(sep - line), &name_len);
            size_t      val_len;
            const char *val;

            if (name_len == 0) {
                continue;
            }
            val = gz_trim(sep + 1, line_len - (size_t)(sep + 1 - line),
                          &val_len);
            if (val_len == 0) {
                continue;       /* an empty value counts as absent */
            }
            gz_copy_n(key, keyCap, name, name_len);
            gz_copy_n(value, valueCap, val, val_len);
            return 1;
        }
    }

    return 0;
}

int gz_prefs_get_nth(const char *text, size_t len, const char *key, int n,
                     char *out, size_t cap)
{
    /* Every value has to fit here on the way past, whether or not it is the
       one being asked for. A feed line is the long one. */
    char   name[64];
    char   value[768];
    size_t off  = 0;
    int    seen = 0;

    if (out != NULL && cap > 0) {
        out[0] = '\0';
    }
    if (text == NULL || key == NULL || n < 0) {
        return 0;
    }

    while (gz_prefs_next(text, len, &off, name, sizeof name,
                         value, sizeof value)) {
        if (gz_stricmp(name, key) != 0) {
            continue;
        }
        if (seen++ != n) {
            continue;
        }
        gz_copy_n(out, cap, value, strlen(value));
        return 1;
    }

    return 0;
}

int gz_prefs_get(const char *text, size_t len, const char *key,
                 char *out, size_t cap)
{
    return gz_prefs_get_nth(text, len, key, 0, out, cap);
}

long gz_prefs_get_num(const char *text, size_t len, const char *key, long def)
{
    char buf[32];

    if (!gz_prefs_get(text, len, key, buf, sizeof buf)) {
        return def;
    }
    return gz_parse_dec(buf, strlen(buf), def);
}

/* ------------------------------------------------------------------ */
/* Text for Mac OS 9                                                   */
/* ------------------------------------------------------------------ */

/* NewsProxy's _ASCII_SUBS, as Unicode code points. Anything absent from
   this table and outside ASCII becomes '?' — deliberately visible, so a
   missing spelling shows up in the reader instead of silently vanishing. */
typedef struct {
    unsigned long cp;
    const char   *rep;
} GzAsciiSub;

static const GzAsciiSub kAsciiSubs[] = {
    /* Typographic punctuation */
    { 0x2018UL, "'"    }, { 0x2019UL, "'"    },
    { 0x201CUL, "\""   }, { 0x201DUL, "\""   },
    { 0x2013UL, "-"    }, { 0x2014UL, "--"   },
    { 0x2026UL, "..."  }, { 0x00A0UL, " "    },
    { 0x2022UL, "*"    }, { 0x00B7UL, "*"    },
    { 0x00B0UL, " deg" },
    { 0x00AEUL, "(R)"  }, { 0x00A9UL, "(C)"  }, { 0x2122UL, "(TM)" },
    { 0x00BDUL, "1/2"  }, { 0x00BCUL, "1/4"  }, { 0x00BEUL, "3/4"  },

    /* Lowercase with diacritics */
    { 0x00E1UL, "a" }, { 0x00E0UL, "a" }, { 0x00E2UL, "a" },
    { 0x00E3UL, "a" }, { 0x00E4UL, "a" }, { 0x00E5UL, "a" },
    { 0x00E6UL, "ae" },
    { 0x00E9UL, "e" }, { 0x00E8UL, "e" }, { 0x00EAUL, "e" }, { 0x00EBUL, "e" },
    { 0x00EDUL, "i" }, { 0x00ECUL, "i" }, { 0x00EEUL, "i" }, { 0x00EFUL, "i" },
    { 0x00F3UL, "o" }, { 0x00F2UL, "o" }, { 0x00F4UL, "o" },
    { 0x00F5UL, "o" }, { 0x00F6UL, "o" }, { 0x00F8UL, "o" },
    { 0x00FAUL, "u" }, { 0x00F9UL, "u" }, { 0x00FBUL, "u" }, { 0x00FCUL, "u" },
    { 0x00FDUL, "y" }, { 0x00FFUL, "y" },
    { 0x00E7UL, "c" }, { 0x00F1UL, "n" }, { 0x00DFUL, "ss" },
    { 0x00BAUL, "o." }, { 0x00AAUL, "a." },

    /* Uppercase with diacritics */
    { 0x00C1UL, "A" }, { 0x00C0UL, "A" }, { 0x00C2UL, "A" },
    { 0x00C3UL, "A" }, { 0x00C4UL, "A" }, { 0x00C5UL, "A" },
    { 0x00C6UL, "AE" },
    { 0x00C9UL, "E" }, { 0x00C8UL, "E" }, { 0x00CAUL, "E" }, { 0x00CBUL, "E" },
    { 0x00CDUL, "I" }, { 0x00CCUL, "I" }, { 0x00CEUL, "I" }, { 0x00CFUL, "I" },
    { 0x00D3UL, "O" }, { 0x00D2UL, "O" }, { 0x00D4UL, "O" },
    { 0x00D5UL, "O" }, { 0x00D6UL, "O" }, { 0x00D8UL, "O" },
    { 0x00DAUL, "U" }, { 0x00D9UL, "U" }, { 0x00DBUL, "U" }, { 0x00DCUL, "U" },
    { 0x00DDUL, "Y" }, { 0x00C7UL, "C" }, { 0x00D1UL, "N" }
};

/*
 * Decode one UTF-8 sequence at src[*off]. Stores the code point and advances
 * *off past it. A malformed or truncated sequence consumes exactly one byte
 * and yields 0xFFFD, so bad input can never stall the loop or run off the end.
 */
static unsigned long gz_utf8_next(const char *src, size_t len, size_t *off)
{
    unsigned char  b0 = (unsigned char)src[*off];
    unsigned long  cp;
    int            extra;
    int            i;

    if (b0 < 0x80) {
        (*off)++;
        return b0;
    } else if ((b0 & 0xE0) == 0xC0) {
        cp = b0 & 0x1FUL;
        extra = 1;
    } else if ((b0 & 0xF0) == 0xE0) {
        cp = b0 & 0x0FUL;
        extra = 2;
    } else if ((b0 & 0xF8) == 0xF0) {
        cp = b0 & 0x07UL;
        extra = 3;
    } else {
        (*off)++;
        return 0xFFFDUL;            /* stray continuation or invalid lead */
    }

    /* The continuation bytes live at *off + 1 .. *off + extra, so the last
       one must still be inside the buffer. */
    if (*off + (size_t)extra >= len) {
        (*off)++;
        return 0xFFFDUL;
    }

    for (i = 1; i <= extra; i++) {
        unsigned char bn = (unsigned char)src[*off + (size_t)i];
        if ((bn & 0xC0) != 0x80) {
            (*off)++;
            return 0xFFFDUL;
        }
        cp = (cp << 6) | (unsigned long)(bn & 0x3F);
    }

    *off += (size_t)(extra + 1);
    return cp;
}

size_t gz_utf8_to_ascii(const char *src, size_t len, char *out, size_t cap)
{
    size_t in  = 0;
    size_t written = 0;

    if (out == NULL || cap == 0) {
        return 0;
    }
    if (src == NULL) {
        out[0] = '\0';
        return 0;
    }

    while (in < len && written < cap - 1) {
        unsigned long cp = gz_utf8_next(src, len, &in);
        const char   *rep = NULL;
        size_t        rep_len;
        size_t        i;

        if (cp < 0x80UL) {
            out[written++] = (char)cp;
            continue;
        }

        for (i = 0; i < sizeof kAsciiSubs / sizeof kAsciiSubs[0]; i++) {
            if (kAsciiSubs[i].cp == cp) {
                rep = kAsciiSubs[i].rep;
                break;
            }
        }
        if (rep == NULL) {
            out[written++] = '?';
            continue;
        }

        /* A multi-byte replacement is all-or-nothing: half of "(TM)" in the
           output would be worse than stopping cleanly at the buffer's end. */
        rep_len = strlen(rep);
        if (written + rep_len > cap - 1) {
            break;
        }
        memcpy(out + written, rep, rep_len);
        written += rep_len;
    }

    out[written] = '\0';
    return written;
}

/* Shared by both flatteners: keepBreaks decides what a whitespace run that
   contains a newline collapses to. */
static size_t FlattenWhitespace(char *s, size_t len, int keepBreaks)
{
    size_t in  = 0;
    size_t out = 0;

    if (s == NULL) {
        return 0;
    }

    while (in < len && gz_is_space((unsigned char)s[in])) {
        in++;
    }

    while (in < len) {
        if (gz_is_space((unsigned char)s[in])) {
            int sawBreak = 0;

            while (in < len && gz_is_space((unsigned char)s[in])) {
                if (s[in] == '\n' || s[in] == '\r') {
                    sawBreak = 1;
                }
                in++;
            }
            /* Nothing follows the run: it was trailing, and is dropped. */
            if (in < len) {
                s[out++] = (keepBreaks && sawBreak) ? '\n' : ' ';
            }
        } else {
            s[out++] = s[in++];
        }
    }

    s[out] = '\0';
    return out;
}

size_t gz_flatten_ws(char *s, size_t len)
{
    return FlattenWhitespace(s, len, 0);
}

size_t gz_flatten_lines(char *s, size_t len)
{
    return FlattenWhitespace(s, len, 1);
}

