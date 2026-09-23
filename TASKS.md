# TASKS.md — Gazette

Live checklist for Claude Code / Opus 5.5 long runs.  
Update this file as work progresses. Prefer reading this over scrollback.

Legend: `[ ]` todo · `[~]` in progress · `[x]` done · `[-]` deferred / deliberate

---

## Mac OS 9 — remaining polish (post Phase 5)

Goal: release-ready quality matching the 0.1.0 release notes, with known deliberate gaps documented.

- [x] Close remaining visual polish items called out in  Phase 5 / “Beyond Phase 5” (or document as deliberate).
  Every Phase 5 item is ✅ in the pre-rename AGENT.md (`git show de48c05:AGENT.md`); the window was signed off 2026-09-18 and nothing in the source is marked unfinished. “Beyond Phase 5” held two notes: groups merge every enabled feed newest first (deliberate), and Google News links unresolved (since fixed, see next item).
- [x] Google News JS-redirect article links: best-effort decode (NewsProxy technique) **or** clear status-line fallback to feed summary.
  Decode built and correct (CI probe run 35880811279 gets the full NBC article). On the Mac it failed at the last hop: debug build said "(www.nbcnews.com: TLS connected, err 0, ssl 0, OT -3162, alert 0)". Cause: Certainly's TLS 1.2 pump called BearSSL's between-records state "handshaking" after the handshake, so MacTLS_Read refused an established connection; every TLS 1.2-only server (NBC is one) failed "while reading headers". Fixed in `f0a2ed8` (PATCHES.md §24). Builds: run 35886767625 (normal), 35886776828 (NETDEBUG). Confirmed on the Mac by Bruno, 2026-09-23: the NBC article loads in full.
- [x] 2026-09-23 detour, reverted: articles were switched to feed-text-only (a467649, 52dce8a), which was a misreading — Bruno wants FULL articles with photos, as Newsstand 1.1 shows them. Reverted in 861edbb / bc7c22f; the photo-address entity fix (146665e) stays, as it helps pages too.
- [x] Photo addresses have their entities decoded (`&#038;` → `&`): WordPress sizes were being lost on pages.
- [x] `probe.yml` (one article link through resolver + extractor), `tls-probe.yml` (one host through the vendored BearSSL), `build.yml -f net_debug=true` (transport detail on fetch failures).
- [x] Auto-refresh keeps the article being read (confirmed on the Mac by Bruno, 2026-09-23): selected by link after the list is rebuilt, reader scroll restored, its page kept through the refresh so nothing is fetched again (`GazetteUIKeepPlace`).
- [x] Video players in article pages are dropped (a whole "video" token in class/id), and so is a player's clock left as text ("00:00 00:00", "0:00 / 3:45"). General rules, no site names. Players stay out: OS 9's QuickTime 6 cannot play today's H.264/HLS web video, and Bruno chose no poster or caption in their place (2026-09-23).
- [x] CI: `actions/checkout@v5` (Node 24) in every workflow.
- [x] Gateway write-up for carrying §24 across: `docs/gateway-tls12-fix.md` (local, gitignored) and a shared doc.
- [ ] Next release is **0.2.0** (no 0.1.1): the OS 9 changes since 0.1.0 plus the Windows port. Version bump, `docs/release-notes.md` and the `v0.2.0` tag happen then.
- [x] Confirm host tests (`tests/host`) still pass on every change. Green on CI, run 35876474049 (commit 73163dd).
- [x] Confirm CI produces clean `.sit` and `.dsk`. Same run: `Gazette.sit` 451 KB, `Gazette.dsk` 1.6 MB, no compiler warnings from Gazette's own code (only Node deprecation notices from the upload action).
- [x] README + `docs/release-notes.md` accurately describe what ships.
  Checked against the code on 2026-09-23 (three pictures, ⌘; Preferences, Hide/Show Photos, Gazette Cache, Top Stories starter feed, CarbonLib 1.1 + QuickTime, 8/4 MB). README restored to its full-article wording after the revert.
