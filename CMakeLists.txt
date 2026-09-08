# Gazette — Carbon RSS / Atom Reader for Mac OS 9 (PowerPC)
# Copyright (c) 2026 brunocastello
#
# Retro68 CMake project targeting Carbon / PowerPC (retrocarbon toolchain).
#
# Configure with:
#   cmake -S . -B build \
#     -DCMAKE_TOOLCHAIN_FILE=/Retro68-build/toolchain/powerpc-apple-macos/cmake/retrocarbon.toolchain.cmake

cmake_minimum_required(VERSION 3.20)
project(Gazette C CXX)

# The engine is C99 and the Toolbox shell is C++17, but both need GNU
# extensions on: Apple's Universal Interfaces lean on GCC-specific syntax
# (and Retro68 enables Pascal string literals as an extension).
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON)

# Source files — Phase 0: main.cpp (Carbon) + core stub
set(GAZETTE_SOURCES
    src/main.cpp

    # Core (Phase 0) — thin C seam
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

# Resource fork: SIZE (8 MB preferred / 4 MB minimum), About alert, version.
set(GAZETTE_RESOURCES
    Resources/Gazette.r
)

# ------------------------------------------------------------------ #
# Build target                                                        #
# ------------------------------------------------------------------ #

# add_application() comes from the Retro68 toolchain file. It is what turns
# the linked XCOFF into a real Mac application: MakePEF plus a Rez pass over
# RetroCarbonAPPL.r, producing Gazette.bin (MacBinary), Gazette.APPL,
# Gazette.dsk (disk image) and the Gazette.ad / %Gazette.ad AppleDouble pair.
# A plain add_executable() stops at the XCOFF and produces none of those.
add_application(Gazette
    ${GAZETTE_SOURCES}
    ${GAZETTE_RESOURCES}
    TYPE APPL
    CREATOR "Gzt9"
)

target_include_directories(Gazette PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)
