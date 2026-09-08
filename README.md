# Gazette — Carbon RSS / Atom Reader for Mac OS 9 & Mac OS X

A native, Carbon-based RSS and Atom feed reader that runs on **Mac OS 9.x** and **Mac OS X (10.3+)**.

**Gazette** is a spiritual successor to Alex Robb's Newsstand 1.1 — a beautiful, fast, native replacement with full Platinum look-and-feel, modern HTTPS feed fetching, Google News support, and sensible offline caching.

## Features (planned)

- **Google News** — same countries and curated topics/sections as Newsstand 1.1
- **Custom feeds** — user-added RSS 2.0 and Atom feeds with auto-discovery
- **Platinum UI** — sidebar, article list, reader pane (Newsstand-inspired layout)
- **Offline caching** — feed lists and article bodies cached locally
- **Non-blocking networking** — all HTTPS via Gateway's Certainly/TLS stack

## Requirements

- **macOS** (any version with Carbon framework, which is all macOS from 10.3 onward)
- **CMake** 3.20+
- **Xcode command line tools** (for `clang`, `iconutil`)

## Building

### Step 1 — Install Xcode command line tools

```bash
xcode-select --install
```

### Step 2 — Build the Carbon application

```bash
cd /path/to/Gazette

# Create a build directory (out-of-tree recommended)
mkdir build && cd build

# Configure with CMake (uses system Carbon framework automatically)
cmake .. -DCMAKE_OSX_ARCHITECTURES="ppc;x86_64"

# Build the executable
make -j$(nproc)
```

The output will be a `Gazette` executable (or `Gazette.app` bundle) that runs on:
- **Mac OS 9.x** (PowerPC) — Classic Mac OS with Carbon support
- **Mac OS X 10.3+** (Tiger and later) — Universal Binary with Carbon

### Step 3 — Build an icon (optional)

To create a proper app icon for the Carbon bundle:

```bash
# Create an iconset directory with all required sizes
mkdir -p Resources/Gazette.iconset

# Generate from a source image (e.g., 1024×1024 PNG)
sips -z 16 16 source.png --out Resources/Gazette.iconset/icon_16x16.png
sips -z 32 32 source.png --out Resources/Gazette.iconset/icon_16x16@2x.png
sips -z 32 32 source.png --out Resources/Gazette.iconset/icon_32x32.png
sips -z 64 64 source.png --out Resources/Gazette.iconset/icon_32x32@2x.png
sips -z 128 128 source.png --out Resources/Gazette.iconset/icon_128x128.png
sips -z 256 256 source.png --out Resources/Gazette.iconset/icon_128x128@2x.png
sips -z 256 256 source.png --out Resources/Gazette.iconset/icon_256x256.png
sips -z 512 512 source.png --out Resources/Gazette.iconset/icon_256x256@2x.png
sips -z 512 512 source.png --out Resources/Gazette.iconset/icon_512x512.png
sips -z 1024 1024 source.png --out Resources/Gazette.iconset/icon_512x512@2x.png

# Build .icns from the iconset (CMake will copy it into the bundle)
iconutil -c icns Resources/Gazette.iconset -o build/Gazette.icns
```

The icon theme should evoke a classic newspaper / gazette / newsstand:
- Isometric or clean 3/4 view of a folded newspaper, small newsstand kiosk
- Platinum-era friendly colours (avoid flat modern design language)

## Continuous Integration

All builds are performed via **GitHub Actions** — nothing is compiled locally.

### Workflow overview (`.github/workflows/build.yml`)

| Job | Platform | Output |
|-----|----------|--------|
| `carbon-build` | macOS 12 (Monterey) — last with Carbon | `Gazette-carbon-macos12` (x86_64) |
| `portable-build` | Ubuntu Linux (latest) | `Gazette-host-tests-linux` (host tests only) |
| `universal-build` | macOS 12 (Monterey) — Carbon available | `Gazette-universal-macos12` (ppc + x86_64) |

### Triggering a build

Push to `main` or open a pull request — the workflow runs automatically.
Build artifacts (binaries) are uploaded as GitHub Actions artifacts and can be downloaded from the Actions tab.

