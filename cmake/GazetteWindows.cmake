# Gazette - the Win32 build target.
#
# Included from the top-level CMakeLists.txt when the target system is
# Windows. It is a separate file because it shares nothing with the
# Retro68 build above it: no add_application, no Rez, no resource fork,
# and an entirely different shell.
#
# What is deliberately absent, and why:
#
#   src/main.cpp, src/ui/       The Toolbox shell. src/win/ replaces it.
#   src/store/gazette_store.c   The File Manager. gazette_store_win32.c
#                               stands in for it (docs/windows.md says
#                               where its files go).
#   src/core/gazette_sys_mac.c  NewPtr, GetDateTime and ReadLocation;
#                               gazette_sys_win32.c stands in for it.
#   gazette_net_ot.c,           Open Transport. gazette_net_win32.c and
#   transport_ot.c, entropy.c   Certainly's transport_win32.c and
#                               entropy_win32.c stand in for them.
#
# The engine below is one static library, linked by the application and by
# GazetteNetTest, the console program CI runs under Wine to prove the
# network path against live servers.

enable_language(RC)

# Everything, everywhere in this build:
#
#   0x0400 is Windows 95 / NT 4.0, so the headers offer nothing later.
#   __USE_MINGW_ANSI_STDIO gives the C99 snprintf family from libmingwex.
#   msvcrt's _snprintf does not terminate on truncation and returns -1
#   rather than the length wanted, and the engine's snprintf calls rely on
#   both -- the same reason Gateway's Makefile.win32 sets it.
#   -Os and section garbage collection: the program has to fit on a floppy
#   with room to spare, and TLS is most of it.
add_compile_definitions(
    _WIN32_WINNT=0x0400
    WINVER=0x0400
    __USE_MINGW_ANSI_STDIO=1
    GAZETTE_WIN32=1        # the user-agent says Windows; see gazette_http.h
)
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
add_compile_options(-ffunction-sections -fdata-sections)

# ------------------------------------------------------------------ #
# TLS: BearSSL + Certainly, vendored from Gateway, as on the Mac      #
# ------------------------------------------------------------------ #

set(CERTAINLY_DIR ${CMAKE_SOURCE_DIR}/third_party/certainly)

