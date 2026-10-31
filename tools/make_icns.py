#!/usr/bin/env python3
"""
make_icns.py - Gazette.icns from the classic icon family already in Rez.

The Finder icon is drawn once, by tools/generate_icon.py, and written into
Resources/Gazette_icon.r as ICN# / icl8 / ics# / ics8 data blocks. An .icns
file is a container whose elements are those very resources, so the OS X
bundle needs no new artwork: this script lifts the four data blocks (ID 128)
out of the Rez source and wraps them.

    element  size  what
    ICN#     256   32x32 1-bit icon and mask
    icl8    1024   32x32 8-bit icon (the standard Mac 256-colour palette)
    ics#      64   16x16 1-bit icon and mask
    ics8     256   16x16 8-bit icon

    make_icns.py Resources/Gazette_icon.r build/Gazette.icns
"""

import re
import struct
import sys

ELEMENTS = [("ICN#", 256), ("icl8", 1024), ("ics#", 64), ("ics8", 256)]


def data_block(source, kind, res_id):
    m = re.search(r"data\s+'%s'\s*\(\s*%d\b[^)]*\)\s*\{(.*?)\n\};" % (re.escape(kind), res_id),
                  source, re.DOTALL)
    if not m:
        raise SystemExit("make_icns.py: no data '%s' (%d) block" % (kind, res_id))
    # Only the $"..." hex strings are data; a "..." text string beside one
    # (the 'Gzt9' resource has one) and the /* comments */ are not.
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.DOTALL)
    hexes = re.findall(r'\$"([0-9A-Fa-f\s]*)"', body)
    return bytes.fromhex("".join(hexes).replace(" ", "").replace("\n", "").replace("\t", ""))


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    with open(argv[1], "r", encoding="latin-1") as f:
        source = f.read()
    parts = []
    for kind, size in ELEMENTS:
        payload = data_block(source, kind, 128)
        if len(payload) != size:
            raise SystemExit("make_icns.py: '%s' is %d bytes, wanted %d" % (kind, len(payload), size))
        parts.append(kind.encode("latin-1") + struct.pack(">I", 8 + size) + payload)
    body = b"".join(parts)
    with open(argv[2], "wb") as f:
        f.write(b"icns" + struct.pack(">I", 8 + len(body)) + body)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
