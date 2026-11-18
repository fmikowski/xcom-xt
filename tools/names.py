# SPDX-License-Identifier: GPL-3.0-or-later
"""Soldier names: NAMES.PAK from OpenXcom's SoldierName/*.nam files.

    python tools\\names.py C:\\Games\\OpenXcom\\common\\SoldierName packs

Layout (read by src/base.c soldier_name):
    "XNM1", u8 n_nations, n x u32 offset of the nation's block
    block: u16 count[4] (male first, female first, male last, female last
           names), then u16 offsets of every name (from the block start) in
           that order, then the names (u8 length, the letters).
A pool without female last names uses the male ones (OpenXcom
SoldierNamePool::load); names keep only ASCII letters, spaces, '-' and '''.
"""

import os
import struct
import sys
import unicodedata


def clean(name):
    """ASCII only: accents dropped, other scripts left out."""
    name = unicodedata.normalize("NFKD", name)
    out = "".join(c for c in name if c.isascii() and (c.isalpha() or c in " -'"))
    return out.strip()[:23]


def build_names(name_dir):
    pools = []
    for fn in sorted(os.listdir(name_dir)):
        if not fn.lower().endswith(".nam"):
            continue
        doc, key = {}, None
        # A plain reader: one file has a broken quoted name at its end.
        with open(os.path.join(name_dir, fn), encoding="utf-8") as f:
            for line in f:
                if line[:1].isalpha() and line.rstrip().endswith(":"):
                    key = line.strip()[:-1]
                    doc[key] = []
                elif key and line.strip().startswith("- "):
                    doc[key].append(line.strip()[2:].strip().strip("\"'"))
        lists = [doc.get("maleFirst") or [], doc.get("femaleFirst") or [],
                 doc.get("maleLast") or []]
        lists.append(doc.get("femaleLast") or lists[2])
        lists = [[n for n in (clean(str(x)) for x in l) if n] for l in lists]
        if lists[0]:
            pools.append(lists)
    blocks = []
    for lists in pools:
        names = [n for l in lists for n in l]
        head = 8 + 2 * len(names)
        offs, data = [], b""
        for n in names:
            offs.append(head + len(data))
            data += bytes([len(n)]) + n.encode("ascii")
        assert head + len(data) < 65536
        blocks.append(struct.pack("<4H", *[len(l) for l in lists])
                      + struct.pack("<%dH" % len(offs), *offs) + data)
    out = b"XNM1" + bytes([len(blocks)])
    pos = len(out) + 4 * len(blocks)
    for b in blocks:
        out += struct.pack("<I", pos)
        pos += len(b)
    return out + b"".join(blocks), len(blocks)


def main():
    name_dir, outdir = sys.argv[1:3]
    data, n = build_names(name_dir)
    with open(os.path.join(outdir, "NAMES.PAK"), "wb") as f:
        f.write(data)
    print("NAMES.PAK", n, "nationalities", len(data), "bytes")


if __name__ == "__main__":
    main()
