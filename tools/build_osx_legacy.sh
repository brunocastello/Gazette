#!/usr/bin/env bash
#
# build_osx_legacy.sh - one half of the Mac OS X 10.4 - 10.14 Universal Binary.
#
# Gazette for Mac OS 9 is Carbon; OS X runs Carbon too, so the same shell is
# compiled again as Mach-O. This script is that build for ONE architecture.
# .github/workflows/osx-legacy.yml runs it twice and lipo's the results:
#
#   build_osx_legacy.sh pascal              on the runner, once: converts the
#                                           "\p" literals (python3 needed)
#   build_osx_legacy.sh ppc  compile|link   inside gcc-powerpc-apple-darwin8
#   build_osx_legacy.sh i386 compile|link   inside the same image, which also
#                                           carries clang and ld64 (but no
#                                           python, hence the first line)
#
# Environment:
#   SDK    the MacOSX10.4u.sdk to build against. For ppc the image's own;
#          for i386 the full phracker one, because the image's has its i386
#          slices stripped (see the workflow).
#   OUT    build directory (default build/osx)
#   GAZETTE_ALLOW_UNDEFINED=1
#          while src/net/gazette_net_posix.c and Certainly's
#          transport_posix.c / entropy_posix.c do not all exist, fall back to
#          a link that ignores undefined symbols so the bundle, zip and dmg
#          steps can still be proved. Refused once all three files exist.
#
# It does not use CMakeLists.txt: that build is Retro68's and ends in a PEF.
# The source list below is that file's GAZETTE_SOURCES with Open Transport
# (gazette_net_ot.c, transport_ot.c, entropy.c) swapped for BSD sockets
# (gazette_net_posix.c, transport_posix.c, entropy_posix.c). Keep the two
# lists in step.

set -euo pipefail

ARCH=${1:?usage: build_osx_legacy.sh pascal | ppc|i386 compile|link}
OUT=${OUT:-build/osx}
PASCAL=$OUT/pascal
CERT=third_party/certainly

SHELL_SOURCES=(
    src/main.cpp
    src/app/gazette_app.c
    src/core/gazette_core.c
    src/core/gazette_sys_mac.c
    src/ui/platinum_window.c
    src/ui/gazette_dialogs.c
    src/net/gazette_net.c
    src/net/gazette_net_posix.c
    src/net/gazette_fetch.c
    src/feeds/gazette_feeds.c
    src/feeds/gazette_photos.c
    src/feeds/gazette_index.c
    src/feeds/gazette_feed_parse.c
    src/feeds/gazette_googlenews.c
    src/feeds/gazette_gnews_topics.c
    src/extract/gazette_extract.c
    src/store/gazette_store.c
    src/prefs/gazette_prefs.c
    src/prefs/gazette_opml.c
    src/portable/gazette_portable.c
    src/portable/gazette_url.c
    src/portable/gazette_http.c
    $CERT/src/certainly.c
    $CERT/src/transport_posix.c
    $CERT/src/entropy_posix.c
    $CERT/src/ca_roots.c
    $CERT/src/tls13_keysched.c
    $CERT/src/tls13_record.c
    $CERT/src/tls13_handshake.c
)
# The three files written for the BSD-sockets port. Until they all exist the
# link cannot succeed, and the compile step says so rather than failing on a
# missing file.
POSIX_FILES=(src/net/gazette_net_posix.c $CERT/src/transport_posix.c $CERT/src/entropy_posix.c)

# A source containing a "\p" literal is converted first: Retro68's gcc reads
# those as Pascal strings, these compilers do not (tools/pascal_strings.py).
# The sources that need it are found by looking, not by a list, so a new one
# cannot be forgotten; the copies and a manifest of them go in $PASCAL, and
# both halves compile from the same copies.
if [ "$ARCH" = pascal ]; then
    rm -rf "${PASCAL:?}"
    mkdir -p "$PASCAL"
    : > "$PASCAL/MANIFEST"
    for src in "${SHELL_SOURCES[@]}"; do
        [ -f "$src" ] || continue
        if ! python3 -I tools/pascal_strings.py --check "$src"; then
            mkdir -p "$PASCAL/$(dirname "$src")"
            python3 -I tools/pascal_strings.py "$src" "$PASCAL/$src"
            echo "$src" >> "$PASCAL/MANIFEST"
        fi
    done
    echo "converted: $(tr '\n' ' ' < "$PASCAL/MANIFEST")"
    exit 0
