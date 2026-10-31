# Gazette on Mac OS X and macOS

Plan and ownership map for the two Mac OS X builds. CLAUDE.md holds the rules; this file holds the plan. Written 2026-10-31, the start of the `osx-legacy` stage.

## The two builds

| | `osx-legacy` | `macos-universal` |
|---|---|---|
| Systems | Mac OS X 10.4 – 10.14 | macOS 10.13 – current |
| Architectures | ppc + i386 (Universal Binary) | x86_64 + arm64 (Universal 2) |
| Shell | the Mac OS 9 Carbon shell (`src/main.cpp`, `src/ui/`), compiled as Mach-O | AppKit, `src/cocoa/` (not started) |
| Event loop | `WaitNextEvent`, as on Mac OS 9 | `NSApplication` run loop + timer |
| Network | BSD sockets + Certainly/BearSSL (`GAZETTE_USE_EMBEDDED_TLS`) | `URLSession` (`GAZETTE_USE_NATIVE_TLS`) |
| Toolchain | iWordle's: gcc 14 cross (ppc) + clang/ld64 (i386) against the 10.4u SDK, on Ubuntu | Xcode on `macos-latest` |
| Workflow | `.github/workflows/osx-legacy.yml` | `.github/workflows/macos.yml` (later) |

10.13 and 10.14 are covered by both; that overlap is what closes Catalina's gap (no 32-bit).

## `osx-legacy`: what changes, file by file

The Carbon shell is CarbonLib code and Carbon on OS X runs it with Aqua controls, so this stage is mostly a re-link. What differs from the Mac OS 9 build:

| Area | Mac OS 9 | Mac OS X (`osx-legacy`) | Where |
|---|---|---|---|
| TCP | Open Transport (`transport_ot.c`, `gazette_net_ot.c`) | BSD sockets (`transport_posix.c`, `gazette_net_posix.c`) | Certainly, `src/net/` |
| Entropy | Toolbox sources (`entropy.c`) | `/dev/urandom` + timing (`entropy_posix.c`) | Certainly |
| Certainly compat | `CERTAINLY_OPEN_TRANSPORT` | `CERTAINLY_POSIX` (calloc, monotonic TickCount) | `certainly_compat.h`, PATCHES.md |
| Headers | Universal Interfaces 3.4, flat | 10.4u SDK framework `Headers` dirs on the include path, still flat | build script |
| Pascal strings | `-fpascal-strings` (Retro68) | rewritten at build time by `tools/pascal_strings.py`; sources unchanged | build script |
| Resources | resource fork | `Contents/Resources/Gazette.rsrc` (Retro68 Rez, data fork) | workflow |
| Icon | `ICN#`/`icl8`/`ics#`/`ics8` | `Gazette.icns` assembled from the same classic elements (proper large art is an X4 item) | workflow |
| Quit | File > Quit | application menu (Aqua layout), `kAEQuitApplication` | `src/main.cpp`, behind `TARGET_RT_MAC_MACHO` |
| Drawing outside updates | immediate | `QDFlushPortBuffer` in tracking loops | `src/ui/` |
| Byte order | big-endian | ppc big, i386 little: custom resource reads made explicit | `src/ui/`, `src/main.cpp` |
| Prefs and cache | System Folder:Preferences | `~/Library/Preferences` via `FindFolder(kUserDomain, ...)`, prefs file and `Gazette Cache` folder alike (same names and formats) | `src/store/gazette_store.c` |
| User agent | `(Macintosh; Mac OS 9)` | `(Macintosh; Mac OS X)` via `GAZETTE_MACOSX` | `src/portable/gazette_http.h` |

Engine (`src/app`, `src/core`, `src/feeds`, `src/extract`, `src/prefs`, `src/portable`): unchanged. It already runs little-endian on Windows.

## Proof

- `net-posix.yml`: the POSIX transport, Certainly and the fetch run as a console test on `ubuntu-latest` and `macos-latest` against live TLS 1.3 and TLS 1.2 servers, the same URLs as the Windows test under Wine.
- `osx-legacy.yml`: `lipo -info` shows ppc and i386; artifact `Gazette-mac-os-x` (`.zip` + `.dmg`).
- Mac OS 9 (`build.yml`) and Windows (`windows.yml`) stay green; host tests gate every job.
- On real systems (Bruno, in UTM): 10.4 PowerPC and one Intel 10.6–10.10 show live headlines.

## Process

Plan (this file, lead) → implementation by `porter` subagents on separate branches (`osx-legacy-net` for the network, `osx-legacy` for the build) → `reviewer` (Opus) on each branch → lead reviews the merged diff → Bruno tests in UTM.

## Later: `macos-universal`

Waits for the native-TLS seam (TASKS.md N1/N2), which `win-modern` builds first with WinHTTP. Then: an AppKit shell in `src/cocoa/` reusing `src/app/` the way `src/win/` does, and a `URLSession` `GazetteFetch` backend.
