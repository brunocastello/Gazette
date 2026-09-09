/*
 * Gazette — portable string, preference-text and OS-9 text helpers
 * Copyright (c) 2026 brunocastello
 *
 * PORTABLE: this file and its implementation include no Mac system headers
 * at all — not even MacTypes.h — so tests/host compiles them with a plain
 * Linux or macOS cc and exercises them without Retro68. Anything that needs
 * the Toolbox belongs in store/, ui/ or main.cpp instead.
 */
#ifndef GAZETTE_PORTABLE_H
#define GAZETTE_PORTABLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Small string helpers                                                */
/* ------------------------------------------------------------------ */

/* ASCII-only tolower. Feed and prefs grammar must not pick up the
   Toolbox's notion of case, which follows the system script. */
int gz_lower(int c);

/* Case-insensitive compare of NUL-terminated strings. */
int gz_stricmp(const char *a, const char *b);

/* Case-insensitive compare of at most n bytes. */
int gz_strnicmp(const char *a, const char *b, size_t n);

/* Copy len bytes of src into a cap-sized dst and NUL-terminate.
   Returns the number of bytes copied (never more than cap - 1). */
size_t gz_copy_n(char *dst, size_t cap, const char *src, size_t len);

/* Trim ASCII whitespace from both ends of s[0..len). Returns a pointer into
   s and stores the trimmed length in *out_len. Never allocates. */
const char *gz_trim(const char *s, size_t len, size_t *out_len);

/* Parse a signed decimal number from at most len bytes.
   Returns def when there are no digits to read. */
long gz_parse_dec(const char *s, size_t len, long def);

/* 1 when the first strlen(prefix) bytes of s (bounded by len) match prefix,
   case-insensitively. */
int gz_starts_ci(const char *s, size_t len, const char *prefix);

/* ------------------------------------------------------------------ */
/* RFC 822 header blocks — shared by the HTTP response parser           */
/* ------------------------------------------------------------------ */

/* Locate the end of a header block. On success stores the offset one past the
   terminating CRLFCRLF (or LFLF) in *head_len and returns 1; returns 0 while
   the block is still incomplete, which is what a streamed read sees first. */
int gz_find_head_end(const char *buf, size_t len, size_t *head_len);

/* Find a header value inside a header block. head points at the start line;
   the search skips it, so a response's "HTTP/1.1 200 OK" can never be mistaken
   for a field. Returns a pointer to the first byte of the trimmed value and
   stores its length in *val_len, or NULL when the field is absent. */
const char *gz_header_find(const char *head, size_t head_len,
                           const char *name, size_t *val_len);

/* ------------------------------------------------------------------ */
/* Preference text — "key = value", one setting per line                */
/*                                                                     */
/* '#' or ';' starts a comment, the separator is '=' or ':', surrounding */
/* whitespace is trimmed, keys are case-insensitive. The same shape as   */
/* Gateway's prefs file, so a hand-edited Gazette Preferences reads the   */
/* way a Gateway user already expects.                                   */
/* ------------------------------------------------------------------ */

/* Copy the value for key into out. Returns 1 when the key was present with
   a non-empty value, 0 otherwise (out is set to "" in that case). */
int gz_prefs_get(const char *text, size_t len, const char *key,
                 char *out, size_t cap);

/* The nth occurrence of a key, counting from 0. A feed list reads far better
   as the same key repeated than as one enormous packed value. */
int gz_prefs_get_nth(const char *text, size_t len, const char *key, int n,
                     char *out, size_t cap);

/* Same as gz_prefs_get, but parses the value as a decimal number. */
long gz_prefs_get_num(const char *text, size_t len, const char *key, long def);

/*
 * Walk the settings in file order. Start with *off at 0; each call fills key
 * and value with the next setting and advances *off, returning 1 until there
 * are none left.
 *
 * The lookups above are order-independent, which is right for a setting but
 * wrong for a list: the feed list is a tree the user arranges by hand and by
 * dragging, and both the order of the feeds and which group each one falls
 * under are carried by nothing but the order of the lines.
 */
int gz_prefs_next(const char *text, size_t len, size_t *off,
                  char *key, size_t keyCap, char *value, size_t valueCap);

/* ------------------------------------------------------------------ */
/* Text for Mac OS 9                                                   */
/*                                                                     */
/* Feeds arrive as UTF-8. Mac OS 9 draws MacRoman, and the Newsstand-era */
/* look depends on text that is plainly readable in Geneva and Charcoal, */
/* so Gazette transliterates rather than trying to render Unicode.       */
/* This is NewsProxy's _to_ascii table, decoding UTF-8 as it goes.       */
/* ------------------------------------------------------------------ */

/* Transliterate UTF-8 src[0..len) into 7-bit ASCII in out[0..cap).
   Accents lose their marks, typographic punctuation becomes its typewriter
   equivalent, and anything with no sensible ASCII spelling becomes '?'.
   Returns the length written, always NUL-terminated when cap > 0. */
size_t gz_utf8_to_ascii(const char *src, size_t len, char *out, size_t cap);

/* Collapse every run of ASCII whitespace in s[0..len) to a single space and
   trim both ends, in place. Returns the new length. NUL-terminates. */
size_t gz_flatten_ws(char *s, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* GAZETTE_PORTABLE_H */