file(GLOB BEARSSL_SOURCES
    ${CERTAINLY_DIR}/bearssl/src/*/*.c
    ${CERTAINLY_DIR}/bearssl/src/settings.c
)
add_library(bearssl STATIC ${BEARSSL_SOURCES})
target_include_directories(bearssl
    PUBLIC  ${CERTAINLY_DIR}/bearssl/inc
    PRIVATE ${CERTAINLY_DIR}/bearssl/src
)
# BearSSL's inner.h compiles a CryptGenRandom seeder for any _WIN32
# target. Certainly seeds every engine from its own pool, so that seeder
# is never used -- and it would put CryptoAPI in the import table, which a
# Windows 95 without OSR2 or IE 3.02 refuses to load. Gateway turns it
# off for the same reason; PATCHES.md §25 is the rest of that story.
target_compile_definitions(bearssl PRIVATE BR_USE_WIN32_RAND=0)
target_compile_options(bearssl PRIVATE -w)

# No CERTAINLY_OPEN_TRANSPORT: its absence is what selects the Winsock
# types and certainly_compat.h's calloc / free / GetTickCount.
add_library(certainly STATIC
    ${CERTAINLY_DIR}/src/certainly.c
    ${CERTAINLY_DIR}/src/transport_win32.c
    ${CERTAINLY_DIR}/src/entropy_win32.c
    ${CERTAINLY_DIR}/src/ca_roots.c
    ${CERTAINLY_DIR}/src/tls13_keysched.c
    ${CERTAINLY_DIR}/src/tls13_record.c
    ${CERTAINLY_DIR}/src/tls13_handshake.c
)
target_include_directories(certainly
    PUBLIC  ${CERTAINLY_DIR}/include
    PUBLIC  ${CERTAINLY_DIR}/src         # certainly_transport.h
    PRIVATE ${CERTAINLY_DIR}/bearssl/inc
)
target_compile_definitions(certainly PRIVATE BR_USE_WIN32_RAND=0)
target_compile_options(certainly PRIVATE -Wall -Wno-unused-parameter)
# Winsock 1.1: WSOCK32, on every Windows 95. WS2_32 was a separate
# download there, and nothing here needs it.
target_link_libraries(certainly PRIVATE bearssl PUBLIC wsock32)

# ------------------------------------------------------------------ #
# The engine: host-tested portable code, and the network over it      #
# ------------------------------------------------------------------ #

add_library(gazette_engine STATIC
    # Portable helpers -- pure C, host-tested (tests/host)
    src/portable/gazette_portable.c
    src/portable/gazette_url.c
    src/portable/gazette_http.c

    # Preferences and feed list -- portable, host-tested
    src/prefs/gazette_prefs.c
    src/prefs/gazette_opml.c

    # Feed engine and extraction -- the portable half
    src/feeds/gazette_feed_parse.c
    src/feeds/gazette_googlenews.c
    src/feeds/gazette_gnews_topics.c
    src/extract/gazette_extract.c

    # Networking -- the Mac's streams and fetch, unchanged, over
    # Certainly's Winsock transport; gazette_net_win32.c is the only
    # Windows file among them
    src/net/gazette_net.c
    src/net/gazette_net_win32.c
    src/net/gazette_fetch.c

    # The file store -- gazette_store.h in Win32 files, beside the
    # executable (docs/windows.md)
    src/store/gazette_store_win32.c

    # The engine above the store: the article store and its refresh, read
    # state, pictures and the core seam -- the Mac's files, over
    # gazette_sys_win32.c for memory, the clock and the time zone
    src/core/gazette_core.c
    src/core/gazette_sys_win32.c
    src/feeds/gazette_feeds.c
    src/feeds/gazette_index.c
    src/feeds/gazette_photos.c
)
target_include_directories(gazette_engine PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_options(gazette_engine PRIVATE -Wall -Wextra -Wno-unused-parameter)
# comdlg32 for the Open and Save As dialogs: on every Windows 95.
target_link_libraries(gazette_engine PUBLIC certainly comdlg32)

# The application apart from its window and menus: src/main.cpp's views,
# refresh queue and pumps, lifted into src/app/ so both shells run one
# copy. It reaches the window only through app/gazette_ui.h. A library of
# its own until gazette_win_window.c answers that header -- compiled on
# every build meanwhile, so nothing Mac-only creeps into it.
add_library(gazette_app STATIC src/app/gazette_app.c)
target_link_libraries(gazette_app PUBLIC gazette_engine)
target_compile_options(gazette_app PRIVATE -Wall -Wextra -Wno-unused-parameter)

set(GAZETTE_WIN_SOURCES
    # The Windows shell. The split mirrors the Mac build's: _main is
    # src/main.cpp and _window is src/ui/platinum_window.c.
    src/win/gazette_win_main.c
    src/win/gazette_win_window.c
)

add_executable(Gazette WIN32
    ${GAZETTE_WIN_SOURCES}
    Resources/win/Gazette.rc
)

set_target_properties(Gazette PROPERTIES OUTPUT_NAME "Gazette")

# src/ is the include root the engine expects ("prefs/gazette_prefs.h");
# Resources/win is where gazette_win_res.h lives, and windres is given
# the same list, which is how the .rc finds both that header and the
# manifest it names.
target_include_directories(Gazette PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/Resources/win
)

target_compile_options(Gazette PRIVATE -Wall -Wextra -Wno-unused-parameter)

# comctl32 is linked, and that is safe: the DLL shipped on the Windows 95
# CD. What did not ship with it is anything numbered after 4.0 --
# InitCommonControlsEx above all -- so InitControls() in
# src/win/gazette_win_main.c fetches that one by name instead. The rule
# for this list is the version, not the library: nothing newer than
# comctl32 4.0 may appear in the import table, which the workflow checks
# by printing it on every build.
target_link_libraries(Gazette PRIVATE gazette_engine user32 gdi32 comctl32)

# Static everything: no libgcc, no libstdc++, no pthread DLL. What is
# copied onto the disk image has to be the whole application.
#
# The subsystem and OS versions stamped in the PE header are what a
# Windows 95 or NT 4 loader reads to decide whether it is allowed to run
# the file at all. Current MinGW defaults to a later pair and the binary
# is refused before it starts, with no message worth reading.
target_link_options(Gazette PRIVATE
    -static
    -static-libgcc
    -Wl,--gc-sections
    -mwindows
    -Wl,--major-subsystem-version,4
    -Wl,--minor-subsystem-version,0
    -Wl,--major-os-version,4
    -Wl,--minor-os-version,0
)

# The application has to fit on a floppy with room to spare -- tools/
# make_img.sh is what puts it there. Stripping is the difference between
# comfortably and not.
target_link_options(Gazette PRIVATE $<$<CONFIG:Release>:-s>)

# ------------------------------------------------------------------ #
# GazetteNetTest: the network path as a console program               #
#                                                                    #
# Not shipped. windows.yml runs it under Wine against live feeds, over #
# TLS 1.3 and TLS 1.2 servers, which is the only way to see the Winsock #
# path work without a Windows machine. Same engine, same flags.        #
# ------------------------------------------------------------------ #

add_executable(GazetteNetTest tests/win/gazette_nettest.c)
target_link_libraries(GazetteNetTest PRIVATE gazette_engine)
target_link_options(GazetteNetTest PRIVATE
    -static
    -static-libgcc
    -Wl,--gc-sections
    -Wl,--major-subsystem-version,4
    -Wl,--minor-subsystem-version,0
    -Wl,--major-os-version,4
    -Wl,--minor-os-version,0
)

# GazetteStoreTest: the file store the same way, run under Wine by CI.
add_executable(GazetteStoreTest tests/win/gazette_storetest.c)
target_link_libraries(GazetteStoreTest PRIVATE gazette_engine)
target_link_options(GazetteStoreTest PRIVATE -static -static-libgcc
    -Wl,--gc-sections)

# GazetteEngineTest: core, refresh, cache and read state, end to end.
add_executable(GazetteEngineTest tests/win/gazette_enginetest.c)
target_link_libraries(GazetteEngineTest PRIVATE gazette_engine)
target_link_options(GazetteEngineTest PRIVATE -static -static-libgcc
    -Wl,--gc-sections)

