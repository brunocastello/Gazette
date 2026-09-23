#!/usr/bin/env bash
#
# Fetch one feed the way Gazette does and print, item by item, what the
# reader pane will show: the body the feed carried, laid out by Gazette's own
# parser and extractor (tests/host/probe; make -C tests/host probe). The
# fetch is curl's, sent with the headers src/portable/gazette_http.c writes.
#
#   tools/probe.sh <feed url> [workdir]
#
set -euo pipefail

FEED="$1"
WORK="${2:-probe-out}"
PROBE="$(dirname "$0")/../tests/host/probe"
UA="$(sed -n 's/^#define kGazetteUserAgent "\(.*\)"$/\1/p' \
      "$(dirname "$0")/../src/portable/gazette_http.h")"
ACCEPT="application/rss+xml, application/atom+xml, application/xml, text/xml, */*"

mkdir -p "$WORK"
echo "== GET the feed"
curl -sS --http1.1 -L --max-redirs 5 -A "$UA" -H "Accept: $ACCEPT" \
     -H "Accept-Encoding: identity" -H "Connection: close" \
     -o "$WORK/feed.xml" -w '%{http_code} %{content_type} %{url_effective}\n' \
     "$FEED"
"$PROBE" feed "$WORK/feed.xml"
