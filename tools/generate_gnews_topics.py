#!/usr/bin/env python3
"""
Emit Gazette's Google News topic table from NewsProxy's CAAQ map.

    python3 tools/generate_gnews_topics.py \
        --newsproxy ../NewsProxy/newsProxy.py \
        --out src/feeds/gazette_gnews_topics.c

NewsProxy keys that map by the base64 "CAAq..." topic IDs the original
Newsstand 1.1 client sent, because NewsProxy answers that client. Gazette is a
standalone application and never speaks the Newsstand protocol (AGENT.md), so
the IDs are dropped and only what they resolved to is kept: whether a topic is
one of Google News' own sections or a saved search, and the section name or
query string it uses. That is the part that was actually reverse-engineered,
and the part that goes stale if Google changes it.

The display name is the query with its first letter capitalised, or the
section name in title case. NewsProxy's queries were written to read as labels
("smartwatches wearables", "NFL football"), so nothing more is needed, and
capitalising only the first character cannot damage "iPhone Apple" or "BASE
jumping".
"""

import argparse
import re

GROUP_RE = re.compile(r'^\s*#\s*([A-Z][A-Za-z ]*)\s*$')
ENTRY_RE = re.compile(r'^\s*"[^"]+":\s*\(\s*"(section|search)",\s*"([^"]+)"\s*\)')


def parse(path):
    src = open(path, encoding="utf-8").read()
    start = src.index("CAAQ = {")
    block = src[start:src.index("\n}\n", start)]

    topics = []
    group = "General"
    for line in block.splitlines():
        m = GROUP_RE.match(line)
        if m:
            group = m.group(1).strip()
            continue
        m = ENTRY_RE.match(line)
        if m:
            topics.append((group, m.group(1), m.group(2)))
    return topics


def display_name(kind, value):
    if kind == "section":
        return value.capitalize()
    return value[0].upper() + value[1:] if value else value


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit(topics):
    groups = []
    for group, _, _ in topics:
        if group not in groups:
            groups.append(group)

    out = ['''/*
 * Gazette — Google News topic table
 * Copyright (c) 2026 brunocastello
 *
 * DO NOT EDIT. Regenerate with:
 *   python3 tools/generate_gnews_topics.py \\
 *       --newsproxy ../NewsProxy/newsProxy.py \\
 *       --out src/feeds/gazette_gnews_topics.c
 *
 * The curated topic set Newsstand 1.1 offered, carried over from NewsProxy's
 * CAAQ map. NewsProxy keys it by the base64 topic IDs the original client
 * sent, because NewsProxy answers that client; Gazette never speaks the
 * Newsstand protocol, so only what those IDs resolved to is kept.
 *
 * PORTABLE: data only, no system headers.
 */

#include "feeds/gazette_googlenews.h"

/* Group names, in the order Newsstand presented them. */
const char *const kGazetteTopicGroups[] = {''']

    for g in groups:
        out.append("    %s," % c_string(g))
    out.append("    0")
    out.append("};")
    out.append("")
    out.append("const int kGazetteTopicGroupCount = %d;" % len(groups))
    out.append("")
    out.append("const GazetteTopic kGazetteTopics[] = {")

    current = None
    for group, kind, value in topics:
        if group != current:
            out.append("")
            out.append("    /* %s */" % group)
            current = group
        out.append("    { %d, %s, %s, %s }," % (
            groups.index(group),
            c_string(display_name(kind, value)),
            "kGazetteTopicSection" if kind == "section" else "kGazetteTopicSearch",
            c_string(value)))

    out.append("};")
    out.append("")
    out.append("const int kGazetteTopicCount ="
               "\n    (int)(sizeof kGazetteTopics / sizeof kGazetteTopics[0]);")
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--newsproxy", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    topics = parse(args.newsproxy)
    open(args.out, "w", encoding="utf-8").write(emit(topics))
    print("wrote %s (%d topics)" % (args.out, len(topics)))


if __name__ == "__main__":
    main()