fi

PHASE=${2:?usage: build_osx_legacy.sh ppc|i386 compile|link}
: "${SDK:?SDK must name a MacOSX10.4u.sdk}"
OBJ=$OUT/obj-$ARCH

# ------------------------------------------------------------------ #
# The toolchains                                                      #
# ------------------------------------------------------------------ #
case $ARCH in
  ppc)
    # gcc 14 for powerpc-apple-darwin8: Apple's SDK, no Apple extensions.
    CC=powerpc-apple-darwin8-gcc
    ARCHFLAGS="-arch ppc -isysroot $SDK -mmacosx-version-min=10.4"
    ;;
  i386)
    # The image's clang compiles for any Mach-O target; its ld64 links
    # (clang's own darwin driver would want a compiler-rt that is not there).
    CC="clang -target i386-apple-darwin8"
    ARCHFLAGS="-isysroot $SDK -mmacosx-version-min=10.4 -fno-stack-protector"
    ;;
  *) echo "unknown architecture $ARCH" >&2; exit 2 ;;
esac

# ------------------------------------------------------------------ #
# Headers                                                             #
#                                                                     #
# The sources include Apple's headers flat (<MacWindows.h>, <Lists.h>) as  #
# they do on Mac OS 9, never the <Carbon/Carbon.h> umbrella. On OS X those #
# headers live inside nested frameworks, so each Headers directory is      #
# added by hand, as -isystem so their warnings stay Apple's. -F is there   #
# for the <CarbonCore/MacTypes.h> style includes Apple's headers use       #
# among themselves.                                                       #
# ------------------------------------------------------------------ #
FW=$SDK/System/Library/Frameworks
SYSINC=""
FWFLAGS="-F$FW"
for top in Carbon CoreServices ApplicationServices; do
    FWFLAGS="$FWFLAGS -F$FW/$top.framework/Frameworks"
    for d in "$FW/$top.framework/Headers" "$FW/$top.framework/Frameworks/"*.framework/Headers; do
        [ -d "$d" ] && SYSINC="$SYSINC -isystem $d"
    done
done
for top in QuickTime CoreFoundation; do
    [ -d "$FW/$top.framework/Headers" ] && SYSINC="$SYSINC -isystem $FW/$top.framework/Headers"
done

# ------------------------------------------------------------------ #
# Flags                                                               #
# ------------------------------------------------------------------ #
#   GAZETTE_MACOSX   the shell's own OS X branches (#ifdef GAZETTE_MACOSX)
#   CERTAINLY_POSIX  selects Certainly's BSD-sockets transport
#                    (not CERTAINLY_OPEN_TRANSPORT; see certainly_compat.h)
#   -Wno-multichar   'Gzt9' and every other four-character code
COMMON="$ARCHFLAGS -O2 -DGAZETTE_MACOSX=1 -DCERTAINLY_POSIX -Wall -Wno-multichar \
-Wno-deprecated-declarations -Isrc -I$CERT/include -I$CERT/src -I$CERT/bearssl/inc \
$FWFLAGS $SYSINC"
CFLAGS="$COMMON -std=gnu99"
# No exceptions, no RTTI: nothing in the shell needs them, and without them
# main.cpp references no libstdc++ (the link uses the C driver, not g++).
CXXFLAGS="$COMMON -x c++ -std=gnu++17 -fno-exceptions -fno-rtti"
# BearSSL: its x86 AES-NI/SSE2/RDRAND and POWER8 paths are switched off, as
# on Mac OS 9; a G3 or a Core Duo takes the portable code.
BEARFLAGS="-w -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_RDRAND=0 -DBR_POWER8=0 -I$CERT/bearssl/src"