### Local CI simulation (optional)

To run the same checks locally:

```bash
# Linux — portable tests only (no Carbon available)
mkdir build && cd build
cmake ..
make -j$(nproc) host_tests
./host_tests

# macOS — full Carbon build (requires Xcode 14.x or earlier)
mkdir build && cd build
cmake .. -DCMAKE_OSX_ARCHITECTURES="ppc;x86_64"
make -j$(nproc)
```

## Project Structure

```
.
├── CMakeLists.txt          # Carbon build configuration (ppc + x86_64)
├── README.md               # This file
├── .gitignore              # Build artifacts
├── AGENT.md                # Project spec (implementation phases)
│
├── .github/workflows/      # GitHub Actions CI configuration
│   └── build.yml           # Carbon + portable builds (CI)
│
├── Resources/              # Minimal resources for Carbon bundle
│   ├── Gazette.r           # SIZE resource (memory preferences)
│   └── Strings.r           # Placeholder for localization
│
├── src/
│   ├── main.cpp            # Carbon shell: CreateWindow, GetNextEvent, menus
│   ├── core/               # Thin C seam (no system headers)
│   │   ├── gazette_core.h  # Feed list & article opaque types
│   │   └── gazette_core.c  # Feed list (array-based) + stubs
│   ├── ui/                 # Carbon window helpers (Phase 3)
│   │   ├── platinum_window.h/.c
│   ├── net/                # Gateway + Certainly (Phase 1)
│   │   └── gazette_net.h/.c
│   ├── feeds/              # RSS 2.0 / Atom parser (Phase 2)
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
├── third_party/certainly/  # (placeholder — vendored from Gateway)
│
├── tests/host/             # Host-testable parsers (Linux/Intel)
│   └── test_portable.c
```

## Implementation Phases (per AGENT.md)

| Phase | Status  | Description                                    |
|-------|---------|-------------------------------------------------|
| 0     | **Done** | Skeleton: Carbon shell, CreateWindow, menus created programmatically, WaitNextEvent loop, quit |
| 1     | TODO    | Networking: Gateway `gw_net` + Certainly, HTTPS fetch, HTTP GET with redirects |
| 2     | TODO    | Feed engine: RSS 2.0 / Atom parser, Google News maps, feed auto-discovery |
| 3     | TODO    | Full Platinum UI: sidebar + article list + reader pane, local caching |
| 4     | TODO    | Polish: custom feed management, full-text fetch, search, read/unread, OPML import/export |

## Carbon vs. Classic Toolbox — Key Differences

| Aspect              | Classic Toolbox (old)          | Carbon (new)                          |
|---------------------|--------------------------------|---------------------------------------|
| Menus               | Loaded from resource fork      | Created programmatically (`CreateMenu`) |
| Windows             | `GetNewCWindow` from resource  | `CreateWindow` with class/attributes   |
| Events              | `EventRecord` (struct)         | `EventRef` (opaque handle)             |
| Memory              | Manual (`MaxMem`, `NewPtr`)   | Managed by OS (SIZE resource optional) |
| Compatibility       | Mac OS 9 only                  | Mac OS 9.x **and** Mac OS X (10.3+)  |
| Icons               | Resource fork (`ICON`, `mask`) | `.icns` file in app bundle            |

## References

- [Carbon Framework](https://developer.apple.com/library/archive/documentation/Carbon/Conceptual/CarbonOverview/CarbonOverview.html) — Apple's Carbon API documentation
- [Gateway](https://github.com/brunocastello/Gateway) — Networking/TLS stack (Certainly/BearSSL)
- [NewsProxy](https://github.com/brunocastello/NewsProxy) — Feed intelligence, Google News maps
- [Alex Robb's Newsstand 1.1](https://github.com/alexrobb/newsstand) — Original inspiration

## License

Copyright © 2026 brunocastello. All rights reserved.

Gazette is a standalone application. It reuses networking and feed-handling
intelligence from Gateway and NewsProxy respectively, but does not need to
speak the old Newsstand XML protocol unless explicitly added later.
