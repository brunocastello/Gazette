<!-- This file is the release body: build.yml passes it to `gh release` as
     --notes-file. Keep it short — it is the page a person reads before
     downloading, not a changelog. Paragraphs stay on one line, because
     GitHub renders release bodies with newline-to-break. -->

Gazette is an RSS and Atom reader for Mac OS 9 on PowerPC, in the spirit of Newsstand: a native Carbon application that fetches feeds over modern TLS itself, reads the whole article with its photographs, and looks like it belongs next to Outlook Express 5.

## What's in 0.1.0

The first release. Feeds in groups, dragged into order; Google News Top Stories, countries and sections; Today, All Unread and Starred across every feed; import and export as OPML.

**The article, not the summary.** Opening a headline fetches the page it links to and extracts the story — the page's own header and footer left out — with headings, emphasis, lists and links kept and up to three photographs drawn in place. A Google News item's real story is found behind Google's redirect.

**A window of the system's own controls.** Sidebar, headlines and article with draggable dividers, an Outlook Express-style toolbar, contextual menus, and a Preferences window for how often to refresh and how much to keep. Its size, position and columns are remembered.

Everything is cached, so the window opens readable and works with the machine unplugged.

## Requirements

Mac OS 9 on PowerPC with CarbonLib 1.1 or later and QuickTime, both of which every Mac OS 9 release ships. 8 MB of memory preferred, 4 MB minimum. If the Finder shows a generic icon at first, the desktop database has not caught up: move the application to another folder and back.

## Downloads

The `.sit` archive for real hardware, or the `.dsk` disk image to mount in an emulator.
