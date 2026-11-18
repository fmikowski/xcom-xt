# SPDX-License-Identifier: GPL-3.0-or-later
# Checks that an XS24 campaign save loaded by the current build and saved again
# (XS25) holds the same campaign: builds the XS25 body from the XS24 one by
# the layout change alone (the bases block's lists widened, zero-filled) and
# compares it byte for byte with the new save.
#   py -3.12 tools\xs24check.py OLD.SAV NEW.SAV
# Sizes are those of the 8086 build (SaveHeader 40, BaseHdr 24, WayPt 6, Craft 138,
# ResProj 6, ProdRun 8, BaseRec 390 bytes before its res list); XS24 sizes: 16 way
# points, 24 craft, 16 research projects and 10 production runs a base.
import sys

HDR, BHDR, WP, CRAFT, RES, PROD, REC_HEAD = 40, 24, 6, 138, 6, 8, 390
OLD = dict(wp=16, craft=24, res=16, prod=10)
NEW = dict(wp=44, craft=44, res=79, prod=32)


def find_bases(b):
    """Offset of the bases block (n, cur, headers) in the body."""
    for p in range(HDR + 4, len(b) - 8):
        n = int.from_bytes(b[p:p + 2], 'little')
        cur = int.from_bytes(b[p + 2:p + 4], 'little')
        if not (1 <= n <= 8 and cur < n):
            continue
        name = b[p + 4:p + 20]
        k = name.find(b'\0')
        if k < 1 or any(c < 32 or c > 126 for c in name[:k]) or any(name[k:]):
            continue
        return p, n
    raise SystemExit('no bases block found')


def widen(old):
    p, n = find_bases(old)
    out = bytearray(old[:HDR])
    out += b'XS25' + old[HDR + 4:p]
    q = p + 4 + BHDR * n
    out += old[p:q]
    pad = lambda k, size: bytes((NEW[k] - OLD[k]) * size)
    out += old[q:q + OLD['wp'] * WP] + pad('wp', WP)
    q += OLD['wp'] * WP
    out += old[q:q + 2]                                     # the next way point's id
    q += 2
    out += old[q:q + OLD['craft'] * CRAFT] + pad('craft', CRAFT)
    q += OLD['craft'] * CRAFT
    for _ in range(n):
        out += old[q:q + REC_HEAD]
        q += REC_HEAD
        out += old[q:q + OLD['res'] * RES] + pad('res', RES)
        q += OLD['res'] * RES
        out += old[q:q + OLD['prod'] * PROD] + pad('prod', PROD)
        q += OLD['prod'] * PROD
    out += old[q:]                                          # soldiers, aliens, graphs
    return bytes(out)


old = open(sys.argv[1], 'rb').read()
new = open(sys.argv[2], 'rb').read()
if old[HDR:HDR + 4] != b'XS24' or new[HDR:HDR + 4] != b'XS25':
    raise SystemExit('magic: %r -> %r' % (old[HDR:HDR + 4], new[HDR:HDR + 4]))
want = widen(old)
if want[HDR:] == new[HDR:]:
    print('same: %s (%d bytes) -> %s (%d bytes)' % (sys.argv[1], len(old), sys.argv[2], len(new)))
    sys.exit(0)
at = next((i for i in range(min(len(want), len(new))) if i >= HDR and want[i] != new[i]), min(len(want), len(new)))
print('DIFFERENT at body offset %d (expected %d bytes, got %d)' % (at - HDR, len(want), len(new)))
sys.exit(1)
