# Gazette - cross-compiling toolchain for the Win32 build.
#
# Configure with:
#   cmake -S . -B build-win \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.toolchain.cmake \
#     -DCMAKE_BUILD_TYPE=Release
#
# i686, never x86_64: the targets run from Windows 95 to XP and half of
# them are 32-bit only, so one 32-bit binary is the whole line.
#
# The -win32 suffixed driver is preferred where the distribution ships
# both. It is the one built with Win32 threads; the -posix one links
# libwinpthread-1.dll, and a DLL beside the executable is exactly what a
# single-file application for these machines must not need.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(GAZETTE_MINGW_PREFIX i686-w64-mingw32)

find_program(CMAKE_C_COMPILER
    NAMES ${GAZETTE_MINGW_PREFIX}-gcc-win32 ${GAZETTE_MINGW_PREFIX}-gcc
    REQUIRED)
find_program(CMAKE_CXX_COMPILER
    NAMES ${GAZETTE_MINGW_PREFIX}-g++-win32 ${GAZETTE_MINGW_PREFIX}-g++
    REQUIRED)
find_program(CMAKE_RC_COMPILER
    NAMES ${GAZETTE_MINGW_PREFIX}-windres
    REQUIRED)

set(CMAKE_FIND_ROOT_PATH /usr/${GAZETTE_MINGW_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
