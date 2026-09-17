# Gazette — Carbon RSS / Atom Reader for Mac OS 9

A native, Carbon-based RSS and Atom feed reader for **Mac OS 9.x** on **PowerPC**.

**Gazette** is a spiritual successor to Alex Robb's Newsstand 1.1 — a beautiful, fast, native replacement with full Platinum look-and-feel, modern HTTPS feed fetching, Google News support, and sensible offline caching.

## Features (planned)

- **Google News** — same countries and curated topics/sections as Newsstand 1.1
- **Custom feeds** — user-added RSS 2.0 and Atom feeds with auto-discovery
- **Platinum UI** — sidebar, article list, reader pane (Newsstand-inspired layout)
- **Offline caching** — feed lists and article bodies cached locally
- **Non-blocking networking** — all HTTPS via Gateway's Certainly/TLS stack

## What it actually targets

Gazette is cross-compiled with [Retro68](https://github.com/autc04/Retro68) into a
**PowerPC CFM (PEF) application with a real resource fork**, linked against CarbonLib.

- **Runs on:** Mac OS 9.x, PowerPC, with CarbonLib 1.1 or later installed
- **Does not run on:** any Intel or Apple Silicon macOS — this is a CFM binary, not Mach-O
- **68K:** not supported, and not planned (Carbon is PowerPC-only)

Every Toolbox call in the source is checked against the `Availability:` block in
Apple's Universal Interfaces headers; anything not marked *"CarbonLib: in CarbonLib
1.0 and later"* is off limits. See `AGENT.md` for the full constraint list.

## Building

There is no macOS-native or Linux-native build. The toolchain is Retro68, and the
supported way to run it is the official Docker image — which is exactly what CI does.

### Requirements

- **Docker**
- **Apple Universal Interfaces 3.4**, already vendored at `third_party/AUI/`

### Build

```bash
docker run --rm -v "$PWD:/root/Gazette" -w /root/Gazette -it \
  ghcr.io/autc04/retro68 /bin/bash
```

Then, inside the container:

```bash
TOOLCHAIN=/Retro68-build/toolchain

# See "The fenv.h workaround" below.
rm -f "$TOOLCHAIN/powerpc-apple-macos/include/fenv.h"

# Stage Apple's Universal Interfaces: <toolchain> <interfaces> <68k> <ppc> <carbon>
/Retro68-build/bin/interfaces-and-libraries.sh "$TOOLCHAIN" \
  /root/Gazette/third_party/AUI/Universal false true true

cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN/powerpc-apple-macos/cmake/retrocarbon.toolchain.cmake"
cmake --build build
```

### Output

`add_application()` runs MakePEF and Rez over the linked XCOFF and produces, in `build/`:

| File | What it is |
|------|------------|
| `Gazette.dsk` | Disk image — mount it in SheepShaver or Basilisk II |
| `Gazette.bin` | MacBinary II — for transfer to real hardware |
| `Gazette.APPL` | Raw application, data fork only |
| `Gazette.ad` + `%Gazette.ad` | AppleDouble pair (data fork + resource fork) |

CI additionally packages the AppleDouble pair into `Gazette.sit`, a StuffIt 1.5.1
archive that StuffIt Expander on OS 9 — or The Unarchiver on a modern machine —
will expand with the resource fork intact.

### The fenv.h workaround

Retro68's `interfaces-and-libraries.sh` stages Apple's headers by symlinking them
into the toolchain's include directory, and its `prepare-headers.sh` deliberately
whitelists Apple's `fenv.h` on the premise that *"newlib does not provide fenv.h"*.
Current Retro68 images **do** ship one, as a real file rather than a symlink, and
the staging script only unlinks symlinks before restaging — so the leftover real
file makes the symlink fail with `ln: failed to create symbolic link './fenv.h':
File exists`, aborting the whole step.

Deleting newlib's copy first resolves it, and Apple's is the one we want anyway:
`CoreServices.h` includes `<fenv.h>` directly.

### Icon

A folded newspaper — masthead band, lead picture, columns of text, with a second
sheet showing behind it. It lives in the **resource fork** as `ICN#`, `icl8`,
`ics#` and `ics8`, plus the `BNDL`/`FREF` pair that binds the family to the
`Gzt9` creator. A `.icns` file is meaningless for a CFM application on Mac OS 9.

Classic Mac icons are hand-placed pixels rather than scaled art, so the drawing
is code. `Resources/Gazette_icon.r` is generated and should not be hand-edited:

```bash
python3 tools/generate_icon.py --out Resources/Gazette_icon.r --ascii
```

If a freshly built copy shows a generic application icon, the desktop database
has not caught up — rebuild it by holding Command-Option through startup, or
move the application to another folder and back.

The window's own small icons — the sidebar's three standing folders, the mark
on a starred headline, and one per toolbar button — are drawn the same way,
into `Resources/Gazette_ui_icons.r`:

```bash
python3 tools/generate_ui_icons.py --out Resources/Gazette_ui_icons.r --ascii
```

Everything else the window draws is a *system* icon taken from Icon Services
by constant, because a reader already knows what a folder means.

## Preferences

Gazette keeps its settings and feed list in a plain text file called
**Gazette Preferences**, in the System Folder's Preferences folder. It is
created on first launch with Google News Top Stories in it, and is meant to be
hand-edited — SimpleText opens it directly.

```
refresh-minutes = 30      # 0 = manual refresh only
max-articles    = 100     # per feed
full-text       = 0       # 1 = also fetch and strip the article page

feed     = https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en | Google News - Top Stories
feed     = https://www.macrumors.com/macrumors.xml | MacRumors | https://www.macrumors.com/
feed-off = https://example.com/feed.xml | A feed that is switched off
```

`#` or `;` starts a comment, the separator is `=` or `:`, keys are
case-insensitive, and a feed line is `<url> | <title> | <home page>` with the
title and the home page optional. The lines are in the sidebar's order: a
`group = Name` line (or `group-closed = Name`) opens a group, the feed lines
after it belong to it, and a `group-end = 1` line closes it, so that a feed
below a group can be a top-level feed rather than the group's last. The home page is the site the feed is for,
learned from the feed itself on its first refresh and written back here; it is
what the sidebar's Open Home Page and Copy Home Page URL use.
Gazette writes CR line endings the way OS 9 text files do, but reads CR, LF and
CRLF alike so an edit from another machine still loads.

A file that names any feed at all replaces the built-in list outright, so
deleting Google News from it sticks.

## Testing

The portable modules — the preference grammar, the ASCII transliteration, and
the feed parsing as it lands — include no system headers, so they build and run
with a plain host compiler:

```bash
make -C tests/host
```

Everything that touches the Toolbox (`store/`, `ui/`, `main.cpp`) is deliberately
absent from that build. CI runs it as a gate before the Retro68 job.

## Networking

All HTTPS goes through [Certainly](https://github.com/minorbug/certainly) and
BearSSL, vendored at `third_party/certainly/` as a byte-for-byte copy of
[Gateway](https://github.com/brunocastello/Gateway)'s tree plus the two Carbon
patches below, so fixes transfer between the projects with a plain `diff`.
Gazette does not reimplement TLS (AGENT.md constraint 4).

The plain-HTTP path is Certainly's own Open Transport client, driven directly.
Gateway needs a full OT layer of its own because it is a proxy — it listens,
accepts, and upgrades live plaintext connections to TLS in place. Gazette makes
outbound client connections and nothing else, and `certainly_transport.h`
already provides exactly that, so Gateway's ~450 lines of endpoint plumbing have
no counterpart here.

### What Carbon changes about Open Transport

Two things, and only one of them is a source change.

**`OTCARBONAPPLICATION=1`.** `OTOpenEndpoint`, `OTOpenInternetServices` and
`InitOpenTransport` all read *"CarbonLib: not available"* — a Carbon application
must call the `OTClientContext`-taking `*InContext` forms. Apple's headers
already define the plain spellings as macros forwarding to those with a `NULL`
context, but only inside `#if OTCARBONAPPLICATION`, which defaults to `0`. So
the whole adaptation is that one definition, set on the `certainly` target in
`CMakeLists.txt`. Without it nothing compiles.

**UPPs are opaque under Carbon.** `OTInstallNotifier` takes an `OTNotifyUPP`.
On classic PowerPC that collapses to the procedure pointer itself, so passing a
notifier directly has always worked; under Carbon `OPAQUE_UPP_TYPES` is on and
the call needs `NewOTNotifyUPP()`. See `third_party/certainly/PATCHES.md` §21–23
for this and the `LMGetTicks()` change in the entropy pool.

### Fetching

`src/net/gazette_fetch.c` is a non-blocking GET: connect, send, read the head,
read the body, following up to five redirects. It is driven a slice at a time
from the `WaitNextEvent` idle branch, so the UI never stops. The whole fetch
lives in one `NewPtrClear` block taken at the start, and the body streams to a
callback rather than accumulating — a front-page feed is 100–200 KB, and holding
one in memory to hand over at the end would be the largest allocation in the
application.

Bodies are framed three ways: chunked, `Content-Length`, or delimited by the
connection close. The third is why the stream layer distinguishes EOF from
error at all.

## Feeds

### Parsing

`src/feeds/gazette_feed_parse.c` is a scanner, not an XML parser. It does not
validate, ignores namespace prefixes (`<atom:link>` and `<link>` are the same
element), and knows nothing of document structure beyond *am I inside an item*.
Feeds in the wild are not well-formed often enough for strictness to be a
virtue, and the failure mode of a strict parser — no headlines at all — is worse
than that of a lax one.

It is incremental because the fetch delivers 4 KB at a time and a front-page
feed is 100–200 KB. One article is held in progress and emitted when its closing
tag arrives, so the largest thing in memory is one article and one element's
text. Every token may be split across a chunk boundary — a tag name, an entity,
the CDATA terminator itself — and the host tests parse each document whole, in
7-byte chunks, and one byte at a time to prove it.

RSS 2.0 and Atom share the code because the difference is only which elements
carry what: Atom puts the article URL in a `href` attribute and RSS in the
element's text, and `rel="self"` has to be told from `rel="alternate"` or the
article link becomes the feed's own.

Text is put through the Mac OS 9 pipeline in an order that matters:

1. **Entities decoded** to UTF-8 — `&lt;b&gt;` is not markup until it is decoded.
   One pass only: feeds double-escape constantly, and `&amp;lt;` means the *text*
   `&lt;`, not a tag.
2. **Markup stripped** — what step 1 revealed.
3. **Transliterated** to ASCII — NewsProxy's table (constraint 8).
4. **Whitespace flattened.**

Dates are RFC 822 for RSS and ISO 8601 for Atom, in the shapes feeds actually
send: no day name, one-digit days, two-digit years, named zones, fractional
seconds. Unreadable is `0` rather than a guess — an article dated by accident
sorts wrong forever, and a blank date column is honest.

### Google News

The country map and the 184 curated topics come from
[NewsProxy](https://github.com/brunocastello/NewsProxy), which reverse-engineered
them against the original Newsstand 1.1 client. NewsProxy keys them by the
base64 `CAAq…` topic IDs that client sent, because NewsProxy *answers* that
client; Gazette never speaks the Newsstand protocol, so only what those IDs
resolved to is kept — a section name or a search query. That is the part that was
actually discovered, and the part that goes stale if Google changes it.

`src/feeds/gazette_gnews_topics.c` is generated:

```bash
python3 tools/generate_gnews_topics.py     --newsproxy ../NewsProxy/newsProxy.py     --out src/feeds/gazette_gnews_topics.c
```

### Custom feeds

Any RSS 2.0 or Atom URL can be added to `Gazette Preferences` as a `feed` line
(see [Preferences](#preferences)). Auto-discovery — pasting a site's home page
and finding its feed from the `<link rel="alternate">` tag — is implemented in
the parser and waits on the Phase 4 feed-management UI to be reachable without
editing the file.

## The window

```
+---------------+-----------------------------------+
| Feeds         | Google News - Top Stories (38)  |^|
|---------------|---------------------------------| |
| Google News   | Sep 08 21:30  A headline        | |
| Example Blog  | Sep 08 20:12  Another headline  |v|
|               |=================================+=+   <- draggable
|               | A headline                      |^|
|               | CNN - Sep 08 21:30              | |
|               |                                 | |
|               | The summary the feed carried... |v|
+---------------+-----------------------------------+
| 38 articles from Google News - Top Stories        |
+---------------------------------------------------+
```

Clicking a feed reads its **cache** first and only fetches when there is
nothing cached, so after the first fetch moving through the sidebar is instant
and works with the machine unplugged. Command-R forces a refresh of the feed
you are looking at. Arrow, page, home and end keys move the headline selection.
Both dividers drag.

Auto-refresh follows the `refresh-minutes` preference and is deliberately
modest: it refreshes the feed being looked at, and only when nothing else is
running. Walking the whole list in the background means a queue, several
connections and a policy for partial failure, which is Phase 4's problem.

### What draws it

The window is a **control hierarchy**, not a set of rectangles the application
draws into. There is a root control, and the sidebar and the headline list are
real **List Box controls** — CDEF 22, through `CreateListBoxControl`. Each one
draws its own Platinum frame and focus ring, carries its own scroll bar, tracks
its own clicks and moves its own selection; `SetKeyboardFocus` decides which of
them the keyboard is talking to, and the CDEF shows it.

Only what goes *inside a cell* is Gazette's, through a list definition function
handed to `CreateListBoxControl` as a `ListDefSpec` of `kListDefUserProcType` —
a callback in `platinum_window.c` rather than an `LDEF` code resource, which
Carbon does not allow anyway, and without `RegisterListDefinition`, which would
cost CarbonLib 1.5. The cells carry no data. Each list is a view onto something
the engine already holds in order, so a cell's row number *is* its index into
it: the sidebar's rows come from `GazetteCoreSidebarRowAt`, the headlines from
`GazetteFeedsArticleAt`. A copy in the cells would only be a second thing to
get out of date.

The strip over each list is a **window header control** — CDEF 21's list-view
variant, which is what Platinum puts above a list. It carries no title of its
own, so the text is drawn on top of it. The status line along the bottom is
deliberately *not* a control: Outlook Express leaves that strip flat on the
window background, and a placard's bevel there would be a box around something
that is not a button.

The reader is a **user pane control** with drawing, tracking and focus
procedures, holding a styled TextEdit record. Being a control means it draws
when the Control Manager says to, gets its own clicks, has a real
`DrawThemeFocusRect` ring, and sits in the Tab order — so **Tab** is
`AdvanceKeyboardFocus` and **Shift-Tab** `ReverseKeyboardFocus` across all
three panes rather than a switch statement in the application.

Every font is the system's, asked for once at startup and never assumed:
`kThemeSystemFont` for the chrome (Charcoal 12 on a stock Mac OS 9 — the
system's own font at the system's own size), `kThemeViewsFont` for the two
lists (Geneva 10, what the Finder sets a list view in), and the application
font at `GetDefFontSize()` for the article, which is the one place on screen
holding prose rather than labels.

Nothing is sized around those fonts by hand: the header bar and the status bar
are as tall as the chrome font needs, the **row height and baselines follow
from the views font**, and the headline list's date column is measured from
`"Sep 00 00:00"`. Text is truncated with `TruncText`, the Script Manager's own,
which knows where a character ends.

The sidebar follows Outlook Express 5's folder list. Rows carry a small icon
from **Icon Services** — the system's own, a folder for a group (opened when
the group is) and `kInternetLocationNewsIcon` for a feed, which is what Mac OS
9 already uses to mean a news source at a URL; a switched-off feed is plotted
with `kTransformDisabled`. A selected row draws a box around the **name**, not
a bar across the row: filled with the highlight colour when the list has the
focus, outlined when it does not. The headline list keeps a full-width row
highlight, which is what OE's message list does.

A selected row inverts with the **highlight colour** the user chose in the
Appearance control panel, not with black: `LMSetHiliteMode` clears the hilite
bit immediately before the `InvertRect` that wants it. The disclosure triangles
are `DrawThemeButton` with `kThemeDisclosureButton`, the window background is
`kThemeBrushDocumentWindowBackground` (a document window, not a dialog) and the
list backgrounds `kThemeBrushListViewBackground`. The grow box is drawn with
`DrawGrowIcon`, and the status line stops short of it.

What is still drawn by hand is only what a list definition function and a user
pane drawing procedure must draw: the text inside a cell, and the background
behind it. There are no hand-drawn widgets left.

The article's three weights — the title bold at 9, the byline plain at 9, the
body at 10 — are three TextEdit style runs, applied to ranges that are already
in the record rather than set on an insertion point and left for the next
`TEInsert` to pick up. A drag selects, and **Edit ▸ Copy** copies: `TECopy`
onto TextEdit's private scrap, then `TEToScrap` to move it to the system's
(`ZeroScrap` is not in Carbon, so `ClearCurrentScrap` comes first). The record
is kept deactivated unless a drag is actually selecting something — an active
TextEdit record with an empty selection draws an insertion point, and a caret
in a pane that cannot be typed into is a lie about what the pane is.

### Two Carbon details

The reader's scroll bar is built with `NewControl` and
`kControlScrollBarProc` rather than `CreateScrollBarControl`, which is the
older spelling and does the same thing. Its action procedure is a real UPP —
`OPAQUE_UPP_TYPES` again — made with `NewControlActionUPP`. It scrolls the
pane directly rather than invalidating, because it runs inside
`TrackControl`'s own loop where an update event would not be seen until that
returned. Each list's bar is inside its List Box control and the CDEF tracks
it, so clicks are routed with `FindControlUnderMouse` and `HandleControlClick`
and the lists never need hit-testing by hand.

Styled text has no one line height to count in, so the reader's bar is
measured in **pixels** where the lists' are measured in rows.

## The cache

One file per feed in a **Gazette Cache** folder beside the preferences, named
by an FNV-1a hash of the feed's URL. The URL is the only stable thing about a
feed — its position changes when the file is reordered, its title when it is
renamed — and either would otherwise hand a feed another one's articles.

The format is line-oriented, one field per line behind a one-letter tag, which
is only possible because every string in the store has already been through
whitespace flattening: no embedded newlines to escape, nothing to quote. It
opens readably in SimpleText, which is worth something when the question is
*why is this feed showing the wrong articles*. Both directions stream through a
2 KB buffer.

A cache is written only after a refresh has been judged good, so a failed parse
can never be read back next launch as though it were real; and reading commits
nothing until the first complete record, so a truncated file leaves what is on
screen alone.

## Continuous Integration

`.github/workflows/build.yml` defines two jobs.

| Job | Runner | Toolchain | Artifact |
|-----|--------|-----------|----------|
| `host-tests` | `ubuntu-latest` | plain `cc` | — |
| `build` | `ubuntu-latest` | `ghcr.io/autc04/retro68` (Docker) | `Gazette-carbon-ppc-macos9` |

`host-tests` runs `make -C tests/host` in seconds and gates the build, because a
failure there is a real failure whereas a Retro68 break is usually a toolchain
question. `build` then stages the Universal Interfaces, builds with the
`retrocarbon` toolchain, packages the `.sit`, and uploads `Gazette.dsk`,
`Gazette.APPL`, `Gazette.bin` and `Gazette.sit`.

They run on push and pull request when `src/`, `Resources/`, `tests/`,
`CMakeLists.txt`, `third_party/AUI/` or the workflow itself changes, and can be
started by hand from the Actions tab (`workflow_dispatch`).

## Project Structure

```
.
├── CMakeLists.txt          # Retro68 add_application() target (Carbon / PowerPC)
├── README.md               # This file
├── AGENT.md                # Project spec: constraints, phases, coding standards
├── .gitignore
│
├── .github/workflows/
│   └── build.yml           # Retro68 Carbon PPC build + .sit packaging
│
├── Resources/
│   ├── Gazette.r           # SIZE (8 MB / 4 MB), About alert, vers
│   ├── Gazette_icon.r      # Generated icon family + BNDL/FREF — do not hand-edit
│   ├── Gazette_ui_icons.r  # generated — see tools/generate_ui_icons.py
│   └── Strings.r           # Placeholder for localization (not yet in the build)
│
├── tools/
│   ├── generate_icon.py         # Draws the icon and emits Gazette_icon.r
│   ├── generate_ui_icons.py     # The sidebar's and toolbar's small icons
│   └── generate_gnews_topics.py # Emits the Google News topic table
│
├── src/
│   ├── main.cpp            # Carbon shell: CreateNewWindow, WaitNextEvent, menus
│   ├── core/               # Thin C seam (MacTypes.h only) — owns the live prefs
│   │   └── gazette_core.h/.c
│   ├── ui/                 # The Platinum window — every Toolbox UI call
│   │   ├── platinum_window.h/.c
│   │   └── gazette_dialogs.h/.c      # DLOG/DITL modal dialogs
│   ├── net/                # Stream over Certainly's OT client; HTTP fetch
│   │   ├── gazette_net.h/.c
│   │   └── gazette_fetch.h/.c
│   ├── feeds/              # Feed engine (parsing is portable, host-tested)
│   │   ├── gazette_feeds.h/.c        # article store, refresh, full text, search
│   │   ├── gazette_index.h/.c        # read articles + per-feed unread counts
│   │   ├── gazette_feed_parse.h/.c   # incremental RSS/Atom, auto-discovery
│   │   ├── gazette_googlenews.h/.c   # country map, URL building
│   │   └── gazette_gnews_topics.c    # generated — see tools/
│   ├── extract/            # HTML to readable text — portable, host-tested
│   │   └── gazette_extract.h/.c   # article-page extractor + tag knowledge
│   ├── store/              # The only File Manager calls in the application
│   │   └── gazette_store.h/.c        # preferences + the per-feed cache
│   ├── prefs/              # Settings + feed list, portable and host-tested
│   │   ├── gazette_prefs.h/.c
│   │   └── gazette_opml.h/.c         # OPML import/export text
│   └── portable/           # Pure C, host-tested
│       ├── gazette_portable.h/.c   # strings, prefs grammar, ASCII transliteration
│       ├── gazette_url.h/.c        # URI splitting, redirect resolution
│       └── gazette_http.h/.c       # GET building, response parsing, chunked
│
├── third_party/
│   ├── AUI/                # Apple Universal Interfaces 3.4 (Carbon headers, CarbonLib)
│   └── certainly/          # TLS 1.2/1.3 + BearSSL, vendored from Gateway
│
└── tests/host/             # Host tests for the portable modules (Linux/macOS)
    ├── Makefile
    └── run_tests.c
```

## Implementation Phases (per AGENT.md)

| Phase | Status | Description |
|-------|--------|-------------|
| 0 | **Done** | Skeleton: Carbon shell, window, menus, WaitNextEvent loop, quit, SIZE resource, Finder icon, preferences/feed list on disk, host-test target |
| 1 | **Done** | Networking: Certainly/BearSSL vendored and building under Carbon, non-blocking stream, HTTPS GET with redirects driven from the event loop |
| 2 | **Done** | Feed engine: incremental RSS 2.0 / Atom parser, Google News country and topic maps, feed auto-discovery, headline list |
| 3 | **Done** | Platinum UI: sidebar + headline list + reader pane, scroll bars, draggable dividers, on-disk cache, auto-refresh |
| 4 | **Done** | Polish: feed and group management with auto-discovery, full-text fetch with block-aware extraction, search, read/unread with per-feed unread counts, readable groups, OPML import/export, keyboard navigation |
| 5 | **Done** | Native controls: a root control with real List Box controls for the sidebar and headline list, a styled TextEdit record for the article with selection and Copy, Appearance Manager disclosure triangles, theme highlight colour, real keyboard focus |

## Known limitations

**Google News article links are not resolved.** A `news.google.com/rss/articles/…`
link is a redirector that resolves with JavaScript rather than with an HTTP
redirect, so the full-text option cannot reach the publisher for those feeds.
It fails the way it should: the interstitial extracts to nothing, the page is
judged to hold no article, and the feed's own summary stays on screen.
Decoding those URLs is the NewsProxy technique noted in `AGENT.md`.

## Carbon on Mac OS 9 — what changes

Carbon is not a different UI toolkit; it is the subset of the Toolbox that survived
into Mac OS X, plus some newer replacements. Practical consequences for this codebase:

| Aspect | Classic Toolbox | What Gazette does |
|--------|-----------------|-------------------|
| Startup | `InitGraf`, `InitWindows`, `InitMenus`, `MaxApplZone` | None of these exist (`CALL_NOT_IN_CARBON`); CarbonLib sets up before `main()` |
| Windows | `NewCWindow`, or `GetNewCWindow` from a `WIND` | `CreateNewWindow()` with a window class and attributes |
| Menus | `GetNewMBar` from `MBAR` / `MENU` resources | `NewMenu` + `AppendMenu`, built programmatically |
| Events | `WaitNextEvent` + `EventRecord` | Same — CarbonLib supports it, and it keeps one cooperative loop for network polling |
| Apple Events | Optional for many apps | Required suite handled (`quit`, `oapp`, `rapp`), so a dock or the Finder can quit Gazette |
| File dialogs | `StandardGetFile` / `StandardPutFile` | `CALL_NOT_IN_CARBON`; OPML uses Navigation Services (`NavGetFile` / `NavPutFile`) |
| Ports | `SetPort((GrafPtr)window)` | `SetPortWindowPort()`; `WindowRef` is opaque |
| Closing | `CloseWindow` | `DisposeWindow` — `CloseWindow` is not in Carbon |
| Memory | SIZE resource | Still required on OS 9, and CarbonLib needs more headroom |
| Icons | Resource fork | Unchanged — resource fork, not `.icns` |

## References

- [Retro68](https://github.com/autc04/Retro68) — the cross-compiler and Rez this project builds with
- [Carbon Framework](https://developer.apple.com/library/archive/documentation/Carbon/Conceptual/CarbonOverview/CarbonOverview.html) — Apple's Carbon API documentation
- [Gateway](https://github.com/brunocastello/Gateway) — Networking/TLS stack (Certainly/BearSSL)
- [NewsProxy](https://github.com/brunocastello/NewsProxy) — Feed intelligence, Google News maps
- [Alex Robb's Newsstand 1.1](https://github.com/alexrobb/newsstand) — Original inspiration

## License

Copyright © 2026 brunocastello. All rights reserved.

Gazette is a standalone application. It reuses networking and feed-handling
intelligence from Gateway and NewsProxy respectively, but does not need to
speak the old Newsstand XML protocol unless explicitly added later.
