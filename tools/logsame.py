# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares two stretches of a LOG.TXT: the lines after the n-th line
matching START up to the next line matching END, for two (n, START, END)
choices. Timings ("123 ms"), file reads ("5 reads, 11 KB") and free
memory figures are masked first, since they change with the overlay area
and the disk; everything else must be equal. Used by the battle save
tests (tests/README.md): the same keys after a save and after loading it
must give the same LOG.

    python tools/logsame.py LOG.TXT "bsave state" 1 "load slot" 1 "bload state" 1 "load slot" 2
    (start pattern, occurrence, end pattern, occurrence after it) x 2

Exit code 0 if equal, 1 if not (the first difference is printed)."""

import re
import sys

MASK = [
    (re.compile(r'\d+ ms\b'), 'N ms'),
    (re.compile(r'\d+ reads, \d+ KB'), 'N reads, N KB'),
    (re.compile(r'free( memory)? \d+ KB \(\d+ KB in all\)'), 'free N KB'),
    (re.compile(r'free \d+ KB'), 'free N KB'),
]
# Lines of a battle's setup (the night panel's colours too) and of the
# turn's start, which a loaded battle has and the battle going on after
# its save has not (the battle track's load too, on AdLib machines).
DROP = re.compile(r'^(seed \d+ pack |craft at |T\d+: setup: |T\d+: Turn \d+ - X-COM|bload: '
                  r'|reads before the battle|packs loaded|map and sprites|floor pictures: '
                  r'|night: panel colours |music: track )')


def norm(line):
    for pat, rep in MASK:
        line = pat.sub(rep, line)
    return line


def stretch(lines, start, n_start, end, n_end):
    hits = [i for i, l in enumerate(lines) if re.search(start, l)]
    if len(hits) < n_start:
        sys.exit('no %d. line matching %r' % (n_start, start))
    first = hits[n_start - 1] + 1
    ends = [i for i in range(first, len(lines)) if re.search(end, lines[i])]
    last = ends[n_end - 1] if len(ends) >= n_end else len(lines)
    return [norm(l) for l in lines[first:last] if not DROP.search(l)]


def main():
    path = sys.argv[1]
    a = sys.argv[2:6]
    b = sys.argv[6:10]
    with open(path, encoding='latin-1', newline='') as f:
        lines = [l.rstrip('\r\n') for l in f]
    sa = stretch(lines, a[0], int(a[1]), a[2], int(a[3]))
    sb = stretch(lines, b[0], int(b[1]), b[2], int(b[3]))
    for i in range(max(len(sa), len(sb))):
        la = sa[i] if i < len(sa) else '(end)'
        lb = sb[i] if i < len(sb) else '(end)'
        if la != lb:
            print('differ at line %d of %d / %d:' % (i + 1, len(sa), len(sb)))
            print('  ' + la)
            print('  ' + lb)
            return 1
    print('same: %d lines' % len(sa))
    return 0


if __name__ == '__main__':
    sys.exit(main())
