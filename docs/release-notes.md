<!-- This file is the release body: build.yml passes it to `gh release` as
     --notes-file. Keep it short — it is the page a person reads before
     downloading, not a changelog. Paragraphs stay on one line, because
     GitHub renders release bodies with newline-to-break. -->

Gazette is an RSS and Atom reader for Mac OS 9 on PowerPC: a native Carbon application that fetches feeds over modern TLS itself and reads the whole article, pictures included.

## What's in 0.1.0

The first release. Feeds in groups, dragged into order; Today, All Unread and Starred across every feed; import and export as OPML. It starts with Google News Top Stories, which can be removed like any feed.

**The article, not the summary.** Opening a headline fetches the page it links to and extracts the story — the page's own header and footer left out — with headings, emphasis, lists and links kept and up to three pictures drawn in place.

**A window of the system's own controls.** Sidebar, headlines and article with draggable dividers, a toolbar, contextual menus, and a Preferences window for how often to refresh and how much to keep. Its size, position and columns are remembered.

Everything is cached, so the window opens readable and works with the machine unplugged.

## Requirements

Mac OS 9 on PowerPC with CarbonLib 1.1 or later and QuickTime, both of which every Mac OS 9 release ships. 8 MB of memory preferred, 4 MB minimum. If the Finder shows a generic icon at first, the desktop database has not caught up: move the application to another folder and back.

## Downloads

The `.sit` archive for real hardware, or the `.dsk` disk image to mount in an emulator.
