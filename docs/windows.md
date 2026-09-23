# Gazette on Windows 95 through XP

One 32-bit executable, cross-compiled with MinGW-w64 on CI (`.github/workflows/windows.yml`), for Windows 95, 98, Me, NT 4.0, 2000 and XP. NT 3.51 and earlier are out: tree and list views arrived with Windows 95 and NT 4.0.

The engine is the Mac's, compiled unchanged. Only the shell (`src/win/`), the network start-up (`src/net/gazette_net_win32.c`) and, next, the file store are Windows code.

## The event and network polling model

The same model as Mac OS 9's `WaitNextEvent` loop: one thread, and the network advances only from the loop's idle point, a small slice at a time, never blocking.

```
for (;;) {
    while (PeekMessage(...))  handle it (WM_QUIT ends the loop)
    PumpNetwork();            refresh, full text, photos: one slice each
    MsgWaitForMultipleObjects(0, NULL, FALSE, 100 ms, QS_ALLINPUT);
}
```

- `GetMessage` is not used. It sleeps until the next message, so nothing would pump the network while the user sat still.
- The 100 ms wait is the Mac's `kSleepTicks` of 6. The same pace on both systems means the pumps, which were tuned on the Mac, behave the same.
- `MsgWaitForMultipleObjects` with no handles is exactly "sleep until input or timeout", and is in USER32 from Windows 95 and NT 3.1 on.
- `PumpNetwork` in `gazette_win_main.c` is empty until the store and the refresh join the Windows build; the Mac's `PumpNetwork` in `src/main.cpp` is what it will call.

Nothing below the loop blocks. Name resolution is the one call Winsock 1.1 only offers blocking (`gethostbyname`), and Certainly's `transport_win32.c` runs it on a short-lived thread and polls a flag; that thread is the only one, and it never touches Gazette's state.

## The network

| Layer | Mac OS 9 | Windows |
|---|---|---|
| HTTP fetch (`gazette_fetch.c`) | shared | shared |
| Streams, plain and TLS (`gazette_net.c`) | shared | shared |
| Start-up, clock, allocation | `gazette_net_ot.c`: Open Transport, `TickCount`, `NewPtrClear` | `gazette_net_win32.c`: `WSAStartup` (Winsock 1.1), `GetTickCount` kept as a running total in sixtieths, `calloc` |
| TCP | Certainly `transport_ot.c` | Certainly `transport_win32.c` (non-blocking sockets, `select`) |
| TLS 1.3 / 1.2 | Certainly + BearSSL | the same, byte for byte |
| Entropy | Certainly `entropy.c` | Certainly `entropy_win32.c` |

Imports are limited to what a plain Windows 95 has: WSOCK32 (never WS2_32), KERNEL32, USER32, GDI32, COMCTL32 up to 4.0, msvcrt. CryptoAPI is looked up at run time rather than imported (Certainly `PATCHES.md` §25), and BearSSL's own CryptGenRandom seeder is compiled out. The workflow fails the build if any of those imports appears.

`GazetteNetTest.exe`, built from `tests/win/gazette_nettest.c` with the same engine, is run under Wine on every Windows build against live servers over TLS 1.3 and TLS 1.2. That is the proof the network path works on Windows until a real machine has run it.

The user-agent names the platform: `Gazette/0.1.0 (Windows; Win32)` (`GAZETTE_WIN32`, set by `cmake/GazetteWindows.cmake`).

## Where settings and the cache live

**Beside `Gazette.exe`**, as Gateway keeps its settings on Windows. It is the one place that exists and is known on every version from 95 to XP, needs no shell32 call newer than 95 (`SHGetSpecialFolderPath` is IE 4), and makes Gazette a program you copy anywhere and delete to uninstall.

| Mac OS 9 (Preferences folder) | Windows (beside Gazette.exe) |
|---|---|
| `Gazette Preferences` | `Gazette Preferences.txt` |
| `Gazette Cache` folder | `Gazette Cache` folder |
| `Gazette Cache:Feed XXXXXXXX` | `Gazette Cache\Feed XXXXXXXX` |
| `Gazette Cache:Gazette Index` | `Gazette Cache\Gazette Index` |

- The preferences file gets `.txt` so double-clicking opens it in Notepad; it stays hand-editable, in the same grammar as the Mac's.
- **Written with CRLF, read with anything.** The engine writes bare CR, as Mac OS 9 wants; the Windows store turns each CR into CRLF on the way to disk, because Notepad on 95 to XP shows a CR-only file as one line. Reading already accepts CR, LF and CRLF.
- The cache file names and their format are the Mac's, so a cache folder can be copied between the two.

**When the folder beside the executable cannot be written** (a CD, a write-protected floppy, or Program Files for a user without rights on NT, 2000 or XP), Gazette looks, in order, at:

1. `%APPDATA%\Gazette` (2000 and XP set `APPDATA`);
2. `%USERPROFILE%\Gazette` (NT 4.0 sets `USERPROFILE`, and has no `APPDATA`);
3. none: Gazette runs, reads nothing and says in the status line that it cannot save.

The choice is made once at launch, by trying to create the file, not by reading permissions, which differ too much between 95 and NT to be worth asking.

**Moving a feed list between systems** is OPML: File ▸ Export Feeds… on one, Import Feeds… on the other. The OPML code is the engine's, so both builds read and write the same file. Titles typed with accents on the Mac are MacRoman bytes in the exported file, which says UTF-8; feed titles Gazette read from feeds are already ASCII, so this touches only titles typed by hand.

## What the next stages must carry across

From the portable-core audit of 2026-09-23 (nothing in the portable modules is Mac-only; these are behaviour to reproduce at the seams):

- **"Now" and article dates share one clock.** On the Mac, "now" is local time (`GetDateTime`) and articles are UTC plus the time zone (`ReadLocation`). The Windows `gazette_feeds.c` seam must do the same with `GetLocalTime` or the time-zone bias, or Today and Yesterday shift by the zone.
- **Paragraphs are `\n`.** Win32 edit controls want `\r\n`; the reader pane converts.
- **The reader's marks** (bytes 1 to 6 and 14 to 17, `GazetteIsMark` in `gazette_extract.h`) are photos, headings, list items, quotes, bold, italic and links, and the Windows reader pane interprets them as the Mac's does.
- **`core/gazette_core.h` includes `MacTypes.h`** only for `Boolean`; Windows needs a typedef there.
- `gazette_feeds.c`, `gazette_photos.c` and `gazette_store.c` still reach for the Toolbox (memory, clock, time zone, File Manager, Navigation Services) and join the Windows build with the store.
