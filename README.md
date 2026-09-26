# Gazette

> [!CAUTION]
> ### 🤖 AI-Generated Code Ahead
> **This entire repository was crafted with the help of AI.**
>
> If you are allergic to neural networks, synthesized functions, or autocomplete on steroids, **this repository is not for you**.
> You are kindly invited to close this page and write your code line-by-line in `ed` or `vim`. Everyone else, enjoy!

An RSS and Atom reader for Mac OS 9 and for Windows 95 through XP, in the
spirit of Newsstand.

<img width="1173" height="703" alt="Screenshot 2026-09-19 at 4 25 51 PM" src="https://github.com/user-attachments/assets/8b692f8d-d942-4ac2-b796-7b7e616a5687" />

Gazette is a native Carbon application for PowerPC Macs, and a native Win32
application for every Windows from 95 to XP. It fetches feeds over modern TLS
itself, reads the whole article rather than the summary, shows the pictures in
it, and looks at home — the three-pane window, the toolbar and the controls
are all the system's own: Platinum on the Mac, the common controls on
Windows, themed by XP where XP is running it.

Both builds are one program: the feeds, the article extractor, the cache and
the preferences are the same C code on each, and only the window around them
is written twice.

> A hobby project pointed at 25-year-old operating systems, one of them with
> no memory protection. Research software, no warranty.

## What it does

- **Feeds**: RSS 2.0 and Atom, with auto-discovery from a site's address.
  Feeds live in groups; drag them to reorder. Today, All Unread and Starred
  are standing views across every feed.
- **The article**: opening a headline fetches the page it links to and
  extracts the story — headline, byline and furniture left out — with
  headings, emphasis, lists and links kept, and its pictures drawn in
  place — three by default, up to ten in Preferences. The feed's summary stands in when the page cannot be had.
- **Offline**: every feed is cached, so the window is readable the moment it
  opens and works with the machine unplugged. Read and starred marks survive
  a refresh.

## Setting it up

It starts with Google News Top Stories; **File ▸ New Feed…** adds a feed by
address, **Import Feeds…** reads an OPML file, **Export Feeds…** writes one —
which is also how a feed list moves from a Mac to a PC and back. You can
remove the Google News feed if you wish.

**Edit ▸ Preferences…** sets how often feeds refresh, how many articles of
each are kept, and how many photos an article shows. **View ▸ Hide Photos**
stops fetching pictures — worth it on a modem.

### Mac OS 9

Put Gazette anywhere and open it. The Command keys are the menus' own:
⌘N for a new feed, ⌘; for Preferences. Every setting is also a line in
`Gazette Preferences`, a plain text file in the Preferences folder that stays
hand-editable; the article cache is the `Gazette Cache` folder beside it.

### Windows

Run `Setup.exe`: it installs to `C:\Gazette` with a Start menu folder and an
uninstaller, and keeps your feed list when you install a newer version over an
older one. Or skip the installer and run `GAZETTE.EXE` straight from the zip
or the floppy image. The Mac's Command keys are the same letters on Ctrl.

The settings are `Gazette.ini` and the article cache is the `Cache` folder,
both beside `Gazette.exe`; the .ini is the same hand-editable text as the
Mac's file. Where that folder cannot be written — a CD, a locked floppy —
they go to your Application Data folder on 2000 and XP, or your profile
folder on NT 4.0; on 95, 98 and Me Gazette says it cannot save rather than
losing your feeds quietly.

Windows has no search field in the toolbar: **Edit ▸ Search…** (Ctrl+F)
filters the headlines, and **Clear Search** — on the toolbar, in the Search
window, or Esc — shows them all again.

## Requirements

**Mac OS 9** on PowerPC with **CarbonLib 1.1 or later** and QuickTime, both
of which every Mac OS 9 release ships. 8 MB of memory preferred, 4 MB minimum.
No 68K, no Intel, no Mac OS X.

**Windows 95, 98, Me, NT 4.0, 2000 or XP**, one binary for all of them, with
TCP/IP installed. Windows 95 needs OSR2, or an earlier 95 that has had
Internet Explorer 3 or later, for the C runtime (`MSVCRT.DLL`) Gazette uses.
NT 3.51 and earlier are out of reach: the tree and list controls the window
is built from arrived with 95 and NT 4.

## Building

You do not build this locally. Push, and GitHub Actions does it. The Mac
workflow runs the Retro68 container against Apple's Universal Interfaces
(vendored in `third_party/AUI/`), runs the host tests for the portable parts,
and uploads a disk image and a StuffIt archive. The Windows workflow
cross-compiles with MinGW-w64, runs the network, file-store and engine tests
under Wine, checks that nothing newer than Windows 95 is imported, and builds
the installer with NSIS, a zip and a floppy image. Tags become releases, with
both builds' files on one release page.

## Layout

```
src/portable/   HTTP, URLs, prefs grammar, transliteration — no system headers
src/prefs/      the preferences and OPML
src/feeds/      RSS/Atom parsing, the store, Google News, photos
src/extract/    the article out of its page
src/net/        Open Transport and TLS, one fetch at a time
src/store/      the only file calls: File Manager on the Mac, Win32 on Windows
src/app/        what a command means, shared by both shells
src/ui/         the Mac's window and dialogs
src/main.cpp    the Mac's shell: menus, events, the one cooperative loop
src/win/        the Windows shell, window, reader and dialogs
third_party/certainly/   Certainly over BearSSL, vendored — see its PATCHES.md
third_party/stb/         stb_image, for the photographs on Windows
installer/      the Windows installer (NSIS)
tools/          the icon generators and the Google News topic table
```

## Contributors

- [Bruno Castelló](https://github.com/brunocastello) — the idea, the
  direction, every decision about how it should look and behave, and the
  testing on the real thing.
- [Claude](https://claude.com/claude-code) (Anthropic's Claude Opus 5 and 5.5,
  by way of Claude Code) — the code, written in conversation with Bruno across
  every phase, from the first `WaitNextEvent` loop to the article extractor
  and the Windows port.

## Thanks

[Newsstand](https://getnewsstand.com) by Alex Robb is the reader this one
grew up reading, and [NewsProxy](https://github.com/brunocastello/NewsProxy)
is where how Newsstand feeds and articles parsing were worked out.
[Certainly](https://github.com/minorbug/certainly) and
[BearSSL](https://bearssl.org) do the cryptography, by way of
[Gateway](https://github.com/brunocastello/Gateway).
[Retro68](https://github.com/autc04/Retro68) makes a Mac OS 9 binary from a
modern toolchain.

On Windows, the photographs in an article are decoded by Sean Barrett's
[stb_image](https://github.com/nothings/stb) — Windows 95 has nothing of its
own that reads a JPEG — and it was [roytam1](https://github.com/roytam1) who
pointed us to it. [MinGW-w64](https://www.mingw-w64.org) builds the one
binary that runs from 95 to XP, [NSIS](https://nsis.sourceforge.io) builds
its installer, and [Wine](https://www.winehq.org) runs its tests on every
push.

## Licence

MIT — see [LICENSE](LICENSE). Copyright © 2026 Bruno Castelló.

The third-party code keeps its own terms: Certainly and BearSSL are MIT,
stb_image is public domain (or MIT), and Apple's Universal Interfaces are
redistributed under Apple's SDK licence and are not covered by the MIT grant.
