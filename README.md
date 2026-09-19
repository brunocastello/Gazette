# Gazette

An RSS and Atom reader for Mac OS 9, in the spirit of Newsstand.

Gazette is a native Carbon application for PowerPC Macs. It fetches feeds
over modern TLS itself, reads the whole article rather than the summary, shows
the pictures in it, and looks modern — the three-pane window, the toolbar, the Platinum controls are all the
system's own.

> A hobby project pointed at a 27-year-old operating system with no memory
> protection. Research software, no warranty.

## What it does

- **Feeds**: RSS 2.0 and Atom, with auto-discovery from a site's address.
  Feeds live in groups; drag them to reorder. Today, All Unread and Starred
  are standing views across every feed.
- **The article**: opening a headline fetches the page it links to and
  extracts the story — headline, byline and furniture left out — with
  headings, emphasis, lists and links kept, and up to three pictures drawn
  in place. The feed's summary stands in when the page cannot be had.
- **Offline**: every feed is cached, so the window is readable the moment it
  opens and works with the machine unplugged. Read and starred marks survive
  a refresh.

## Setting it up

Put Gazette anywhere and open it. It starts with Google News Top Stories;
**File ▸ New Feed…** (⌘N) adds a feed by address, **Import Feeds…** reads an
OPML file, **Export Feeds…** writes one. You can remove the Google News feed if you wish.

**Edit ▸ Preferences…** (⌘;) sets how often feeds refresh and how many
articles of each are kept. Everything is also a line in `Gazette
Preferences`, a plain text file in the Preferences folder that stays
hand-editable; the article cache is the `Gazette Cache` folder beside it.

**View ▸ Hide Photos** stops fetching pictures — worth it on a modem.

## Requirements

Mac OS 9 on PowerPC with **CarbonLib 1.1 or later** and QuickTime, both of
which every Mac OS 9 release ships. 8 MB of memory preferred, 4 MB minimum.
No 68K, no Intel, no Mac OS X.

## Building

You do not build this locally. Push, and GitHub Actions does it: the
workflow runs the Retro68 container against Apple's Universal Interfaces
(vendored in `third_party/AUI/`), runs the host tests for the portable
parts, and uploads a disk image and a StuffIt archive. Tags become releases.

## Layout

```
src/portable/   HTTP, URLs, prefs grammar, transliteration — no system headers
src/prefs/      the preferences and OPML
src/feeds/      RSS/Atom parsing, the store, Google News, photos
src/extract/    the article out of its page
src/net/        Open Transport and TLS, one fetch at a time
src/store/      the only File Manager calls
src/ui/         the window and the dialogs
src/main.cpp    the shell: menus, events, the one cooperative loop
third_party/certainly/   Certainly over BearSSL, vendored — see its PATCHES.md
tools/          the icon generators and the Google News topic table
```

## Thanks

[Newsstand](https://getnewsstand.com) by Alex Robb is the reader this one
grew up reading, and [NewsProxy](https://github.com/brunocastello/NewsProxy)
is where how Newsstand feeds and articles parsing were worked out.
[Certainly](https://github.com/minorbug/certainly) and
[BearSSL](https://bearssl.org) do the cryptography, by way of
[Gateway](https://github.com/brunocastello/Gateway).
[Retro68](https://github.com/autc04/Retro68) makes a Mac OS 9 binary from a
modern toolchain.

## Licence

Copyright © 2026 Bruno Castelló. All rights reserved.
