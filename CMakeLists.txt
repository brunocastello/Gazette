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

    # UI — the Platinum three-pane window, and every Toolbox call it makes
    src/ui/platinum_window.c
    src/ui/gazette_dialogs.c

    # Networking — stream over Certainly's Open Transport client + TLS,
    # and the non-blocking HTTP GET driven from the event loop
    src/net/gazette_net.c
    src/net/gazette_fetch.c

    # Feed engine — RSS/Atom parser, Google News maps, article store
    src/feeds/gazette_feeds.c
    src/feeds/gazette_index.c
    src/feeds/gazette_feed_parse.c
    src/feeds/gazette_googlenews.c
    src/feeds/gazette_gnews_topics.c

    # HTML extraction — stripping, full-text (Phase 3)
    src/extract/gazette_extract.c

    # Local store — the only File Manager calls in the application
    src/store/gazette_store.c

    # Preferences and feed list — portable, host-tested
    src/prefs/gazette_prefs.c
    src/prefs/gazette_opml.c

    # Portable helpers — pure C, host-tested (tests/host)
    src/portable/gazette_portable.c
    src/portable/gazette_url.c
    src/portable/gazette_http.c
)

# Resource fork: SIZE (8 MB preferred / 4 MB minimum), About alert, version,
# and the Finder icon family with the BNDL/FREF that binds it to 'Gzt9'.
# Gazette_icon.r is generated -- see tools/generate_icon.py -- and so are the
# sidebar's and the toolbar's own small icons in Gazette_ui_icons.r; see
# tools/generate_ui_icons.py.
set(GAZETTE_RESOURCES
    Resources/Gazette.r
    Resources/Gazette_icon.r
    Resources/Gazette_ui_icons.r
)

# ------------------------------------------------------------------ #
# TLS: BearSSL + Certainly, vendored from Gateway                     #
#                                                                     #
# Constraint 4 -- do not reimplement TLS. third_party/certainly is a  #
# byte-for-byte copy of Gateway's, so security fixes on either side    #
# transfer with a plain diff; the Carbon adaptation is entirely in the #
# two definitions below and needs no source change. See PATCHES.md.    #
# ------------------------------------------------------------------ #

set(CERTAINLY_DIR ${CMAKE_SOURCE_DIR}/third_party/certainly)

# BearSSL is plain C99 against the standard library, so it needs nothing
# from the Toolbox and nothing Carbon-specific.
file(GLOB_RECURSE BEARSSL_SOURCES
    ${CERTAINLY_DIR}/bearssl/src/aead/*.c
    ${CERTAINLY_DIR}/bearssl/src/codec/*.c
    ${CERTAINLY_DIR}/bearssl/src/ec/*.c
    ${CERTAINLY_DIR}/bearssl/src/hash/*.c
    ${CERTAINLY_DIR}/bearssl/src/int/*.c
    ${CERTAINLY_DIR}/bearssl/src/kdf/*.c
    ${CERTAINLY_DIR}/bearssl/src/mac/*.c
    ${CERTAINLY_DIR}/bearssl/src/rand/*.c
    ${CERTAINLY_DIR}/bearssl/src/rsa/*.c
    ${CERTAINLY_DIR}/bearssl/src/ssl/*.c
    ${CERTAINLY_DIR}/bearssl/src/symcipher/*.c
    ${CERTAINLY_DIR}/bearssl/src/x509/*.c
    ${CERTAINLY_DIR}/bearssl/src/settings.c
)

add_library(bearssl STATIC ${BEARSSL_SOURCES})
target_include_directories(bearssl
    PUBLIC  ${CERTAINLY_DIR}/bearssl/inc
    PRIVATE ${CERTAINLY_DIR}/bearssl/src
)

# Certainly speaks Open Transport, which is where Carbon actually bites.
#
# OTCARBONAPPLICATION=1 is the whole adaptation. Under Carbon the plain
# OTOpenEndpoint, OTOpenInternetServices and InitOpenTransport entry points do
# not exist -- their Availability: blocks read "CarbonLib: not available", and
# a Carbon application must call the OTClientContext-taking *InContext forms
# instead. With this macro set, Apple's own headers define the old spellings
# as macros forwarding to the new ones with a NULL context, which is exactly
# what an application wants. So Certainly's transport_ot.c compiles for Carbon
# untouched; without the macro it does not compile at all, which is the right
# way round for a mistake this quiet to fail.
add_library(certainly STATIC
    ${CERTAINLY_DIR}/src/certainly.c
    ${CERTAINLY_DIR}/src/transport_ot.c
    ${CERTAINLY_DIR}/src/entropy.c
    ${CERTAINLY_DIR}/src/ca_roots.c
    ${CERTAINLY_DIR}/src/tls13_keysched.c
    ${CERTAINLY_DIR}/src/tls13_record.c
    ${CERTAINLY_DIR}/src/tls13_handshake.c
)

target_compile_definitions(certainly PUBLIC
    CERTAINLY_OPEN_TRANSPORT             # select transport_ot.c, EndpointRef CTSocket
    OTCARBONAPPLICATION=1                # see above
)

target_include_directories(certainly
    PUBLIC  ${CERTAINLY_DIR}/include
    PUBLIC  ${CERTAINLY_DIR}/src         # certainly_transport.h, for CTSocket
    PRIVATE ${CERTAINLY_DIR}/bearssl/inc
)
target_link_libraries(certainly PRIVATE bearssl)
target_compile_options(certainly PRIVATE -Wall -Wno-unused-parameter)

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

# src/net/ drives Open Transport directly for plaintext HTTP, so it needs the
# same Carbon redirection Certainly does. PUBLIC on the certainly target
# already carries both definitions here; naming them again would be a second
# place to get them wrong.
target_link_libraries(Gazette certainly bearssl)
