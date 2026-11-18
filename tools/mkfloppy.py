# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a FAT12 floppy image from a boot sector and a list of files.

The disk format (360K, 720K, 1.2M, 1.44M) is taken from the BPB in the boot
sector. Files are stored contiguously in the order given, so passing IO.SYS and
MSDOS.SYS first (with --system) produces a bootable MS-DOS disk.

    python tools/mkfloppy.py --boot emu/dos5/bootsect.bin --out emu/boot.img \
        --system emu/dos5/IO.SYS emu/dos5/MSDOS.SYS -- emu/dos5/COMMAND.COM ...
"""

import argparse
import datetime
import os
import struct
import sys

ATTR_READONLY = 0x01
ATTR_HIDDEN = 0x02
ATTR_SYSTEM = 0x04
ATTR_VOLUME = 0x08
ATTR_ARCHIVE = 0x20


def dos_name(path):
    base = os.path.basename(path).upper()
    stem, _, ext = base.partition(".")
    if not stem or len(stem) > 8 or len(ext) > 3:
        sys.exit(f"not an 8.3 name: {base}")
    return stem.ljust(8).encode("ascii") + ext.ljust(3).encode("ascii")


def dos_datetime(when):
    date = ((when.year - 1980) << 9) | (when.month << 5) | when.day
    time = (when.hour << 11) | (when.minute << 5) | (when.second // 2)
    return time, date


def fat12_set(fat, cluster, value):
    off = cluster * 3 // 2
    if cluster & 1:
        fat[off] = (fat[off] & 0x0F) | ((value << 4) & 0xF0)
        fat[off + 1] = (value >> 4) & 0xFF
    else:
        fat[off] = value & 0xFF
        fat[off + 1] = (fat[off + 1] & 0xF0) | ((value >> 8) & 0x0F)


def build(boot, files, label):
    (bytes_per_sec, sec_per_clus, reserved, nfats, root_ents, total_secs,
     media, sec_per_fat) = struct.unpack_from("<HBHBHHBH", boot, 11)
    if bytes_per_sec != 512:
        sys.exit("only 512-byte sectors are supported")

    image = bytearray(total_secs * 512)
    image[0:512] = boot

    root_secs = root_ents * 32 // 512
    fat_start = reserved * 512
    root_start = (reserved + nfats * sec_per_fat) * 512
    data_start = root_start + root_secs * 512
    clus_bytes = sec_per_clus * 512
    n_clusters = (total_secs * 512 - data_start) // clus_bytes

    fat = bytearray(sec_per_fat * 512)
    fat12_set(fat, 0, 0xF00 | media)
    fat12_set(fat, 1, 0xFFF)

    entries = []
    time, date = dos_datetime(datetime.datetime.now())

    next_cluster = 2
    for path, attr in files:
        with open(path, "rb") as f:
            data = f.read()
        count = (len(data) + clus_bytes - 1) // clus_bytes
        first = next_cluster if count else 0
        if next_cluster + count - 2 > n_clusters:
            sys.exit(f"disk full while adding {path}")
        for i in range(count):
            c = next_cluster + i
            fat12_set(fat, c, 0xFFF if i == count - 1 else c + 1)
            off = data_start + (c - 2) * clus_bytes
            chunk = data[i * clus_bytes:(i + 1) * clus_bytes]
            image[off:off + len(chunk)] = chunk
        next_cluster += count
        entries.append(struct.pack("<11sB10sHHHI", dos_name(path), attr,
                                   b"\0" * 10, time, date, first, len(data)))

    # The label goes last: DOS boot sectors expect IO.SYS and MSDOS.SYS
    # to be the first two root directory entries.
    if label:
        name = label.upper()[:11].ljust(11).encode("ascii")
        entries.append(struct.pack("<11sB10sHHHI", name, ATTR_VOLUME,
                                   b"\0" * 10, time, date, 0, 0))

    if len(entries) > root_ents:
        sys.exit("too many files for the root directory")
    for i in range(nfats):
        off = fat_start + i * sec_per_fat * 512
        image[off:off + len(fat)] = fat
    for i, entry in enumerate(entries):
        off = root_start + i * 32
        image[off:off + 32] = entry

    free_kb = (n_clusters - (next_cluster - 2)) * clus_bytes // 1024
    return image, free_kb


def data_boot_sector():
    """A 360K boot sector for a data disk: prints a note and reboots."""
    bpb = struct.pack("<3s8sHBHBHHBHHHII", b"\xEB\x3C\x90", b"XCOMXT  ", 512, 2, 1, 2,
                      112, 720, 0xFD, 2, 9, 2, 0, 0)
    bpb += bytes((0, 0, 0x29)) + struct.pack("<I", 0x58434F4D) + b"XCOMXT     " + b"FAT12   "
    msg = b"Not a boot disk. Boot DOS, then run XCOMXT.\r\n\0"
    # at 0x3E: push cs; pop ds; xor bx,bx; mov si,msg; [lodsb; or al,al;
    #          jz wait; mov ah,0Eh; int 10h; jmp loop]; wait: xor ax,ax;
    #          int 16h; int 19h
    code_len = 24
    msg_off = 0x7C00 + 0x3E + code_len
    code = bytes((0x0E, 0x1F, 0x31, 0xDB, 0xBE)) + struct.pack("<H", msg_off)
    code += bytes((0xAC, 0x08, 0xC0, 0x74, 0x06, 0xB4, 0x0E, 0xCD, 0x10, 0xEB, 0xF5,
                   0x31, 0xC0, 0xCD, 0x16, 0xCD, 0x19))
    assert len(code) == code_len
    sector = bytearray(512)
    sector[0:len(bpb)] = bpb
    sector[0x3E:0x3E + len(code)] = code
    sector[0x3E + len(code):0x3E + len(code) + len(msg)] = msg
    sector[510:512] = b"\x55\xAA"
    return bytes(sector)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--boot", help="512-byte boot sector with BPB (default: data disk)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--label", default="")
    ap.add_argument("--system", nargs="*", default=[],
                    help="system files, stored first with R/H/S attributes")
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()

    if args.boot:
        with open(args.boot, "rb") as f:
            boot = f.read(512)
    else:
        boot = data_boot_sector()
    if len(boot) != 512 or boot[510:512] != b"\x55\xAA":
        sys.exit("boot sector must be 512 bytes ending in 55 AA")

    sys_attr = ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_ARCHIVE
    files = [(p, sys_attr) for p in args.system]
    files += [(p, ATTR_ARCHIVE) for p in args.files]
    image, free_kb = build(boot, files, args.label)
    with open(args.out, "wb") as f:
        f.write(image)
    print(f"{args.out}: {len(image) // 1024} KB, {len(files)} files, {free_kb} KB free")


if __name__ == "__main__":
    main()