compile_one() {   # compile_one <app|bear> <source>
    local kind=$1 src=$2 obj in flags=""
    obj=$OBJ/$(echo "$src" | tr / _).o
    in=$src
    if [ "$kind" = app ] && grep -qx "$src" "$PASCAL/MANIFEST"; then
        in=$PASCAL/$src
        # The copy lives elsewhere; "gazette_dialogs.h" still has to be found.
        flags="-iquote $(dirname "$src")"
    fi
    case $src in
        *.cpp) flags="$flags $CXXFLAGS" ;;
        *)     flags="$flags $CFLAGS"; [ "$kind" = bear ] && flags="$flags $BEARFLAGS" ;;
    esac
    # shellcheck disable=SC2086
    $CC $flags -c "$in" -o "$obj"
}
export -f compile_one
export CC CFLAGS CXXFLAGS BEARFLAGS OBJ PASCAL

FRAMEWORKS="-framework Carbon -framework QuickTime -framework CoreFoundation"

case $PHASE in
  compile)
    mkdir -p "$OBJ" "$PASCAL"
    find "${OBJ:?}" -name '*.o' -delete
    for f in "${POSIX_FILES[@]}"; do
        [ -f "$f" ] || echo "::warning::$f does not exist yet; skipped, so the link will fail"
    done

    list=$OUT/sources-$ARCH.txt
    : > "$list"
    for s in "${SHELL_SOURCES[@]}"; do
        [ -f "$s" ] && echo "app $s" >> "$list"
    done
    for s in $CERT/bearssl/src/*/*.c $CERT/bearssl/src/settings.c; do
        echo "bear $s" >> "$list"
    done

    echo "compiling $(wc -l < "$list") files for $ARCH"
    JOBS=$( (nproc || sysctl -n hw.ncpu) 2>/dev/null || echo 2)
    # xargs runs every file even when one fails, so a single run shows every
    # error; it exits non-zero if any did.
    xargs -P "$JOBS" -L1 bash -c 'compile_one "$@" || { echo "FAILED: $2" >&2; exit 1; }' _ < "$list" \
        || { echo "::error::compile failed for $ARCH" >&2; exit 1; }
    echo "$ARCH: all translation units compiled"
    ;;

  link)
    LINKOUT=$OUT/Gazette-$ARCH
    case $ARCH in
      ppc)
        link() { $CC -arch ppc -isysroot "$SDK" -mmacosx-version-min=10.4 "$@" \
                     $FRAMEWORKS -o "$LINKOUT"; } ;;
      i386)
        LD=$(ls /usr/local/bin/*-ld | head -1)
        link() { $LD -arch i386 -macosx_version_min 10.4 -syslibroot "$SDK" \
                     -o "$LINKOUT" "$SDK/usr/lib/crt1.o" "$@" $FRAMEWORKS -lSystem; } ;;
    esac
    if link "$OBJ"/*.o > "$OUT/link-$ARCH.log" 2>&1; then
        cat "$OUT/link-$ARCH.log"
        echo "$ARCH: linked $LINKOUT"
        exit 0
    fi
    cat "$OUT/link-$ARCH.log"
    echo "::error::$ARCH link failed"

    # While the BSD-sockets files are missing, the only acceptable link
    # failure is the transport they provide.
    absent=0
    for f in "${POSIX_FILES[@]}"; do [ -f "$f" ] || absent=1; done
    if [ "${GAZETTE_ALLOW_UNDEFINED:-0}" != 1 ] || [ $absent = 0 ]; then
        exit 1
    fi
    echo "::warning::$ARCH: relinking with undefined symbols allowed; this binary will not run"
    case $ARCH in
      ppc)  $CC -arch ppc -isysroot "$SDK" -mmacosx-version-min=10.4 "$OBJ"/*.o \
                $FRAMEWORKS -Wl,-undefined,dynamic_lookup -o "$LINKOUT" ;;
      i386) $LD -arch i386 -macosx_version_min 10.4 -syslibroot "$SDK" -undefined dynamic_lookup \
                -o "$LINKOUT" "$SDK/usr/lib/crt1.o" "$OBJ"/*.o $FRAMEWORKS -lSystem ;;
    esac
    ;;
  *) echo "unknown phase $PHASE" >&2; exit 2 ;;
esac
