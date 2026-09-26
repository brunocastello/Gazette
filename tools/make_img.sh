#!/bin/sh
#
# make_img.sh - package the Windows build the way a period machine can
# actually take delivery of it.
#
#   make_img.sh <path to Gazette.exe> <output directory> [Setup.exe]
#
# Produces two files, which are the Windows answer to the Mac build's
# .sit and .dsk:
#
#   Gazette.zip   the executable and its read-me, for a machine that is
#                 already on a network, and for anyone who just wants
#                 the .exe out of it.
#   Gazette.img   a 1.44 MB FAT12 floppy image. 86Box mounts one from
#                 its own menu with no change to the machine's
#                 configuration, which is what makes it the format for
#                 testing: boot the profile, mount the image, run
#                 A:\GAZETTE.EXE.
#
# The floppy is built with mtools rather than a loop mount so that this
# runs unprivileged, on CI and on a developer's machine alike.
#
# When the application outgrows a floppy this script stops and says so.
# The replacement is a FAT16 hard disk image -- mpartition -I -c then
# mformat, 16 heads and 63 sectors so the geometry is one 86Box offers
# in its list -- and the change belongs here, not in the workflow.

set -eu

EXE=${1:-}
OUT=${2:-}
SETUP=${3:-}

if [ -z "$EXE" ] || [ -z "$OUT" ]; then
    echo "usage: make_img.sh <Gazette.exe> <output directory> [Setup.exe]" >&2
    exit 2
fi

if [ ! -f "$EXE" ]; then
    echo "make_img.sh: no such file: $EXE" >&2
    exit 1
fi

mkdir -p "$OUT"
# Absolute, because the archive is built from inside the staging
# directory so that the zip carries bare names and no path.
OUT=$(cd "$OUT" && pwd)

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

# 8.3 and upper case. Long names would survive on 95 and later but not
# on a plain NT 4 command prompt's eye, and the floppy is read on both.
cp "$EXE" "$STAGE/GAZETTE.EXE"

# The read-me and the licence, CRLF throughout: they are read in Notepad,
# which before XP does not break lines on a bare LF and shows a whole file
# as one line. The read-me is installer/README.TXT, the one the installer
# carries, so the three ways of getting Gazette say the same thing.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
sed 's/$/\r/' "$ROOT/installer/README.TXT" > "$STAGE/README.TXT"
sed 's/$/\r/' "$ROOT/LICENSE"              > "$STAGE/LICENSE.TXT"

# The installer, when there is one: on the floppy and in the zip beside
# the bare executable, which still runs from either with nothing installed.
if [ -n "$SETUP" ]; then
    cp "$SETUP" "$STAGE/SETUP.EXE"
fi

# ------------------------------------------------------------------ #
# The archive                                                        #
# ------------------------------------------------------------------ #

rm -f "$OUT/Gazette.zip"
(cd "$STAGE" && zip -q -X "$OUT/Gazette.zip" *)

# ------------------------------------------------------------------ #
# The floppy                                                         #
# ------------------------------------------------------------------ #

# 1474560 bytes on the disk, less the boot sector, two 9-sector FATs and
# a 14-sector root directory: what is actually left for files.
FLOPPY_FREE=1457664

TOTAL=0
for f in "$STAGE"/*; do
    SIZE=$(wc -c < "$f")
    # FAT12 on a 1.44 MB disk allocates in 512-byte clusters; a file
    # costs the next whole cluster up, which matters when the margin is
    # this thin.
    TOTAL=$((TOTAL + (SIZE + 511) / 512 * 512))
done

if [ "$TOTAL" -gt "$FLOPPY_FREE" ]; then
    echo "make_img.sh: $TOTAL bytes will not fit on a 1.44 MB floppy" >&2
    echo "  (the disk holds $FLOPPY_FREE once formatted)" >&2
    echo "  Gazette has outgrown the floppy image. Switch this script to" >&2
    echo "  a FAT16 hard disk image -- see the note at the top." >&2
    exit 1
fi

IMG="$OUT/Gazette.img"
rm -f "$IMG"

# -C creates the file, -f 1440 sets the 3.5" HD geometry, and the label
# is what the machine shows for the drive.
MTOOLS_SKIP_CHECK=1 mformat -C -f 1440 -v GAZETTE -i "$IMG" ::
for f in "$STAGE"/*; do
    MTOOLS_SKIP_CHECK=1 mcopy -i "$IMG" "$f" ::/
done

echo "Packaged:"
ls -l "$OUT/Gazette.zip" "$IMG"
MTOOLS_SKIP_CHECK=1 mdir -i "$IMG" ::/
