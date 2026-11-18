#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Sums up the LOGs of tools/ailof.ps1 (docs/design.md "Alien AI"): for each, the
alien turns' total and longest, the median, mean and longest go of an alien.
  ailof.py a.log b.log ..
"""
import re
import statistics
import sys


def times(path):
    turns, cheap, seen_go = {}, [], []
    lines = open(path, errors='replace').read().splitlines()
    free = ''
    for s in lines:
        m = re.match(r'T(\d+): alien turn (\d+) ms', s)
        if m:
            turns[int(m[1])] = int(m[2])
        m = re.match(r'T(\d+): alien \d+ at .* seen (\d), (\d+) ms', s)
        if m and int(m[3]):
            (seen_go if m[2] == '1' else cheap).append(int(m[3]))
        if 'DOS free at start' in s:
            free = s
    res = [s for s in lines if s.startswith('result ')]
    return turns, cheap, seen_go, free, res


def main(paths):
    print('%-22s %5s %9s %9s %8s %8s %8s' % ('log', 'turns', 'total s', 'longest', 'med go', 'mean go', 'max go'))
    for p in paths:
        t, g, sg, free, res = times(p)
        name = p.replace('\\', '/').split('/')[-1]
        if not t:
            print('%-22s no finished alien turn' % name)
            continue
        all_go = g + sg
        print('%-22s %5d %9.1f %9.1f %8d %8d %8d  unseen med %s  %s %s' % (
            name, len(t), sum(t.values()) / 1000, max(t.values()) / 1000,
            statistics.median(all_go) if all_go else 0,
            statistics.mean(all_go) if all_go else 0, max(all_go) if all_go else 0,
            int(statistics.median(g)) if g else '-', res[0][:24] if res else '', free[:40]))


if __name__ == '__main__':
    main(sys.argv[1:])
