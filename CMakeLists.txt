# Gazette — Carbon RSS / Atom Reader for Mac OS 9 (PowerPC)
# Copyright (c) 2026 brunocastello
#
# Retro68 CMake project targeting Carbon / PowerPC (retrocarbon toolchain).

cmake_minimum_required(VERSION 3.20)
project(Gazette C CXX)

# Source files — Phase 0: main.cpp (Carbon) + core stub
set(GAZETTE_SOURCES
    src/main.cpp

    # Core (Phase 0) — pure C, no system headers
    src/core/gazette_core.c

    # UI — Carbon window helpers (Phase 3)
    src/ui/platinum_window.c

    # Networking — Gateway + Certainly (Phase 1)
    src/net/gazette_net.c

    # Feed engine — RSS/Atom, Google News (Phase 2)
    src/feeds/gazette_feeds.c

    # HTML extraction — stripping, transliteration (Phase 3)
    src/extract/gazette_extract.c

    # Local store — file-based caching (Phase 3)
    src/store/gazette_store.c

    # Preferences — load/save (Phase 0)
    src/prefs/gazette_prefs.c

    # Portable helpers — pure C, host-testable (Phase 2)
    src/portable/gazette_portable.c
)

set(GAZETTE_HEADERS
    src/core/gazette_core.h
    src/ui/platinum_window.h
)

# ------------------------------------------------------------------ #
# Build targets                                                     #
# ------------------------------------------------------------------ #

add_executable(Gazette ${GAZETTE_SOURCES})

# Include directories
target_include_directories(Gazette PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

# ------------------------------------------------------------------ #
# Install / deploy                                                    #
# ------------------------------------------------------------------ #

install(TARGETS Gazette BUNDLE DESTINATION .)
