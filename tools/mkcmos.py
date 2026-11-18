# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes an 86Box NVR file with a valid CMOS setup (360K drive A:, no AT
hard disk, EGA/VGA, 640 KB base memory), so an AT-class machine boots
without stopping at a setup prompt (86Box starts with an empty CMOS).
Used by tools/emutest.ps1 for at286 (IBM XT 286), u286 (IBM AT) and, with
--ami, for at386 and u386 (AMI BIOS 05/05/91).

    python tools/mkcmos.py <out.nvr> <extended KB> [size] [--ami <AMI BIOS ROM>]

--ami <rom>: an AMI BIOS of 1990/91 (86Box asus386, dataexpert386sx, kmxc02:
05/05/91) keeps its ADVANCED CMOS SETUP in 11h, 13h, 2Dh and 34h-3Dh and its
chipset setup in 40h up to a ROM-given limit, with a second checksum in
3Eh/3Fh (big-endian sum of 34h-3Dh and 40h-limit, which must not be 0). It
says "CMOS system options not set" when bit 7 of 0Eh is set and "CMOS
checksum failure" when either sum is wrong. The values come from the ROM
itself, as its "Load BIOS Setup Defaults" leaves them: its table of CMOS
10h-3Fh (code at F000:03CA copies it), then its two lists of (register,
mask, setup default, power-on default) applied as F000:0471 does with AL=1.
"""
import sys


def ami_defaults(rom):
    """CMOS 10h-7Fh as the AMI BIOS's setup defaults leave them, and the last
    register its second checksum covers (3Fh or more)."""
    d = open(rom, 'rb').read()
    w = lambda a: d[a] | d[a + 1] << 8
    sig = ((0, b'0123AAAAMMMMIIII'), (0x3CA, bytes((0xB9, 0x30, 0, 0xBE))),
           (0x477, bytes((0x2E, 0x8B, 0x0E))), (0x482, bytes((0x2E, 0x8B, 0x0E))),
           (0x56B, bytes((0x2E, 0x3A, 0x0E))))
    if any(d[a:a + len(b)] != b for a, b in sig):
        sys.exit('mkcmos: %s is not an AMI BIOS of the 05/05/91 kind' % rom)
    limit = d[w(0x56E)] & 0x7F
    c = bytearray(0x80)
    c[0x10:0x40] = d[w(0x3CE):w(0x3CE) + 0x30]
    for cnt, tab in ((w(w(0x485)), w(0x488)), (w(w(0x47A)), w(0x47D))):
        for i in range(cnt):
            reg, mask, val = d[tab + 4 * i], d[tab + 4 * i + 1], d[tab + 4 * i + 2]
            reg &= 0x7F
            c[reg] = c[reg] & ~mask & 0xFF | val & mask
    return c, limit


def bcd(v):
    return (v // 10) << 4 | v % 10


def main():
    args = sys.argv[1:]
    ami = None
    if '--ami' in args:
        k = args.index('--ami')
        ami = args[k + 1]
        del args[k:k + 2]
    out, ext = args[0], int(args[1])
    size = int(args[2]) if len(args) > 2 else 128
    c = bytearray(max(size, 64))
    if ami:
        dflt, limit = ami_defaults(ami)
        c[0x10:] = dflt[0x10:len(c)]     # chipset registers past the limit too
    c[0:10] = bytes([0, 0, 0, 0, bcd(12), 0, 4, bcd(21), bcd(10), bcd(26)])
    c[0x0A], c[0x0B], c[0x0D] = 0x26, 0x02, 0x80     # RTC running, 24 h, valid
    c[0x10] = 0x10              # A: 5.25" 360K, B: none
    c[0x14] = c[0x14] & 0xCC | 0x01 if ami else 0x01    # one floppy drive, EGA/VGA, no FPU
    c[0x15], c[0x16] = 0x80, 0x02                   # 640 KB base memory
    c[0x17], c[0x18] = ext & 0xFF, ext >> 8
    c[0x30], c[0x31] = ext & 0xFF, ext >> 8
    c[0x32] = bcd(19)
    s = sum(c[0x10:0x2E])
    c[0x2E], c[0x2F] = s >> 8, s & 0xFF
    if ami:
        s = sum(c[0x34:0x3E]) + sum(c[0x40:limit + 1])
        c[0x3E], c[0x3F] = s >> 8, s & 0xFF
    open(out, 'wb').write(bytes(c[:size]))


if __name__ == '__main__':
    main()
