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
  Both are built. `gazette_feeds.c` decodes (GET `rss/articles/<token>` → POST `batchexecute` → story URL, or follows a plain redirect). Any failure goes through `FailFullText`, so the pane shows the feed summary and the status line reads “Summary only - Google News did not say where the story is.”
- [ ] Confirm the Google News decode on the Mac (built 2026-09-19, never seen working on real hardware or SheepShaver).
- [x] Confirm host tests (`tests/host`) still pass on every change. Green on CI, run 35876474049 (commit 73163dd).
- [x] Confirm CI produces clean `.sit` and `.dsk`. Same run: `Gazette.sit` 451 KB, `Gazette.dsk` 1.6 MB, no compiler warnings from Gazette's own code (only Node deprecation notices from the upload action).
- [x] README + `docs/release-notes.md` accurately describe what ships.
  Checked against the code: three pictures (`kGazetteMaxPhotos`), ⌘; Preferences, Hide/Show Photos, Gazette Cache in Preferences, Top Stories starter feed, CarbonLib 1.1 + QuickTime, 8/4 MB. `main` is `v0.1.0` plus the README screenshot, so the 0.1.0 notes still describe what ships.
- [x] No CarbonLib > 1.1 APIs introduced.
  Audited 265 Toolbox calls in the Mac sources and Certainly against AUI's `Availability:` blocks. Only five read “not available”: four OT macros that expand to `…InContext` (CarbonLib 1.0), and `NavServicesAvailable`/`LMGetTicks` in `!TARGET_API_MAC_CARBON` branches.
- [x] SIZE 8 MB preferred / 4 MB minimum; one `WaitNextEvent` loop; no `Delay`/sync OT; the only `for (;;)` loops are bounded text/layout work.
- [x] About window's modal loop now runs the same `PumpNetwork()` as the main loop. Before, photos and the auto-refresh clock stalled while About was open.
- [ ] Memory and cooperative rules still hold under real-hardware / SheepShaver stress (many feeds, full-text, photos on). Needs Bruno on the Mac.
- [-] CLAUDE.md dropped the detailed Phase 0–5 record and the group-merge note in the rename; left as is on Bruno's instruction (not to be edited for now). The full text is in `git show de48c05:AGENT.md`.

---

## Windows port (NT / 9x / Me / 2000 / XP)

Reuse Gateway’s Win32 networking and build patterns. Keep portable core pure.

### W0 – Foundation
- [ ] Audit portable core for any accidental Mac-only headers or assumptions.
- [ ] Document Windows constraints and event/network polling model (short note in `docs/` or CLAUDE.md).
- [ ] CI skeleton: MinGW-w64 job that builds a stub `Gazette.exe` (mirror Gateway).

### W1 – Network + store adapter
- [ ] Winsock adapter for the same non-blocking fetch API used on OS 9.
- [ ] File I/O / cache path for Windows (prefs + cache folders beside the exe or under AppData — pick one, document it).
- [ ] Host tests still pass; no Mac headers in portable code.

### W2 – Minimal shell
- [ ] Win32 window + message loop that polls the network layer.
- [ ] Show feed list / titles from cache or a live fetch.
- [ ] Quit cleanly; basic menus.

### W3 – Full three-pane UI
- [ ] Sidebar (groups + feeds), headline list, article pane.
- [ ] Resizable panes / splitters with classic common controls.
- [ ] Read / starred / search / OPML import-export.
- [ ] Full-text extract path wired.

### W4 – Polish + ship
- [ ] Native look on 95–XP (no modern flat redesign).
- [ ] Installer or zip distribution via CI.
- [ ] Icon (`.ico`) derived from the same artwork.
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
