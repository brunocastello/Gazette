#!/usr/bin/env bash
#
# Put one article link through Gazette's full-text path and print what the
# reader pane would show. A Google News link goes to Google first, the three
# requests the Mac makes: the article page for two attributes, batchexecute
# for the story's address, then the story. Fetching is curl's, sent with the
# headers src/portable/gazette_http.c writes; every decision is Gazette's
# own, in tests/host/probe (make -C tests/host probe).
#
#   tools/probe.sh <link> [workdir]
#
set -euo pipefail

LINK="$1"
WORK="${2:-probe-out}"
PROBE="$(dirname "$0")/../tests/host/probe"
UA="$(sed -n 's/^#define kGazetteUserAgent "\(.*\)"$/\1/p' \
      "$(dirname "$0")/../src/portable/gazette_http.h")"
ACCEPT="application/rss+xml, application/atom+xml, application/xml, text/xml, */*"

mkdir -p "$WORK"

fetch() {       # fetch <out> <url> [curl args...]: what the Mac's fetch sends
    local out="$1" url="$2"
    shift 2
    curl -sS --http1.1 -L --max-redirs 5 -A "$UA" -H "Accept: $ACCEPT" \
         -H "Accept-Encoding: identity" -H "Connection: close" \
         -o "$out" -w '%{http_code} %{content_type} %{url_effective}\n' \
         "$@" "$url"
}

URL="$LINK"
if TOKEN="$("$PROBE" token "$LINK")"; then
    echo "== Google News token: ${TOKEN:0:60}..."
    echo "== GET the article page"
    fetch "$WORK/google.html" "https://news.google.com/rss/articles/$TOKEN"
    "$PROBE" scan "$WORK/google.html" | tee "$WORK/scan.txt"
    TS="$(sed -n 's/^ts=//p' "$WORK/scan.txt")"
    SIG="$(sed -n 's/^sig=//p' "$WORK/scan.txt")"
    BODY="$("$PROBE" body "$TOKEN" "$TS" "$SIG")"
    echo "== POST batchexecute"
    fetch "$WORK/answer.txt" \
        "https://news.google.com/_/DotsSplashUi/data/batchexecute" \
        -H "Content-Type: application/x-www-form-urlencoded;charset=UTF-8" \
        --data "$BODY"
    URL="$("$PROBE" answer "$WORK/answer.txt")"
    echo "== The story is at: $URL"
fi

echo "== GET the story"
fetch "$WORK/page.html" "$URL"
echo "== The reader pane"
"$PROBE" extract "$WORK/page.html"
