# AGENT.md — Gazette

Native RSS / Atom reader.  
**Name:** Gazette  
**Primary target (complete):** Mac OS 9 (Carbon, PowerPC) — a beautiful, fast, native replacement for (and spiritual successor to) Alex Robb’s Newsstand 1.1, with full Platinum look-and-feel, modern HTTPS feed fetching, Google News support, custom RSS/Atom, and sensible offline caching — all running entirely on Mac OS 9 with no helper Mac required.

**Ports in progress:**
- Windows NT / 9x / Me / 2000 / XP (Win32) — the *legacy Windows* build

**Expansion vectors (2026-10-31; development focus is redirected here):**
- **Vector 1 — backwards:** Classic 68K Mac OS — System 7.5.3 to Mac OS 8.1 on 68020/030/040, with Open Transport 1.3.1 and CFM-68K installed (built the way Gateway 0.4.0's 68K build is).
- **Vector 2 — forwards:**
  - Mac OS X / macOS: every release from Tiger 10.4 to current macOS, PowerPC, Intel and Apple Silicon, 10.6–10.10 included.
  - Modern Windows: Windows 10 and 11 (x64 / ARM64) with modern DPI awareness and native Unicode paths. The legacy Win32 build stays as it is.
- **Network layer:** host-aware. Use the system's TLS where it does TLS 1.2+, and embed TLS only where it doesn't. See *Network layer strategy* below.

This is a real client application, not a proxy. It reuses Gateway’s networking/TLS stack wherever the target needs embedded TLS, and the feed-handling intelligence from NewsProxy everywhere.

### Platform matrix

| Build | Targets | Toolchain | Shell | Network / TLS | Status |
|---|---|---|---|---|---|
| `mac-ppc-carbon` | Mac OS 9 (CarbonLib 1.1+), PowerPC | Retro68 `retrocarbon` | Carbon, Platinum (`src/main.cpp`, `src/ui/`) | Open Transport + Certainly/BearSSL (embedded) | complete, shipping |
| `win32-legacy` | Windows 95 / NT 4 to XP, i686 | MinGW-w64 i686 cross | Win32, ANSI, common controls 4.0 (`src/win/`) | Winsock 1.1 + Certainly/BearSSL (embedded) | shipping |
| `mac-68k` | System 7.5.3 – Mac OS 8.1, 68020+, OT 1.3.1 | Retro68 `m68k-apple-macos` (Gateway's 68K recipe) | classic Toolbox (no Carbon on 68K) | Open Transport (static 68K glue) + Certainly/BearSSL; optional HTTP proxy | planned (Vector 1) |
| `osx-legacy` | Mac OS X 10.4 – 10.14, ppc + i386 Universal Binary | iWordle's recipe: `powerpc-apple-darwin8-gcc` container (PowerPC half) + clang/ld64 against phracker's 10.4u SDK (i386 half), `lipo` | Carbon, Mach-O: the Mac OS 9 shell (`src/main.cpp`, `src/ui/`), Aqua on OS X | BSD sockets + Certainly/BearSSL (embedded), because Secure Transport has no TLS 1.2 before 10.8 | planned (Vector 2) |
| `macos-universal` | macOS 10.13+, x86_64 + arm64 (Universal 2; arm64 is 11+) | Xcode on a `macos-latest` runner | AppKit (`src/cocoa/`) | `URLSession` (native) | planned (Vector 2) |
| `win-modern` | Windows 10 / 11, x64 + ARM64 | llvm-mingw cross on Ubuntu | Win32, Unicode (`W`), Per-Monitor V2 DPI | WinHTTP (native) | planned (Vector 2) |

Coverage on the Mac side has no gaps. `osx-legacy` (i386) runs on every Intel Mac from 10.4 to 10.14, which covers 10.6–10.10. Catalina dropped 32-bit, and `macos-universal` takes over from 10.13. All builds run on GitHub Actions; no build is done by hand.

---

## Opus 5.5 / Claude Code operating rules

These rules exist so long runs stay productive and token-efficient. Follow them on every session.

### Keep going
When a step doesn’t need human input, keep going. Put status notes in the same message as your next action.  
Stop and ask **only** when:
- you cannot continue without a design decision the human has not already made, or
- before anything destructive (deleting data, force-pushing, changing files outside this repository).

### Task list survives context
Keep a live checklist in `TASKS.md`. Tick items when done; add anything new you discover. Prefer reading `TASKS.md` over scrollback when the run is long.

### No “think carefully” / “think step by step”
Opus 5.5 already reasons before every reply. Do not add those phrases to prompts or this file. For a quick answer to a simple question, the human may say “Answer directly.”

### Finish line first
Every substantial task arrives with an explicit definition of “done”. Treat that definition as the stop condition. If the human did not supply one, ask for it before starting heavy work.

### End-of-run summary format
End every long run with three headings:

```
## Blocked on me
## Changed
## Found
```

### Subagents for large work
For audits, migrations, or reviews that span many modules, split the work across subagents and verify each subagent’s evidence before accepting it. Finish with one table or checklist.

### Design direction (UI ports)
When building or porting a UI, name the styles to *avoid*. Default styles without direction are generic. Prefer the native look of the target platform (Platinum on OS 9, classic Win32 common controls on Windows 95–XP, native AppKit / system appearance on modern macOS).

### Review before merge
Before a human reviews a branch, be prepared for a prompt that asks only for merge-blocking problems (file + line, why wrong, how to show it fails).

### Permission prompts
Keep permission prompts on for destructive commands. The keep-going rule never overrides that.

---

## Icon

Application icon should evoke a classic newspaper / gazette / newsstand:

- Prefer an isometric or clean 3/4 view of a folded newspaper, a small newsstand kiosk, or a stack of papers with a “Gazette” masthead feel.
- Classic Mac icon style (suitable for 32×32, 16×16 and larger).
- Platinum-era friendly colours; avoid flat modern design language.
- Transparent or solid background as required for resource forks.

Creator code: `Gzt9`, set via `add_application(... CREATOR "Gzt9")` in `CMakeLists.txt`. Keep signature style consistent with Post, Gateway and other brunocastello classic projects.

Icons live in the resource fork (`ICN#` / `icl8` / `ics#` / `ics8` plus `BNDL` and `FREF`), rezzed in through `Resources/Gazette_icon.r` — not as a `.icns` file, which has no meaning for a CFM application on Mac OS 9. That file is generated by `tools/generate_icon.py`; edit the drawing code, not the Rez output.

Windows and OS X ports need their own icon assets (`.ico`, `.icns`) derived from the same artwork; generate them from the same source drawing when those ports start.

---

## Non-negotiable constraints (Mac OS 9 — read once, obey forever)

1. Target: Mac OS 9 with CarbonLib 1.1 or later, PowerPC only. Build with Retro68's `retrocarbon` toolchain against Apple's Universal Interfaces 3.4 (vendored in `third_party/AUI/`). Before using any Toolbox call, check its `Availability:` block in the header — if its CarbonLib line is later than 1.1, it cannot be used. The floor was 1.0 until 2026-09-10, when it was raised to 1.1 so the window could be built from real controls: CreateListBoxControl, CreateWindowHeaderControl, CreateStaticTextControl and CreatePlacardControl are all "in CarbonLib 1.1 and later", and drawing those widgets by hand instead was the debt Phase 5 exists to pay off. 1.1 is a free Apple update and every Mac OS 9 release ships something later, so the floor costs nothing in practice. Anything needing 1.2 or beyond still cannot be used. Many familiar classic calls (`InitGraf`, `InitWindows`, `InitMenus`, `MaxApplZone`, `CloseWindow`, `SystemClick`) are `CALL_NOT_IN_CARBON` and simply do not exist here.
2. UI must feel like a native Apple Platinum application from the Newsstand / OE5 era — clean sidebar, article list, readable text pane, proper toolbar and menus. Do not invent a modern or OS X-like interface.
3. Memory: SIZE resource 8 MB preferred / 4 MB minimum (same discipline as Gateway/Post), declared in `Resources/Gazette.r`. CarbonLib is hungrier on OS 9 than the classic Toolbox was, so the headroom is deliberate. Stream network data. Prefer `NewPtrClear`. Cooperative multitasking only — never block the event loop.
4. Networking & TLS: reuse / extract from Gateway (`gw_net` + Certainly/BearSSL). All outbound HTTPS (Google News, RSS, Atom, article pages) goes through this stack. Do not reimplement TLS.
5. Interfaces: one set, Apple’s Universal Interfaces. Carbon needs `Carbon.h` and `CarbonLib`, which the Multiversal Interfaces do not provide, so the dual Multiversal/Universal separation Gateway and Post use does not apply here. Two rules survive from it:
   - Include only the headers you actually use (`<MacWindows.h>`, `<Menus.h>`, …), never the `<Carbon.h>` umbrella — it drags in the whole of ApplicationServices and CoreServices for nothing. Retro68 links the headers *flat*, so it is `<MacWindows.h>`, not `<Carbon/MacWindows.h>`.
   - Portable feed parsing, Google News helpers and text processing keep zero system headers and stay host-testable on Linux.
   The `core/` seam stays thin and plain-typed; `MacTypes.h` is the only system header it may include.
6. No C++ static constructors / non-trivial file-scope objects.
7. All network I/O is non-blocking and polled from the single `WaitNextEvent` loop. Use the classic `EventRecord` loop, not the Carbon Event Manager (`RunApplicationEventLoop`): CarbonLib supports it fully on Mac OS 9, and it is the only shape that leaves an idle point to poll network I/O from. Do not set `kWindowStandardHandlerAttribute` on windows — it installs a Carbon Event handler that competes with the loop.
8. Text output must be OS-9 friendly: ASCII transliteration of accents/diacritics/typographic punctuation and flattened whitespace (exactly as NewsProxy does) so the original Newsstand-style text rendering stays clean.

---

## Non-negotiable constraints (ports)

### Shared / portable core (all platforms)
- `src/portable/`, `src/feeds/`, `src/extract/`, and the pure parts of `src/prefs/` stay free of platform headers and remain host-testable on Linux.
- Prefer the same prefs grammar, cache layout, and OPML behaviour so a user’s feed list and read state can move between platforms with minimal friction.
- Do not reimplement TLS. Embedded builds reuse Certainly/BearSSL. Native builds use the system stack, as decided on 2026-10-31 (see *Network layer strategy*).
- The portable core never sees a platform or TLS flag. Platform `#if`s live in shells and in `src/net/` backends only.

### Classic 68K Mac OS (System 7 / Mac OS 8) — Vector 1
**The pattern is Gateway's 68K build (Gateway 0.4.0, 2026-10, System 7.5.3 to Mac OS 8.1).** Copy it rather than re-deriving it: `Gateway/.github/workflows/build-mac68k.yml`, the `GW_M68K` branch of `Gateway/CMakeLists.txt`, and rule 8 in Gateway's CLAUDE.md.

- **Baseline (decided 2026-10-31):** System 7.5.3 or later, through Mac OS 8.1 (the last 68K release), with **Open Transport 1.3.1** and the Code Fragment Manager (CFM-68K) installed. Gateway found that OT 1.1, which a System 7.5 Update puts back, lacks what endpoints need, so 1.3 is the floor there too. MacTCP is out.
- **No Carbon.** CarbonLib is PowerPC-only and needs Mac OS 8.1 or later, so the 68K build is a classic Toolbox shell (`src/classic/`). It must not be an `#ifdef` maze through `src/main.cpp`. On 68K the classic init calls (`InitGraf`, `InitWindows`, `MaxApplZone`, …) are required, not forbidden. PowerPC Macs on 7.5.3–8.0 have no CarbonLib either. They run the 68K build under the Mac's built-in 68K emulator, so no separate classic PowerPC build is planned.
- **Code model.** This is Gateway's: a classic (non-CFM) 68K application from Retro68's `m68k-apple-macos` toolchain, a single code segment, `-ffunction-sections` + `--gc-sections`. Open Transport and Internet Config come in as **static 68K glue** that `interfaces-and-libraries.sh` converts into `universal/lib68k` (`libOpenTransportApp.a`, `libOpenTransport.a`, `libOpenTptInet.a`, `libICGlueFar.a`, then `libInterface.a` last for the trap-less calls). Nothing is a CFM import. Use the far-model glue wherever a near branch can't reach across a segment this size. CFM-68K being installed is part of the user's baseline, but the build does not depend on it.
- **CPU.** `-mcpu=68020 -msoft-float`, as Gateway uses. Open Transport doesn't run on a 68000 and Mac OS 8 needs a 68040, so 68020 code costs no reach. It also buys the 32×32→64 multiply that BearSSL's bignum code lives on. Soft float is needed because a 68LC040 has no FPU. There is no 68000 build.
- **Memory.** Gateway ships 8 MB preferred / 4 MB minimum on 68K with several TLS listeners. Gazette makes one or two outbound connections at a time, so its 68K minimum should come in under that: it should open on an 8 MB machine running 7.5.3 + OT 1.3.1. Measure it, then record it in the 68K `SIZE` resource. Stream everything. Cap the cache index, the article buffer, photo decode (smaller or off on 68K) and the number of concurrent fetches. Grow nothing on a timer.
- **Networking.** The same Certainly Open Transport client the PowerPC build uses. The fetch and stream layers take buffer sizes and parallelism from the build (`mac-68k` values) rather than hard-coding the PowerPC ones.
- **TLS on 68K.** Embedded Certainly/BearSSL is the default. Gateway proves TLS 1.3 on 68K hardware. The handshake is slow on a 68030, so status feedback during the connection matters more here. The **HTTP proxy mode** preference (plain HTTP to a user-chosen proxy, e.g. Gateway on the LAN) is available for slow or tight machines. On the PowerPC build, "no helper Mac required" still holds.
- **Look: native on each system.** On Mac OS 8.0/8.1, use Appearance Manager 1.0 controls (Platinum). Appearance 1.0 lacks the 1.1 variants, e.g. `kControlCheckBoxAutoToggleProc`, so toggle those by hand, as Gateway does. On System 7, which has no Appearance Manager, use the System 7 standard controls (the List Manager, standard `CDEF`s, scroll bars), which is what a native System 7 app looks like. Gate every Appearance call behind a `Gestalt(gestaltAppearanceAttr)` check, the way Gateway gates its Settings window. Draw nothing by hand on either system.
- **Packaging.** These are Gateway's 68K artifacts: a StuffIt `.sit` for real hardware, and a **partitioned** hybrid CD `.iso` (`genisoimage -hfs -part --double`) for QEMU / UTM's Quadra 800, attached as a CD. A bare `.dsk` makes the Quadra offer to initialise it.

### Windows — legacy build (NT / 9x / Me / 2000 / XP)
- Native Win32. Target the oldest platform you claim support for (imports and common controls must work on Windows 95 OSR2 / NT 4 if that is in scope; currently aim for 95 through XP as Gateway does).
- Networking: Winsock 1.1 + Certainly/BearSSL. XP's SChannel has no TLS 1.2, so this build keeps embedded TLS for good.
- UI: classic Win32 common controls and dialogs. No modern flat redesign; feel native on the target OS.
- Build: MinGW-w64 i686 cross-compile in CI (`.github/workflows/windows.yml`, `cmake/GazetteWindows.cmake`).
- Event loop must poll the non-blocking network layer the same way the Mac OS 9 loop does.
- Details: `docs/windows.md`.

### Windows — modern build (10 / 11, x64 / ARM64) — Vector 2
- **The same `src/win/` shell**, compiled a second time. It is not a fork. Differences are build-selected and kept small:
  - Unicode: `UNICODE`/`_UNICODE` and explicit `W` calls for paths, window text and file dialogs. Conversion happens at the shell edge (UTF-8 inside, UTF-16 at the Win32 call). The ASCII-transliterated engine text is unchanged. The legacy build stays ANSI.
  - DPI: Per-Monitor V2 declared in the application manifest, with `SetProcessDpiAwarenessContext` looked up at run time as a fallback (it is Windows 10 1703+). Layout metrics scale with `GetDpiForWindow`, and `WM_DPICHANGED` is handled.
  - Storage: `%APPDATA%\Gazette` (the legacy beside-the-exe rule exists for 95–XP), in the same file formats.
  - Look: common controls 6 via manifest. Native Windows 10/11 look, no custom chrome.
- **Networking:** WinHTTP async (native TLS). Its callbacks arrive on a system thread. They only queue results and set a flag, and the engine still advances only from the message loop's idle point (the constraint 7 model).
- No embedded crypto in this binary: Certainly/BearSSL are not linked.
- **Build:** x64 and ARM64 with llvm-mingw, cross-compiled on Ubuntu like the legacy job. It is one toolchain for both architectures and keeps the MinGW CMake shape we already have. It is a separate job from the i686 legacy one.

### OS X / macOS (PowerPC, Intel, Apple Silicon) — Vector 2
- Keep the portable C core. Coverage is every release from 10.4 Tiger to current macOS, 10.6–10.10 included (decided 2026-10-31). SwiftUI only if the human asks for it.
- **Two builds, two shells, the pattern iWordle already ships** (`iWordle/.github/workflows/build-macosx.yml` and `build-macos.yml`):
  - `osx-legacy`, 10.4–10.14, ppc + i386: **the existing Carbon shell** (`src/main.cpp`, `src/ui/`) compiled as Mach-O. Gazette's Mac OS 9 shell is CarbonLib code, and Carbon on OS X draws the same controls in Aqua, so this is native there and is mostly a re-link. Differences stay behind a build flag: Mach-O frameworks instead of CarbonLib, BSD sockets instead of Open Transport, preferences and cache under `~/Library` instead of the System Folder. It is 32-bit only, which Carbon UI needs anyway. It uses embedded Certainly/BearSSL. Shell code must compile under both gcc 14 (PowerPC) and clang (i386).
  - `macos-universal`, 10.13 to current, x86_64 + arm64: an AppKit shell (`src/cocoa/`) using `URLSession`. The deployment target is 10.13, not iWordle's 11.0, so Catalina (no 32-bit) is covered. Newer API goes behind `@available`. CI uses the newest installed Xcode, so it picks up the macOS 26 look by itself.
- `osx-legacy` keeps the `WaitNextEvent` loop it already has. The modern shell's event loop is `NSApplication`'s run loop. Network progress is driven from a run-loop timer or a main-queue `URLSession` delegate, so the engine stays single-threaded and polled.
- UI: native system appearance. Do not invent a custom “modern web” look.
- Produce real Mach-O binaries in a proper `.app` bundle (`Info.plist`, `.icns`). Retro68 is classic Mac OS only.
- Architecture note and ownership map live in `docs/osx-port.md` once Stage 1 of the OS X port is done.

---

## Network layer strategy (conditional TLS / HTTP)

Not every target needs embedded TLS. Decided 2026-10-31:

- **System-native TLS:** modern Windows (WinHTTP) and macOS 10.13+ (`URLSession`). Do not embed crypto there, and keep those binaries lean.
- **Embedded TLS (Certainly + BearSSL, Gateway's stack):** classic Mac OS 7/8/9, OS X 10.4–10.14 (`osx-legacy`), and legacy Windows 95–XP. Their native stacks have no TLS 1.2+ with modern ciphers. mbedTLS is not added. BearSSL is already vendored and patched.
- **HTTP proxy mode:** a run-time preference (proxy host:port, plain HTTP to the proxy) for low-RAM 68K machines. It may also be compiled into a 68K variant that has no TLS at all.

**The seam is `src/net/gazette_fetch.h`, not `gazette_net.h`.** WinHTTP and `URLSession` are HTTP-level APIs and do not hand out a TCP/TLS byte stream. So a native backend implements the whole `GazetteFetch` interface (start / pump / state / status / content type / final URL / sink callback), with redirects and de-chunking done by the system. Embedded builds keep today's `gazette_fetch.c` over `gazette_net.c` streams unchanged. Everything above the fetch (refresh, full text, photos, Google News decode) must not be able to tell which backend it runs on.

**Preprocessor flags.** CMake sets exactly one per build, and `src/net/` fails the build with `#error` if none or two are set:

| Flag | Meaning | Builds |
|---|---|---|
| `GAZETTE_USE_EMBEDDED_TLS` | `gazette_fetch.c` + `gazette_net.c` + Certainly/BearSSL | `mac-ppc-carbon`, `win32-legacy`, `mac-68k` (default), `osx-legacy` |
| `GAZETTE_USE_NATIVE_TLS` | a native `GazetteFetch` backend; Certainly/BearSSL not linked | `win-modern` (WinHTTP), `macos-universal` (`URLSession`) |
| `GAZETTE_USE_NO_TLS` | plain HTTP only; https URLs go through the proxy or fail with a clear message | reserved: built only if 68K measurements ever demand a TLS-less variant |

`GAZETTE_HTTP_PROXY` (on by default for `mac-68k`, available everywhere) compiles in the proxy preference. Platform selection (`GAZETTE_WIN32`, the existing user-agent flag, and its siblings) stays separate from the TLS choice.

---

## Primary sources to reuse (copy / adapt, do not rewrite)

**From Gateway** (https://github.com/brunocastello/Gateway):
- `third_party/certainly/` + BearSSL (already patched)
- `src/net/` — Open Transport on Mac OS 9, Winsock on Windows
- Portable utilities: `gw_log`, `gw_util`, `gw_prefs`, `gw_http`, `gw_b64`, etc. as needed
- Build system, host-test pattern, `docs/inventory.md` memory & cooperative rules
- Win32 shell and installer patterns for the Windows port

**From NewsProxy** (https://github.com/brunocastello/NewsProxy):
- Google News country / section / topic ID maps
- Feed auto-discovery logic
- Google News redirect / batchexecute URL decoding technique
- RSS 2.0 + Atom parsing approach
- Article body strategy (prefer feed summary; fall back to simple HTML stripping when full page is fetched)
- ASCII transliteration and whitespace flattening for Mac OS 9 text compatibility
- Endpoint/format knowledge only if a compatibility mode with the original Newsstand binary is ever desired (not required for Gazette itself)

Gazette is a standalone app. It does **not** need to speak the old Newsstand XML protocol unless explicitly added later as an optional compatibility server.

---

## Feature targets

### Core (Newsstand parity + necessary modernisation) — Mac OS 9: done
- User-added custom RSS 2.0 and Atom feeds
- Feed auto-discovery when a site homepage URL is given
- Sidebar organised by the user's own groups (one level deep)
- Article list with title, source, date
- Clean article reader pane (full-text extract where possible)
- Manual and automatic refresh
- Local caching so the app is usable offline after first fetch
- Progress / status feedback that does not freeze the UI
- Mark as read / unread, starring
- Search within downloaded articles
- OPML import/export
- Keyboard navigation that feels native

### Google News
A Google News feed is an ordinary RSS feed. Curated browser was deliberately dropped.  
`feeds/gazette_gnews_topics.c` and the country map are **kept deliberately** even though nothing in the application currently calls them — raw material for a future search helper. Do not delete them as dead code.

Full text cannot work on a Google News item that lands on a pure JS redirector; the feed summary stands in. Decoding those URLs is the NewsProxy technique.

### Explicitly out of scope for v1 (all platforms)
- Full modern HTML/CSS rendering engine
- JavaScript execution
- Podcast / media enclosure playback
- Synchronisation with cloud services
- Complex database; prefer simple, recoverable file-based storage

---

## Architecture sketch

```
src/
  main.cpp              # Mac OS 9 Carbon shell, WaitNextEvent, menus, windows
  ui/                   # Platinum sidebar, lists, reader pane, dialogs (OS 9)
  core/                 # thin C seam between UI and engine
  net/                  # GazetteFetch seam; embedded backend (Open Transport / Winsock
                        #   + Certainly); native backends (proposed): gazette_fetch_winhttp.c,
                        #   gazette_fetch_urlsession.m; 68K: OT with small buffers
  app/                  # shell logic shared by the Mac and Windows shells
  feeds/                # feed model, Google News, RSS/Atom parser, refresh logic
  extract/              # HTML stripping, transliteration, summary vs full-text
  store/                # local cache of feeds and articles
  prefs/                # user settings, feed list
  portable/             # pure C, host-testable parsing and text helpers
  win/                  # Windows shell (legacy ANSI build and modern Unicode/DPI build)
  classic/              # (planned, Vector 1) classic Toolbox shell for 68K System 7.5.3 – 8.1
  cocoa/                # (planned, Vector 2) AppKit shell for macOS 10.13+ (10.4–10.14 reuse the Carbon shell)
third_party/
  AUI/                  # Apple Universal Interfaces 3.4 (Carbon headers + CarbonLib)
  certainly/            # vendored from Gateway, plus PATCHES.md for Carbon
tools/
  generate_icon.py      # draws the icon, emits Resources/Gazette_icon.r
Resources/
  Gazette.r             # SIZE, About alert, vers
  Gazette_icon.r        # generated icon family + BNDL/FREF
docs/
  design.md
  newsstand-parity.md
  osx-port.md           # (to be written during OS X Stage 1)
  release-notes.md
TASKS.md                # live checklist for Opus 5.5 long runs
PROMPTS.md              # ready-to-paste prompts for Claude Code
```

Single cooperative (or message-loop) event model. Network fetches and parsing progress via poll functions. UI stays responsive.

### Build system and CI strategy (expansion)

One CMake project, one toolchain file per build, one CI job per build. Each job runs after the `host-tests` gate.

- **Retro68:** keep the existing `retrocarbon` PowerPC job in `build.yml`, and add `build-mac68k.yml` modelled on Gateway's. It uses the same `ghcr.io/autc04/retro68` container and runs `interfaces-and-libraries.sh` with `true false false` (68K only) over Apple's Universal Interfaces, so ConvertObj fills `universal/lib68k`. It fails if any of the OT / IC / Interface glue libraries is missing, then runs CMake with `m68k-apple-macos/cmake/retro68.toolchain.cmake`. CMake switches on `CMAKE_SYSTEM_NAME MATCHES Retro68` (as Gateway's `GW_M68K` does) to pick the classic shell, the 68K libraries, `-mcpu=68020 -msoft-float`, the 68K `SIZE` resource and the 68K buffer sizes. The Carbon sources are never compiled for 68K. Then come the `.sit` and partitioned `.iso` packaging steps and the release upload, as in Gateway's workflow.
- **Windows (`windows.yml`):** keep the i686 MinGW legacy job, with its 95-import check, unchanged. Add a `win-modern` matrix (x64, ARM64) on llvm-mingw with its own toolchain files and its own import check: no Certainly/BearSSL symbols, WinHTTP present.
- **macOS (new `macos.yml`):**
  - `macos-universal`: `macos-latest` with Xcode, `-arch arm64 -arch x86_64`, deployment target 10.13. The output is an ad-hoc-signed `.app` in a zip.
  - `osx-legacy`: copy iWordle's `build-macosx.yml`, all on `ubuntu-latest`:
    1. Retro68's Rez compiles the Carbon resources into a data-fork `.rsrc`.
    2. The PowerPC half: `ghcr.io/variantxyz/gcc-powerpc-apple-darwin8:build-gcc-14.2-MacOSXSDK10.4u`, `-arch ppc -isysroot /usr/local/MacOSX10.4u.sdk -mmacosx-version-min=10.4`.
    3. The i386 half: that image's SDK has its i386 slices stripped, so download a full `MacOSX10.4u.sdk` from `phracker/MacOSX-SDKs` (release 11.3, pinned by SHA-256). Compile with the image's `clang -target i386-apple-darwin8` and link with its ld64 directly.
    4. `lipo` the halves together, assemble the `.app`, and make a `.zip`, plus a `.dmg` via libdmg-hfsplus.
    The SDK is never committed: it is fetched in CI only.
- Release: each job uploads its own artifact, and the existing release step collects them.

---

## Implementation status

**Phase 0 – Skeleton** ✅  
**Phase 1 – Networking foundation** ✅  
**Phase 2 – Feed engine** ✅  
**Phase 3 – Full Platinum UI + caching** ✅  
**Phase 4 – Polish and extras** ✅  
**Phase 5 – Native Toolbox controls** ✅  

See `TASKS.md` for remaining Mac OS 9 polish and the full port roadmap.

Never begin heavy visual polish or extra features before a platform’s core produces real live headlines on the target (emulator or real hardware).

---

## Coding standards

- C99 for engine, C++17 only for Toolbox / Win32 / Cocoa shell and UI helpers. Objective-C (or Objective-C++) only in the AppKit shell and the `URLSession` backend. GNU extensions stay on where the platform toolchain needs them.
- The Carbon shell must also compile as Mach-O for `osx-legacy`, under gcc 14 (PowerPC) and clang (i386).
- Explicit memory ownership (`NewPtr` / `DisposePtr` on OS 9; matching discipline on other platforms).
- No exceptions.
- Log freely (adapt Gateway log facilities).
- Small, testable pure functions in `portable/`.
- Comments explain platform-specific decisions.
- Keep files focused.

---

## Testing

- `tests/host/` for parsers, transliteration, Google News URL helpers, etc.  
  `make -C tests/host`. Compiles `src/portable/` and pure prefs with a plain host `cc`. CI runs it as a gate before platform builds.
- Live testing against real Google News and public RSS feeds on emulator + real hardware for each target platform.
- Verify text rendering stays clean on OS 9 (no high Unicode, no smart quotes, etc.).
- Windows and OS X ports must keep the same host-test gate. So must the 68K build.
- Each `GazetteFetch` backend gets the same live network test the Windows build already runs (`tests/win/gazette_nettest.c`, under Wine): TLS 1.3 and TLS 1.2 servers, a redirect, a chunked body, and a POST.
- 68K: test in UTM's QEMU Quadra 800 (as Gateway was) on System 7.5.3 + OT 1.3.1 and on Mac OS 8.1, at a realistic RAM size, before claiming a memory budget.

---

## References the model must use

- Gateway: networking, TLS, cooperative / Win32 patterns, memory rules
- NewsProxy: Google News maps, feed logic, transliteration, article strategy
- Alex Robb’s Newsstand 1.1 (behaviour, layout, feature set) — the north-star UX for the OS 9 app
- Classic Mac Toolbox documentation
- Win32 common controls and Winsock documentation (Windows port)
- Cocoa / AppKit documentation (modern macOS port)
- RSS 2.0 and Atom specifications (keep parsing pragmatic, not pedantic)
- `PROMPTS.md` in this repository for the recommended Claude Code prompts

---

## Final reminder

When in doubt:
- Make the OS 9 app feel like a first-class Platinum app that could have shipped in 2001.
- Make the Windows and OS X ports feel native to their platforms, not ports of a Mac UI.
- Prefer Newsstand’s clarity and simplicity over feature bloat.
- Reuse Gateway’s networking/TLS where a target needs embedded TLS, the system’s TLS where it has a modern one, and NewsProxy’s feed intelligence everywhere.
- Keep the portable core pure and host-testable.
- Advance one phase / one port stage at a time.
- Use the Opus 5.5 operating rules at the top of this file so long runs stay efficient.

The name of the application is **Gazette**.
