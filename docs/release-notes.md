<!-- This file is the release body: build.yml and windows.yml pass it to
     `gh release` as --notes-file. Keep it short — it is the page a person
     reads before downloading, not a changelog. Paragraphs stay on one line,
     because GitHub renders release bodies with newline-to-break. -->

Gazette is an RSS and Atom reader for Mac OS 9 on PowerPC and for Windows 95 through XP: a native application on each that fetches feeds over modern TLS itself and reads the whole article, pictures included.

## What's in 0.2.0

**Gazette for Windows.** The same reader for Windows 95, 98, Me, NT 4.0, 2000 and XP — one program for all of them, built from the Mac's own engine, in Windows' own controls: the sidebar a tree, a toolbar with captions, themed by XP where XP runs it. Feeds and groups dragged into order, the whole article with its photographs, selectable text, right-click menus, Preferences, OPML import and export, and a Search window in place of the Mac's search field. It installs with Setup.exe to C:\Gazette, or runs from the zip or the floppy with nothing installed. Your feeds live in Gazette.ini beside it, and an OPML file carries them between a Mac and a PC.

**On both.** Preferences now sets how many photos an article shows, from one to three. A feed you have renamed is headed with your name for it, not the one it gives itself. The article's text has a contextual menu, with Copy and Select All. Refreshing keeps the article you are reading where it was, articles come in date order, and pictures whose addresses carry an ampersand load.

The Mac's window is otherwise as 0.1.0 left it.

## Requirements

**Mac OS 9** on PowerPC with CarbonLib 1.1 or later and QuickTime, both of which every Mac OS 9 release ships. 8 MB of memory preferred, 4 MB minimum.

**Windows 95 OSR2** or later (an earlier 95 needs Internet Explorer 3 or later, for MSVCRT.DLL), 98, Me, NT 4.0, 2000 or XP, with TCP/IP.

## Downloads

**Mac:** the `.sit` archive for real hardware, or the `.dsk` disk image to mount in an emulator.

**Windows:** `-windows-setup.exe` to install; the `-windows.zip` holds the installer, the bare GAZETTE.EXE and a read-me; the `-windows.img` is a 1.44 MB floppy with the same files, for a real machine's drive or an emulator's.
