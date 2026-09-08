# Gazette — Carbon RSS / Atom Reader for Mac OS 9

A native, Carbon-based RSS and Atom feed reader for **Mac OS 9.x** on **PowerPC**.

**Gazette** is a spiritual successor to Alex Robb's Newsstand 1.1 — a beautiful, fast, native replacement with full Platinum look-and-feel, modern HTTPS feed fetching, Google News support, and sensible offline caching.

## Features (planned)

- **Google News** — same countries and curated topics/sections as Newsstand 1.1
- **Custom feeds** — user-added RSS 2.0 and Atom feeds with auto-discovery
- **Platinum UI** — sidebar, article list, reader pane (Newsstand-inspired layout)
- **Offline caching** — feed lists and article bodies cached locally
- **Non-blocking networking** — all HTTPS via Gateway's Certainly/TLS stack

## What it actually targets

Gazette is cross-compiled with [Retro68](https://github.com/autc04/Retro68) into a
**PowerPC CFM (PEF) application with a real resource fork**, linked against CarbonLib.

- **Runs on:** Mac OS 9.x, PowerPC, with CarbonLib 1.0 or later installed
- **Does not run on:** any Intel or Apple Silicon macOS — this is a CFM binary, not Mach-O
- **68K:** not supported, and not planned (Carbon is PowerPC-only)

Every Toolbox call in the source is checked against the `Availability:` block in
Apple's Universal Interfaces headers; anything not marked *"CarbonLib: in CarbonLib
1.0 and later"* is off limits. See `AGENT.md` for the full constraint list.

## Building

There is no macOS-native or Linux-native build. The toolchain is Retro68, and the
supported way to run it is the official Docker image — which is exactly what CI does.

### Requirements

- **Docker**
- **Apple Universal Interfaces 3.4**, already vendored at `third_party/AUI/`

### Build

```bash
docker run --rm -v "$PWD:/root/Gazette" -w /root/Gazette -it \
  ghcr.io/autc04/retro68 /bin/bash
```

Then, inside the container:

```bash
TOOLCHAIN=/Retro68-build/toolchain

# See "The fenv.h workaround" below.
rm -f "$TOOLCHAIN/powerpc-apple-macos/include/fenv.h"

# Stage Apple's Universal Interfaces: <toolchain> <interfaces> <68k> <ppc> <carbon>
/Retro68-build/bin/interfaces-and-libraries.sh "$TOOLCHAIN" \
  /root/Gazette/third_party/AUI/Universal false true true

cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN/powerpc-apple-macos/cmake/retrocarbon.toolchain.cmake"
cmake --build build
```

### Output

`add_application()` runs MakePEF and Rez over the linked XCOFF and produces, in `build/`:

| File | What it is |
|------|------------|
| `Gazette.dsk` | Disk image — mount it in SheepShaver or Basilisk II |
| `Gazette.bin` | MacBinary II — for transfer to real hardware |
| `Gazette.APPL` | Raw application, data fork only |
| `Gazette.ad` + `%Gazette.ad` | AppleDouble pair (data fork + resource fork) |

CI additionally packages the AppleDouble pair into `Gazette.sit`, a StuffIt 1.5.1
archive that StuffIt Expander on OS 9 — or The Unarchiver on a modern machine —
will expand with the resource fork intact.

### The fenv.h workaround

Retro68's `interfaces-and-libraries.sh` stages Apple's headers by symlinking them
into the toolchain's include directory, and its `prepare-headers.sh` deliberately
whitelists Apple's `fenv.h` on the premise that *"newlib does not provide fenv.h"*.
Current Retro68 images **do** ship one, as a real file rather than a symlink, and
the staging script only unlinks symlinks before restaging — so the leftover real
file makes the symlink fail with `ln: failed to create symbolic link './fenv.h':
File exists`, aborting the whole step.

Deleting newlib's copy first resolves it, and Apple's is the one we want anyway:
`CoreServices.h` includes `<fenv.h>` directly.

### Icon

Not drawn yet. When it is, it belongs in the **resource fork** — `ICN#`, `icl8`,
`ics#` plus `BNDL` and `FREF`, rezzed in through `Resources/Gazette.r`. A `.icns`
file is meaningless for a CFM application on Mac OS 9.

The icon theme should evoke a classic newspaper / gazette / newsstand:
- Isometric or clean 3/4 view of a folded newspaper, small newsstand kiosk
- Platinum-era friendly colours (avoid flat modern design language)

## Continuous Integration

`.github/workflows/build.yml` defines a single job.

| Job | Runner | Toolchain | Artifact |
|-----|--------|-----------|----------|
| `build` | `ubuntu-latest` | `ghcr.io/autc04/retro68` (Docker) | `Gazette-carbon-ppc-macos9` |

It stages the Universal Interfaces, builds with the `retrocarbon` toolchain, packages
the `.sit`, and uploads `Gazette.dsk`, `Gazette.APPL`, `Gazette.bin` and `Gazette.sit`.

It runs on push and pull request when `src/`, `Resources/`, `CMakeLists.txt`,
`third_party/AUI/` or the workflow itself changes, and can be started by hand from
the Actions tab (`workflow_dispatch`).

## Project Structure

```
.
├── CMakeLists.txt          # Retro68 add_application() target (Carbon / PowerPC)
├── README.md               # This file
├── AGENT.md                # Project spec: constraints, phases, coding standards
├── .gitignore
│
├── .github/workflows/
│   └── build.yml           # Retro68 Carbon PPC build + .sit packaging
│
├── Resources/
│   ├── Gazette.r           # SIZE (8 MB / 4 MB), About alert, vers
│   └── Strings.r           # Placeholder for localization (not yet in the build)
│
├── src/
│   ├── main.cpp            # Carbon shell: CreateNewWindow, WaitNextEvent, menus
│   ├── core/               # Thin C seam (MacTypes.h only)
│   │   └── gazette_core.h/.c
│   ├── ui/                 # Platinum window helpers (Phase 3)
│   │   └── platinum_window.h/.c
│   ├── net/                # Gateway gw_net + Certainly (Phase 1)
│   │   └── gazette_net.h/.c
│   ├── feeds/              # RSS 2.0 / Atom parser, Google News (Phase 2)
│   │   └── gazette_feeds.h/.c
│   ├── extract/            # HTML stripping, transliteration (Phase 3)
│   │   └── gazette_extract.h/.c
│   ├── store/              # File-based caching (Phase 3)
│   │   └── gazette_store.h/.c
│   ├── prefs/              # Feed list persistence (Phase 0)
│   │   └── gazette_prefs.h/.c
│   └── portable/           # Pure C, host-testable (Phase 2)
│       └── gazette_portable.h/.c
│
├── third_party/
│   ├── AUI/                # Apple Universal Interfaces 3.4 (Carbon headers, CarbonLib)
│   └── certainly/          # (placeholder — to be vendored from Gateway)
│
└── tests/host/             # Host-testable parsers (Linux/Intel)
    └── test_portable.c     # Stub; not wired into the build yet
```

## Implementation Phases (per AGENT.md)

| Phase | Status | Description |
|-------|--------|-------------|
| 0 | **Mostly done** | Skeleton: Carbon shell, window, menus, WaitNextEvent loop, quit, SIZE resource. Icon, prefs loading and the host-test target are still open. |
| 1 | TODO | Networking: Gateway `gw_net` + Certainly, HTTPS fetch, HTTP GET with redirects |
| 2 | TODO | Feed engine: RSS 2.0 / Atom parser, Google News maps, feed auto-discovery |
| 3 | TODO | Full Platinum UI: sidebar + article list + reader pane, local caching |
| 4 | TODO | Polish: custom feed management, full-text fetch, search, read/unread, OPML import/export |

## Carbon on Mac OS 9 — what changes

Carbon is not a different UI toolkit; it is the subset of the Toolbox that survived
into Mac OS X, plus some newer replacements. Practical consequences for this codebase:

| Aspect | Classic Toolbox | What Gazette does |
|--------|-----------------|-------------------|
| Startup | `InitGraf`, `InitWindows`, `InitMenus`, `MaxApplZone` | None of these exist (`CALL_NOT_IN_CARBON`); CarbonLib sets up before `main()` |
| Windows | `NewCWindow`, or `GetNewCWindow` from a `WIND` | `CreateNewWindow()` with a window class and attributes |
| Menus | `GetNewMBar` from `MBAR` / `MENU` resources | `NewMenu` + `AppendMenu`, built programmatically |
| Events | `WaitNextEvent` + `EventRecord` | Same — CarbonLib supports it, and it keeps one cooperative loop for network polling |
| Ports | `SetPort((GrafPtr)window)` | `SetPortWindowPort()`; `WindowRef` is opaque |
| Closing | `CloseWindow` | `DisposeWindow` — `CloseWindow` is not in Carbon |
| Memory | SIZE resource | Still required on OS 9, and CarbonLib needs more headroom |
| Icons | Resource fork | Unchanged — resource fork, not `.icns` |

## References

- [Retro68](https://github.com/autc04/Retro68) — the cross-compiler and Rez this project builds with
- [Carbon Framework](https://developer.apple.com/library/archive/documentation/Carbon/Conceptual/CarbonOverview/CarbonOverview.html) — Apple's Carbon API documentation
- [Gateway](https://github.com/brunocastello/Gateway) — Networking/TLS stack (Certainly/BearSSL)
- [NewsProxy](https://github.com/brunocastello/NewsProxy) — Feed intelligence, Google News maps
- [Alex Robb's Newsstand 1.1](https://github.com/alexrobb/newsstand) — Original inspiration

## License

Copyright © 2026 brunocastello. All rights reserved.

Gazette is a standalone application. It reuses networking and feed-handling
intelligence from Gateway and NewsProxy respectively, but does not need to
speak the old Newsstand XML protocol unless explicitly added later.
