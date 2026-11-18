# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds a small IDE hard disk image (MBR + one FAT16 partition) holding the
given files in its root directory, and in subdirectories with --sub. Used as
drive C: in 86Box (boot DOS from the floppy).

    python tools/mkhdd.py --out emu/hdd.img FILE... [--sub DIR FILE...]...

--sub DIR FILE...: a subdirectory DIR of the root (an 8.3 name) holding these
files (the game's mode folders CGA, EGA, VGA: docs/design.md "Mode folders").

Geometry 200 cylinders x 16 heads x 63 sectors (98 MB; tools/emutest.ps1 gives
86Box the same numbers): one FAT16 partition of type 06 (BIGDOS, over 32 MB:
DOS 3.31 and later), 2 KB clusters, 512 root entries; it starts on head 1
like DOS's own FDISK does. 30 MB (900 x 4 x 17) until 2026-11-07, when the
E / C packs, the walk packs and the VGA packs outgrew it. 1024 root entries
were tried: MS-DOS 5 then lists the files but cannot execute XCOMXT.EXE, at
any geometry; 512 holds every pack with room (341 files in 2026-11).
"""

import argparse
import datetime
import os
import struct
import sys

CYLS, HEADS, SPT = 200, 16, 63
if os.environ.get("XT_HDD_GEOM"):       # tests: "cylinders,heads,sectors" (emutest.ps1 too)
    CYLS, HEADS, SPT = (int(v) for v in os.environ["XT_HDD_GEOM"].split(","))
TOTAL = CYLS * HEADS * SPT
START = SPT                     # cylinder 0, head 1, sector 1
SEC_PER_CLUS = 4
ROOT_ENTRIES = 512


def chs(lba):
    c = lba // (HEADS * SPT)
    h = (lba // SPT) % HEADS
    s = lba % SPT + 1
    return bytes((h, (s & 0x3F) | ((c >> 2) & 0xC0), c & 0xFF))


def dos_name(path):
    base = os.path.basename(path).upper()
    stem, _, ext = base.partition(".")
    if not stem or len(stem) > 8 or len(ext) > 3:
        sys.exit("not an 8.3 name: " + base)
    return stem.ljust(8).encode() + ext.ljust(3).encode()


def build(files, subs=()):
    part_secs = TOTAL - START
    root_secs = ROOT_ENTRIES * 32 // 512
    # FAT size: iterate until stable.
    fat_secs = 1
    while True:
        data_secs = part_secs - 1 - 2 * fat_secs - root_secs
        clusters = data_secs // SEC_PER_CLUS
        need = ((clusters + 2) * 2 + 511) // 512
        if need <= fat_secs:
            break
        fat_secs = need
    if not 4085 <= clusters < 65525:
        sys.exit("cluster count %d is not FAT16" % clusters)

    image = bytearray(TOTAL * 512)

    # MBR: boot code that chains to the floppy's boot sector (DOS boots from
    # A:), then one primary FAT16 partition (type 04 under 32 MB, else 06).
    mbr = bytearray(512)
    chain = bytes((
        0xFA, 0x31, 0xC0, 0x8E, 0xD0, 0xBC, 0x00, 0x7C,   # cli; xor ax,ax; ss=0; sp=7C00
        0x8E, 0xD8, 0x8E, 0xC0, 0xFB, 0xFC,               # ds=es=0; sti; cld
        0xBE, 0x00, 0x7C, 0xBF, 0x00, 0x06,               # si=7C00; di=0600
        0xB9, 0x00, 0x01, 0xF3, 0xA5,                     # rep movsw (relocate)
        0xEA, 0x1E, 0x06, 0x00, 0x00,                     # jmp 0000:061E
        0xB8, 0x01, 0x02, 0xBB, 0x00, 0x7C,               # ax=0201; bx=7C00
        0xB9, 0x01, 0x00, 0x31, 0xD2,                     # cx=0001; dx=0 (drive A)
        0xCD, 0x13, 0x72, 0x05,                           # int 13h; jc fail
        0xEA, 0x00, 0x7C, 0x00, 0x00,                     # jmp 0000:7C00
        0xCD, 0x18))                                      # fail: int 18h
    mbr[0:len(chain)] = chain
    entry = bytes((0x80,)) + chs(START) + bytes((0x04 if part_secs < 65536 else 0x06,)) + chs(TOTAL - 1) \
        + struct.pack("<II", START, part_secs)
    mbr[446:462] = entry
    mbr[510:512] = b"\x55\xAA"
    image[0:512] = mbr

    # Partition boot sector (not bootable: only the BPB matters).
    boot = bytearray(512)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"XCOMXT  "
    struct.pack_into("<HBHBHHBHHHII", boot, 11, 512, SEC_PER_CLUS, 1, 2, ROOT_ENTRIES,
                     part_secs if part_secs < 65536 else 0, 0xF8, fat_secs, SPT, HEADS,
                     START, 0 if part_secs < 65536 else part_secs)
    struct.pack_into("<BBBI11s8s", boot, 36, 0x80, 0, 0x29, 0x58434F4D,
                     b"XCOMXT     ", b"FAT16   ")
    boot[0x3E:0x40] = b"\xCD\x18"          # int 18h if anyone boots it
    boot[510:512] = b"\x55\xAA"
    base = START * 512
    image[base:base + 512] = boot

    fat = bytearray(fat_secs * 512)
    struct.pack_into("<HH", fat, 0, 0xFFF8, 0xFFFF)
    root = bytearray(root_secs * 512)
    data_start = base + (1 + 2 * fat_secs + root_secs) * 512
    clus_bytes = SEC_PER_CLUS * 512
    now = datetime.datetime.now()
    date = ((now.year - 1980) << 9) | (now.month << 5) | now.day
    time = (now.hour << 11) | (now.minute << 5) | (now.second // 2)
    entries = [struct.pack("<11sB10sHHHI", b"XCOMXT     ", 0x08, b"\0" * 10, time, date, 0, 0)]
    nxt = 2

    def put(data):
        """Writes data to a new cluster chain; returns its first cluster (0: empty)."""
        nonlocal nxt
        count = (len(data) + clus_bytes - 1) // clus_bytes
        first = nxt if count else 0
        for i in range(count):
            c = nxt + i
            struct.pack_into("<H", fat, c * 2, 0xFFFF if i == count - 1 else c + 1)
            off = data_start + (c - 2) * clus_bytes
            chunk = data[i * clus_bytes:(i + 1) * clus_bytes]
            image[off:off + len(chunk)] = chunk
        nxt += count
        return first

    def file_entry(path):
        with open(path, "rb") as f:
            data = f.read()
        return struct.pack("<11sB10sHHHI", dos_name(path), 0x20, b"\0" * 10,
                           time, date, put(data), len(data))

    for path in files:
        entries.append(file_entry(path))
    for sub in subs:
        name, sfiles = sub[0], sub[1:]
        # The directory's own clusters come first: "." needs its number.
        n_ent = 2 + len(sfiles)
        dir_bytes = (n_ent * 32 + clus_bytes - 1) // clus_bytes * clus_bytes
        first = put(bytes(dir_bytes))
        sub_ent = [struct.pack("<11sB10sHHHI", b".          ", 0x10, b"\0" * 10, time, date, first, 0),
                   struct.pack("<11sB10sHHHI", b"..         ", 0x10, b"\0" * 10, time, date, 0, 0)]
        sub_ent += [file_entry(p) for p in sfiles]
        blob = b"".join(sub_ent)
        c, k = first, 0
        while k < len(blob):        # the chain is contiguous
            off = data_start + (c - 2) * clus_bytes
            image[off:off + clus_bytes] = blob[k:k + clus_bytes].ljust(clus_bytes, b"\0")
            c, k = c + 1, k + clus_bytes
        entries.append(struct.pack("<11sB10sHHHI", dos_name(name), 0x10, b"\0" * 10,
                                   time, date, first, 0))
    if len(entries) > ROOT_ENTRIES:
        sys.exit("too many files")
    for i, e in enumerate(entries):
        root[i * 32:(i + 1) * 32] = e
    off = base + 512
    image[off:off + len(fat)] = fat
    image[off + len(fat):off + 2 * len(fat)] = fat
    image[off + 2 * len(fat):off + 2 * len(fat) + len(root)] = root
    used_mb = (nxt - 2) * clus_bytes / 1048576
    return image, used_mb


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--sub", nargs="+", action="append", default=[], metavar=("DIR", "FILE"),
                    help="a subdirectory of the root and its files")
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()
    image, used = build(args.files, args.sub)
    with open(args.out, "wb") as f:
        f.write(image)
    print("%s: %d MB disk, %d files, %.1f MB used%s" % (
        args.out, len(image) // 1048576, len(args.files) + sum(len(s) - 1 for s in args.sub), used,
        "".join(", %s\\ %d files" % (s[0], len(s) - 1) for s in args.sub)))


if __name__ == "__main__":
    main()
