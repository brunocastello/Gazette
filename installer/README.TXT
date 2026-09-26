Gazette - RSS / Atom reader
Windows 95, 98, Me, NT 4.0, 2000 and XP

Gazette reads RSS and Atom feeds, fetches the whole article rather than
the summary, and shows its photographs. It fetches over modern HTTPS by
itself; nothing else needs to be installed.

INSTALLING

Run SETUP.EXE. It installs to C:\GAZETTE with a Start menu folder and an
uninstaller (also in Add/Remove Programs). Installing a newer version over
an older one keeps your feed list.

Or copy GAZETTE.EXE anywhere and run it: it is one file and needs no
installation.

YOUR FILES

Gazette keeps two things beside GAZETTE.EXE:

  Gazette.ini   your feeds, groups and settings - plain text, and safe to
                edit in Notepad while Gazette is closed
  Cache         the articles already fetched, so Gazette is readable with
                the network off

If that folder cannot be written (a CD, a write-protected floppy), 2000
and XP keep them in your Application Data folder and NT 4.0 in your
profile folder; on 95, 98 and Me Gazette says it cannot save.

File > Export Feeds writes your feed list as OPML, which Gazette on a Mac
- or any other feed reader - can read with Import Feeds.

GETTING STARTED

Gazette starts with Google News Top Stories. File > New Feed adds a feed
by its address (a site's home page works too). Edit > Preferences sets how
often feeds refresh, how many articles are kept and how many photos an
article shows (1 to 10). Edit > Search filters the headlines; Clear
Search, or Esc, shows them all again. Right-click a feed, a headline or
the article for more.

REQUIREMENTS

Windows 95 OSR2 or later (an earlier Windows 95 needs Internet Explorer 3
or later, for MSVCRT.DLL), 98, Me, NT 4.0, 2000 or XP, with TCP/IP.

Gazette is MIT licensed; see LICENSE.TXT.
https://github.com/brunocastello/Gazette

Copyright (c) 2026 Bruno Castello
