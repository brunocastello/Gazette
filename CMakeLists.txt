# Gazette — Carbon RSS / Atom Reader for Mac OS 9 & Mac OS X
# Copyright (c) 2026 brunocastello
#
# CMake project targeting Carbon (works on Mac OS 9.x and Mac OS X).

cmake_minimum_required(VERSION 3.20)
project(Gazette C CXX)

# ------------------------------------------------------------------ #
# Carbon configuration                                                #
# ------------------------------------------------------------------ #

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 99)

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

add_executable(Gazette MACOSX_DEFAULT_ENCODING ${GAZETTE_SOURCES})

# Link against Carbon framework (available on Mac OS 9.x and Mac OS X)
find_library(CARBON_FRAMEWORK Carbon)
if(CARBON_FRAMEWORK)
    target_link_libraries(Gazette PRIVATE ${CARBON_FRAMEWORK})
else()
    message(WARNING "Carbon framework not found — build may fail on non-macOS hosts")
endif()

# Include directories
target_include_directories(Gazette PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

# ------------------------------------------------------------------ #
# Icon resource — still useful for Carbon app bundles                 #
# ------------------------------------------------------------------ #

find_program(ICONUTIL iconutil)
if(ICONUTIL)
    set(ICONSET_DIR "${CMAKE_SOURCE_DIR}/Resources/Gazette.iconset")

    if(EXISTS "${ICONSET_DIR}")
        set(ICON_FILE "${CMAKE_BINARY_DIR}/Gazette.icns")

        add_custom_command(
            OUTPUT "${ICON_FILE}"
            COMMAND ${ICONUTIL} -c icns "${ICONSET_DIR}" -o "${ICON_FILE}"
            DEPENDS ${ICONSET_DIR}
            COMMENT "Building .icns from iconset"
        )

        # Copy into app bundle during build
        add_custom_command(
            TARGET Gazette POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Gazette.app/Contents/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy "${ICON_FILE}" "${CMAKE_BINARY_DIR}/Gazette.app/Contents/Resources/"
            COMMENT "Copying icon into app bundle"
        )
    endif()
endif()

# ------------------------------------------------------------------ #
# Host test target (Linux/Intel for portable code)                    #
# ------------------------------------------------------------------ #

add_executable(host_tests
    tests/host/test_portable.c
)

target_include_directories(host_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/portable
)

# ------------------------------------------------------------------ #
# Install / deploy                                                    #
# ------------------------------------------------------------------ #

install(TARGETS Gazette BUNDLE DESTINATION .)
