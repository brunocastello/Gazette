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
#   src/net/, src/store/        Open Transport and the File Manager. The
#                               Winsock and Win32 file seams are the next
#                               two pieces of work.
#   src/feeds/gazette_feeds.c   Not in the host-tested set either: these
#   gazette_index.c, _photos.c  reach for the Toolbox.
#   third_party/certainly       Ships transport_win32.c, so TLS has a
#                               Windows transport waiting. It joins the
#                               build with src/net/, not before.
#
# Everything that is here is exactly the set tests/host already compiles
# with a plain cc, which is the same claim of portability made twice: if
# a module builds on Linux and under MinGW and on Retro68, it really does
# carry no system headers.

enable_language(RC)

set(GAZETTE_WIN_SOURCES
    # The Windows shell -- WinMain, the frame, the menu bar, the panes
    src/win/gazette_win_main.c

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

# comctl32 is missing from this list on purpose -- see InitControls() in
# src/win/gazette_win_main.c. The shell loads it with LoadLibrary so that
# a Windows 95 without Internet Explorer can still start the program.
target_link_libraries(Gazette PRIVATE user32 gdi32)

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
