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

# Source files. Phase 0 is complete through main.cpp, the core seam, the
# portable helpers, the preferences and the file store; the rest are still
# the placeholders their comments say they are.
set(GAZETTE_SOURCES
    src/main.cpp

    # Core — thin C seam between the Toolbox shell and the engine
    src/core/gazette_core.c

    # UI — Carbon window helpers (Phase 3)
    src/ui/platinum_window.c

    # Networking — Gateway + Certainly (Phase 1)
    src/net/gazette_net.c

    # Feed engine — RSS/Atom, Google News (Phase 2)
    src/feeds/gazette_feeds.c

    # HTML extraction — stripping, full-text (Phase 3)
    src/extract/gazette_extract.c

    # Local store — the only File Manager calls in the application
    src/store/gazette_store.c

    # Preferences and feed list — portable, host-tested
    src/prefs/gazette_prefs.c

    # Portable helpers — pure C, host-tested (tests/host)
    src/portable/gazette_portable.c
)

# Resource fork: SIZE (8 MB preferred / 4 MB minimum), About alert, version,
# and the Finder icon family with the BNDL/FREF that binds it to 'Gzt9'.
# Gazette_icon.r is generated -- see tools/generate_icon.py.
set(GAZETTE_RESOURCES
    Resources/Gazette.r
    Resources/Gazette_icon.r
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

# Everything below src/ includes its siblings by directory-qualified path
# ("prefs/gazette_prefs.h", "store/gazette_store.h"), so src/ is the only
# include root needed — and it is the same one tests/host compiles against,
# which keeps the portable modules honest about where their headers live.
target_include_directories(Gazette PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