- [x] No CarbonLib > 1.1 APIs introduced.
  Audited 265 Toolbox calls in the Mac sources and Certainly against AUI's `Availability:` blocks. Only five read “not available”: four OT macros that expand to `…InContext` (CarbonLib 1.0), and `NavServicesAvailable`/`LMGetTicks` in `!TARGET_API_MAC_CARBON` branches.
- [x] SIZE 8 MB preferred / 4 MB minimum; one `WaitNextEvent` loop; no `Delay`/sync OT; the only `for (;;)` loops are bounded text/layout work.
- [x] About window's modal loop now runs the same `PumpNetwork()` as the main loop. Before, photos and the auto-refresh clock stalled while About was open.
- [x] Memory and cooperative rules still hold under real-hardware / SheepShaver stress (many feeds, full-text, photos on). Not needed (Bruno, 2026-09-23).
- [x] CLAUDE.md: Bruno is rewriting it entirely (2026-09-23); earlier versions (AGENT.md included) are disregarded.

---

## Windows port (NT / 9x / Me / 2000 / XP)

Reuse Gateway’s Win32 networking and build patterns. Keep portable core pure.

### W0 – Foundation
- [x] Audit portable core for any accidental Mac-only headers or assumptions.
  2026-09-23, subagent audit spot-checked: the 17 host-tested files include only `<stddef.h>` / `<string.h>`, no Mac types, no Mac `#ifdef`s. Behaviour to carry at the seams is listed in `docs/windows.md` (CR line endings on write, local-time clock contract, `\n` paragraphs, reader marks, `Boolean` in core.h, OPML titles typed with accents are MacRoman).
- [x] Document Windows constraints and event/network polling model — `docs/windows.md` (tracked via a .gitignore exception).
- [x] CI skeleton: MinGW-w64 job that builds `Gazette.exe` — `windows.yml`, now network-capable (288 KB with TLS; floppy image 1.17 MB free). Fails the build on WS2_32, CryptoAPI, post-4.0 comctl32, libgcc_s or libwinpthread imports.

### W1 – Network + store adapter
- [x] Winsock adapter for the same non-blocking fetch API used on OS 9.
  `src/net/` shared; `gazette_net_ot.c` / `gazette_net_win32.c` hold start-up, clock and allocation. Certainly `transport_win32.c` + `entropy_win32.c` (PATCHES §25: CryptoAPI looked up, not imported). Proved by `GazetteNetTest.exe` under Wine on CI, run 35893041987: 4/4 live fetches — Google News (TLS 1.3, 38 articles parsed), NBC feed (25), www.nbcnews.com (TLS 1.2, 1.4 MB), 9to5Mac (100).
- [x] File I/O / cache path for Windows (prefs + cache folders beside the exe or under AppData — pick one, document it).
  Beside `Gazette.exe` (as Gateway), falling back to `%APPDATA%\Gazette`, then `%USERPROFILE%\Gazette`; `Gazette Preferences.txt`, `Gazette Cache\`; CRLF on write. See `docs/windows.md`.
- [x] Win32 store: `src/store/gazette_store_win32.c` — gazette_store.h on Win32 files per the above, CRLF on write, the Mac's cache-file names, GetOpenFileName / GetSaveFileName for OPML with a timer keeping the network moving while they are up. Proved by `GazetteStoreTest.exe` under Wine in windows.yml: 16/16 checks, run 35900555401.
- [x] Host tests still pass; no Mac headers in portable code. 765/765, run 35893041948.

### W2 – Minimal shell
- [~] Win32 window + message loop that polls the network layer. Loop done (PeekMessage + MsgWaitForMultipleObjects 100 ms, `PumpNetwork` idle slice); nothing to pump until the store and refresh join.
- [~] Show feed list / titles from cache or a live fetch. Engine now builds for Windows: `gazette_sys.h` seam (memory, local clock, GMT delta) with `gazette_sys_mac.c` / `gazette_sys_win32.c`; core, feeds, index and photos in the Windows engine library; `Boolean` for Windows in core.h. `GazetteEngineTest.exe` under Wine, run 35901331809: first run writes default prefs with Google News; refresh fetched and cached 38 articles; cache reloads; read state kept; second launch reads the prefs. **Next: wire the window to it**, by the decided method (2026-09-22): lift main.cpp's platform-free logic into `src/app/` piece by piece, the Mac shell calling it too, Carbon green at every step. The pieces, in order:
    1. A shared window interface, `src/app/gazette_ui.h`: the `GazetteUI*` calls `platinum_window.h` already declares (ArticlesChanged, ArticleTextChanged, SetStatus, FeedsChanged, Selection, SelectedFeed, KeepPlace, …), implemented by `platinum_window.c` on the Mac and `gazette_win_window.c` on Windows.
    2. `src/app/gazette_app.c`: ShowFeed / ShowGroup / ShowSmart / ShowArticle, the refresh queue (HandleRefreshSelection, HandleRefreshAll, AdvanceGroupRefresh, PumpRefresh, CheckAutoRefresh, TryDiscovery), PumpNetwork, the full-text and photo pumps, mark read / all / range, star, next unread, sort, hide read, search, OPML import/export, PrefsMaxArticles. Menus, dialogs, About, clipboard and OpenURL stay per shell.
    3. Windows window: sidebar rows from `GazetteCoreSidebarRowAt` (Windows' own folder and document icons from shell32.dll, loaded at run time), the Mac's two-line headline rows with date headings, the header count, the reader text (marks → formatting), status line, menus enabled as their commands land.
    4. Photos on Windows need a JPEG decoder (no QuickTime): look at OleLoadPicture (IE3+) or a small decoder; decide before building.
- [x] Quit cleanly; basic menus. (From the windows-port skeleton: menus mirror the Mac's; Quit, About, Hide Sidebar/Toolbar wired, the rest greyed.)
- [x] Fix the faults from the 86Box runs (2026-09-22, 2026-09-23 — Bruno's screenshot: no toolbar; "Ü÷w" in the headline header; the Feeds and headline headers not following their panes' widths and split into extra sections; no From/Date headers wanted — one headline header, as on the Mac): the toolbar is laid out 0 px tall (created with CCS_NORESIZE, measured before sizing — use TB_GETBUTTONSIZE), and the headline header shows garbage (SetHeaderItem with HDI_TEXT and NULL text — resize with HDI_WIDTH only).
- [x] `gazette_win_window.c:930` unused variable `dc` (compiler warning).
- [x] Match Bruno's design (mockup artifact MN9UA7itM2vc7Ro7vzVigp, then OE5-for-Windows toolbar, 2026-09-23): flat toolbar in the Mac's four groups with captions **under** the icons, two lines max, etched separators; glass + search field at the right; one header band per pane (Feeds; view name + count); sunken white panes; standard tree with dotted lines; status bar with grip. Layout stays the Mac's three columns; one two-line headline list, no columns.
- [x] Toolbar responsiveness on resize — Bruno chose both (2026-09-23): the Mac's rule first (captions drop one at a time from the right, captions beside icons), then Outlook Express 5's (buttons hidden from the right behind a » chevron that drops down a menu of them). Chevron is a toolbar button + TrackPopupMenu so it works on every comctl32, not only 5.80. Rebuilt with TB_DELETEBUTTON/TB_ADDBUTTONS; New and Hide Sidebar never hide.
- [x] Outlook Express look, from Bruno's side-by-side (2026-09-23): toolbar in a rebar band (4.70+, plain bar on 4.0), fixed-width buttons with captions wrapping to two lines, captions all off then » chevron when narrow; Find button (the magnifier icon) + Edit ▸ Find… opening Windows' standard Find dialog instead of the search field; each list in a sunken pane with its header band inside the edge (HDS_BUTTONS); no horizontal scroll bar under the headlines, no scroll bar on the empty reader; status bar in two sections (count | network); window title "<view> - Gazette".
- [x] From Bruno's 86Box check (2026-09-24, "the UI is pretty spot on"): an etched edge round the toolbar strip like OE's (the window draws EDGE_ETCHED; a one-band rebar draws none), and the strip's height follows the captions — shorter when they drop to icons.
- [x] OE's 3 px gap between the toolbar strip and the panes (measured off Bruno's screenshots, 2026-09-24); every pane white — the headline list came up grey after that change (frame without WS_CLIPCHILDREN painting over it); fixed with WS_CLIPCHILDREN, explicit LVM_SETBKCOLOR/TEXTBKCOLOR and a repaint after layout. Awaiting Bruno's 86Box check.
- [x] Window UI approved by Bruno on 86Box (Windows 95), 2026-09-24, build 35933788876. XP still unchecked. Superseded note: confirm on 86Box and an XP machine with build 35898874844 (artifact Gazette-win32): toolbar visible, captions dropping then » chevron on narrowing, header bands clean, sunken panes. **Next after Bruno's OK:** the Win32 store (`src/store/` on Win32 files per docs/windows.md), then `gazette_feeds.c` / `core` on Windows so the sidebar and headlines fill.

### W3 – Full three-pane UI
- [ ] Sidebar (groups + feeds), headline list, article pane.
- [ ] Resizable panes / splitters with classic common controls.
- [ ] Read / starred / search / OPML import-export. OPML is the engine's (`gazette_opml.c`, already in the Windows build): Bruno wants to move his OS 9 feed list over with Export Feeds… / Import Feeds….
- [ ] Full-text extract path wired.

### W4 – Polish + ship
- [ ] Native look on 95–XP (no modern flat redesign).
- [x] Installer or zip distribution via CI. (`Gazette.zip` + 1.44 MB `Gazette.img`, artifact `Gazette-win32`.)
- [x] Icon (`.ico`) derived from the same artwork. (`Resources/win/Gazette.ico`, `tools/generate_win_assets.py`.)
- [ ] Smoke-tested on at least one 9x/Me and one 2000/XP environment (86Box or real).
- [ ] README section for Windows.

---

## OS X / macOS port (PowerPC, Intel, Apple Silicon)

Keep portable C core. Shells may differ by era.

### X0 – Architecture audit (do this first)
- [ ] Confirm every module that must stay pure C and free of Toolbox/Carbon headers.
- [ ] List exact OS X / macOS APIs that replace Open Transport, File Manager, and Appearance Manager controls.
- [ ] Produce `docs/osx-port.md` with ownership map and stage plan.
- [ ] Host tests still pass.

### X1 – First runnable shell
- [ ] Choose path for this stage: Carbon (older OS X) **or** Cocoa (modern). Document the choice.
- [ ] Open window, list feeds, fetch one feed over TLS.
- [ ] Real Mach-O binary (not Retro68).

### X2 – Full UI on modern macOS (Intel + Apple Silicon)
- [ ] Three-pane native UI (AppKit or SwiftUI if requested).
- [ ] Prefs, OPML, read/starred, search, full-text extract.
- [ ] System appearance / native look; no generic “web app” chrome.
- [ ] Universal or separate builds as needed.

### X3 – PowerPC / early OS X path (if still desired)
- [ ] Carbon or dual-target strategy documented and implemented, or explicitly deferred with reason.

### X4 – Ship
- [ ] Icons (`.icns`).
- [ ] CI artifacts / releases.
- [ ] README section for OS X / macOS.

---

## Cross-cutting

- [ ] `CLAUDE.md` Opus 5.5 operating rules kept accurate.
- [ ] `PROMPTS.md` prompts stay in sync with real workflow.
- [ ] Portable core never gains platform `#ifdef`s that break host tests.
- [ ] Prefer one platform “done” definition at a time so Opus 5.5 can finish a clear finish line.

---

## How to use this file with Opus 5.5

At the start of a long run, paste or attach this file and say:

> Keep TASKS.md updated. Tick items as you finish them and add anything new you find.  
> When a step doesn’t need my input, keep going. Stop and ask only when you can’t continue without me or before anything destructive.

At the end of a run, read the **Blocked on me / Changed / Found** summary first, then reconcile this checklist.
